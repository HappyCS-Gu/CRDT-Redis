//
// Created by user on 18-11-15.
//
#include <sys/stat.h>
#include "queue_log.h"
#include "constants.h"


void queue_log::shift_up(int s)
{
    int j = s, i = (j - 1) / 2;
    auto temp = heap[j];
    while (j > 0)
    {
        if (*heap[i] >= *temp)break;
        else
        {
            heap[j] = heap[i];
            heap[j]->index = j;
            j = i;
            i = (i - 1) / 2;
        }
    }
    temp->index = j;
    heap[j] = temp;
}

void queue_log::shift_down(int s)
{
    int i = s, j = 2 * i + 1, tail = static_cast<int>(heap.size() - 1);
    auto temp = heap[i];
    while (j <= tail)
    {
        if (j < tail && *heap[j] <= *heap[j + 1])j++;
        if (*temp >= *heap[j])break;
        else
        {
            heap[i] = heap[j];
            heap[i]->index = i;
            i = j;
            j = i * 2 + 1;
        }
    }
    temp->index = i;
    heap[i] = temp;
}

void queue_log::add(int k, double v)
{
    mtx.lock();
    if (map.find(k) == map.end())
    {
        auto e = new element(k, v);
        map[k] = e;
        heap.push_back(e);
        shift_up(static_cast<int>(heap.size() - 1));
    }
    mtx.unlock();
}

void queue_log::inc(int k, double i)
{
    if (i == 0)return;
    lock_guard<mutex> lk(mtx);
    if (map.find(k) != map.end())
    {
        auto e = map[k];
        e->value += i;
        if (i > 0)
            shift_up(e->index);
        else
            shift_down(e->index);
    }
}

void queue_log::rem(int k)
{
    lock_guard<mutex> lk(mtx);
    if (map.find(k) != map.end())
    {
        auto e = map[k];
        map.erase(map.find(k));
        heap[e->index] = heap.back();
        heap.erase(heap.end() - 1);
        shift_down(e->index);
        delete e;
    }
}

void queue_log::max(int k, double v)
{
    int ak;
    double av;

    {
        lock_guard<mutex> lk(mtx);
        if (!heap.empty())
        {
            ak = heap[0]->name;
            av = heap[0]->value;
        }
        else
        {
            ak = -1;
            av = -1;
        }
    }

    {
        lock_guard<mutex> lk(max_mtx);
        max_log.emplace_back(k, v, ak, av);
    }
}

void queue_log::overhead(int o)
{
    lock_guard<mutex> lk(ovhd_mtx);
    overhead_log.emplace_back(heap.size(), o);
}

void queue_log::write_file(char type)
{
    char n[128],f[256];
    sprintf(n,"../result/%c:%d,%d,(%d,%d)",type,TOTAL_SERVERS,OP_PER_SEC,DELAY,DELAY_LOW);
    mkdir(n,S_IRWXU|S_IRGRP|S_IROTH);

    sprintf(f, "%s/s.ovhd", n);
    FILE *ovhd = fopen(f, "w");
    for (auto o:overhead_log)
    {
        fprintf(ovhd, "%d %d\n", o.num, o.ovhd);
    }
    fflush(ovhd);
    fclose(ovhd);

    sprintf(f, "%s/s.max", n);
    FILE *max = fopen(f, "w");
    for (auto o:max_log)
    {
        fprintf(ovhd, "%d %f %d %f\n", o.kread, o.vread, o.kactural, o.vactural);
    }
    fflush(max);
    fclose(max);
}
