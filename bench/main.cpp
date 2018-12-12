#include <cstdio>
#include <ctime>
#include <thread>
#include <condition_variable>
#include <random>
#include <hiredis/hiredis.h>

#include "constants.h"
#include "generator.h"

using namespace std;

const char *ips[3] = {"192.168.188.135",
                      "192.168.188.136",
                      "192.168.188.137"};

class task_queue
{
    int n = 0;
    mutex m;
    condition_variable cv;
public:
    void worker()
    {
        unique_lock<mutex> lk(m);
        while (n == 0)
            cv.wait(lk);
        n--;
    }

    void add()
    {
        {
            lock_guard<mutex> lk(m);
            n++;
        }
        cv.notify_all();
    }
};

void time_max()
{
    redisContext *c = redisConnect("127.0.0.1", 6379);
    if (c == nullptr || c->err)
    {
        if (c)
        {
            printf("Error: %s\n", c->errstr);
        }
        else
        {
            printf("Can't allocate redis context\n");
        }
        return;
    }
    struct timeval t1{}, t2{};
    gettimeofday(&t1, nullptr);
    redisReply *reply;
    thread threads[THREAD_PER_SERVER];
    reply = static_cast<redisReply *>(redisCommand(c, "VCNEW s"));
    freeReplyObject(reply);

//    default_random_engine e;
//    uniform_int_distribution<unsigned> u(0, 4);

    for (thread &t : threads)
    {
        t = thread([] {
            redisContext *cl = redisConnect("127.0.0.1", 6379);
            redisReply *r;
            for (int i = 0; i < 10000; i++)
            {
                r = static_cast<redisReply *>(redisCommand(cl, "VCINC s"));
                freeReplyObject(r);
            }
            redisFree(cl);
        });
    }
    for (thread &t : threads)
    {
        t.join();
    }

    reply = static_cast<redisReply *>(redisCommand(c, "VCGET s"));
    printf("%s\n", reply->str);
    freeReplyObject(reply);
    gettimeofday(&t2, nullptr);
    double time_diff_sec = (t2.tv_sec - t1.tv_sec) + (double) (t2.tv_usec - t1.tv_usec) / 1000000;
    printf("%f, %f\n", time_diff_sec, (2.0 + THREAD_PER_SERVER * 10000) / time_diff_sec);
    redisFree(c);
}

void conn_one_server(const char *ip, const int port, vector<thread *> &thds, generator &gen)
{
    for (int i = 0; i < THREAD_PER_SERVER; ++i)
    {
        auto t = new thread([ip, port, &thds, &gen] {
            redisContext *c = redisConnect(ip, port);
            for (int times = 0; times < OP_PER_THREAD; ++times)
            {
                gen.gen_exec(c);
                this_thread::sleep_for(chrono::microseconds((int) SLEEP_TIME));
            }
            redisFree(c);
        });
        thds.emplace_back(t);
    }
}

void conn_one_server_timed(const char *ip, const int port, vector<thread *> &thds, generator &gen,
                           vector<task_queue *> &tasks)
{
    for (int i = 0; i < THREAD_PER_SERVER; ++i)
    {
        auto task = new task_queue();
        auto t = new thread([ip, port, &thds, &gen, task] {
            redisContext *c = redisConnect(ip, port);
            for (int times = 0; times < OP_PER_THREAD; ++times)
            {
                task->worker();
                gen.gen_exec(c);
            }
            redisFree(c);
        });
        thds.emplace_back(t);
        tasks.emplace_back(task);
    }
}

void test_local(z_type zt)
{
    vector<thread *> thds;
    queue_log qlog;
    generator gen(zt, qlog);

    for (int i = 0; i < 5; ++i)
    {
        conn_one_server("127.0.0.1", 6379 + i, thds, gen);
    }

    bool mb = true, ob = true;
    thread max([&mb, &qlog, zt] {
        cmd c(zt, zmax, -1, -1, qlog);
        redisContext *cl = redisConnect("127.0.0.1", 6379);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfor-loop-analysis"
        while (mb)
        {
            this_thread::sleep_for(chrono::seconds(TIME_MAX));
            c.exec(cl);
        }
#pragma clang diagnostic pop
        redisFree(cl);
    });
    thread overhead([&ob, &qlog, zt] {
        cmd c(zt, zoverhead, -1, -1, qlog);
        redisContext *cl = redisConnect("127.0.0.1", 6381);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfor-loop-analysis"
        while (ob)
        {
            this_thread::sleep_for(chrono::seconds(TIME_OVERHEAD));
            c.exec(cl);
        }
#pragma clang diagnostic pop
        redisFree(cl);
    });
    for (auto t:thds)
        t->join();
    mb = false;
    ob = false;
    printf("ending.\n");
    max.join();
    overhead.join();
    gen.join();
    qlog.write_file(zcmd[zt]);
}

void test_dis(int n, z_type zt)
{
    vector<thread *> thds;
    vector<task_queue *> tasks;
    queue_log qlog;
    generator gen(zt, qlog);

    timeval t1{}, t2{};
    gettimeofday(&t1, nullptr);

    for (auto ip:ips)
        for (int i = 0; i < n; ++i)
            conn_one_server_timed(ip, 6379 + i, thds, gen, tasks);

    thread timer([&tasks] {
        timeval td{}, tn{};
        gettimeofday(&td, nullptr);
        for (int times = 0; times < OP_PER_THREAD; ++times)
        {
            for (auto t:tasks)
            {
                t->add();
            }
            //this_thread::sleep_for(chrono::microseconds((int)SLEEP_TIME));
            gettimeofday(&tn, nullptr);
            auto slp_time =
                    (td.tv_sec - tn.tv_sec) * 1000000 + td.tv_usec - tn.tv_usec + (long) ((times + 1) * INTERVAL_TIME);
            this_thread::sleep_for(chrono::microseconds(slp_time));
        }
    });

    bool mb = true, ob = true;
    thread max([&mb, &qlog, zt] {
        cmd c(zt, zmax, -1, -1, qlog);
        redisContext *cl = redisConnect(ips[0], 6379);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfor-loop-analysis"
        while (mb)
        {
            this_thread::sleep_for(chrono::seconds(TIME_MAX));
            c.exec(cl);
        }
#pragma clang diagnostic pop
        redisFree(cl);
    });
    thread overhead([&ob, &qlog, zt] {
        cmd c(zt, zoverhead, -1, -1, qlog);
        redisContext *cl = redisConnect(ips[1], 6379);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfor-loop-analysis"
        while (ob)
        {
            this_thread::sleep_for(chrono::seconds(TIME_OVERHEAD));
            c.exec(cl);
        }
#pragma clang diagnostic pop
        char temp[10];
        sprintf(temp, "%czopcount", zcmd[zt]);
        auto r = static_cast<redisReply *>(redisCommand(cl, temp));
        printf("%lli\n", r->integer);
        freeReplyObject(r);
        redisFree(cl);
    });

    timer.join();
    for (auto t:thds)
        t->join();

    gettimeofday(&t2, nullptr);
    double time_diff_sec = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0;
    printf("%f, %f\n", time_diff_sec, TOTAL_OPS / time_diff_sec);

    mb = false;
    ob = false;
    printf("ending.\n");

    max.join();
    overhead.join();
    gen.join();

    qlog.write_file(zcmd[zt]);

    for (auto t :thds)
        delete t;
    for (auto t :tasks)
        delete t;
}

void test_count_dis_one(const char *ip, const int port, z_type zt)
{
    thread threads[THREAD_PER_SERVER];
    queue_log qlog;
    generator gen(zt, qlog);

    timeval t1{}, t2{};
    gettimeofday(&t1, nullptr);

    for (thread &t : threads)
    {
        t = thread([&ip, &port, &gen] {
            redisContext *c = redisConnect(ip, port);
            for (int times = 0; times < 20000; ++times)
            {
                gen.gen_exec(c);
            }
            redisFree(c);
        });
    }

    for (thread &t : threads)
    {
        t.join();
    }

    gettimeofday(&t2, nullptr);
    double time_diff_sec = (t2.tv_sec - t1.tv_sec) + (double) (t2.tv_usec - t1.tv_usec) / 1000000;
    printf("%f, %f\n", time_diff_sec, 20000 * THREAD_PER_SERVER / time_diff_sec);

    redisContext *cl = redisConnect(ips[1], port);
    auto r = static_cast<redisReply *>(redisCommand(cl, "ozopcount"));
    printf("%lli\n", r->integer);
    freeReplyObject(r);
    redisFree(cl);
}

int main(int argc, char *argv[])
{
    //time_max();
    //test_count_dis_one(ips[0],6379);
    test_dis(TOTAL_SERVERS / 3, r);
    test_dis(TOTAL_SERVERS / 3, o);


}