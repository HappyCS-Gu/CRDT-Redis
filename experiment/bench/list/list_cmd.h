//
// Created by admin on 2020/7/16.
//

#ifndef BENCH_LIST_CMD_H
#define BENCH_LIST_CMD_H

#include "../util.h"
#include "list_basics.h"
#include "list_log.h"
#include <string>

class list_cmd : public cmd
{
protected:
    list_type type;
    list_log &list;
    char cmd_head[64]{};

    list_cmd(list_type type, list_log &list, const char *op) : type(type), list(list)
    {
        const char *type_str = list_type_str[static_cast<int>(type)];
        sprintf(cmd_head, "%sl%s %slist", type_str, op, type_str);
    }
};

class list_add_cmd : public list_cmd
{
private:
    string prev, id, content;
    int font, size, color;
    bool bold, italic, underline;
public:
    list_add_cmd(list_type type, list_log &list, string &prev, string &id, string &content,
                 int font, int size, int color, bool bold, bool italic, bool underline) :
            list_cmd(type, list, "insert"), prev(prev), id(id), content(content),
            font(font), size(size), color(color), bold(bold), italic(italic), underline(underline) {}

    void exec(redis_client &c) override
    {
        int property = 0;
        if (bold) property |= BOLD;    //NOLINT
        if (italic) property |= ITALIC;    //NOLINT
        if (underline) property |= UNDERLINE;    //NOLINT
        char cmd[256];
        sprintf(cmd, "%s %s %s %s %d %d %d %d",
                cmd_head, prev.c_str(), id.c_str(), content.c_str(),
                font, size, color, property);
        auto r = c.exec(cmd);
        list.insert(prev, id, content, font, size, color, bold, italic, underline);
    }
};

class list_update_cmd : public list_cmd
{
private:
    string id, upd_type;
    int value;
public:
    list_update_cmd(list_type type, list_log &list, string &id, string &upd_type, int value) :
            list_cmd(type, list, "update"), id(id), upd_type(upd_type), value(value) {}

    void exec(redis_client &c) override
    {
        char cmd[128];
        sprintf(cmd, "%s %s %s %d", cmd_head, id.c_str(), upd_type.c_str(), value);
        auto r = c.exec(cmd);
        list.update(id, upd_type, value);
    }
};

class list_remove_cmd : public list_cmd
{
private:
    string id;
public:
    list_remove_cmd(list_type type, list_log &list, string &id) :
            list_cmd(type, list, "rem"), id(id) {}

    void exec(redis_client &c) override
    {
        char cmd[128];
        sprintf(cmd, "%s %s", cmd_head, id.c_str());
        auto r = c.exec(cmd);
        list.remove(id);
    }
};

class list_read_cmd : public list_cmd
{
public:
    list_read_cmd(list_type type, list_log &list) : list_cmd(type, list, "list") {}

    void exec(redis_client &c) override
    {
        auto r = c.exec(cmd_head);
        list.read_list(r);
    }
};

class list_ovhd_cmd : public list_cmd
{
public:
    list_ovhd_cmd(list_type type, list_log &list) : list_cmd(type, list, "overhead") {}

    void exec(redis_client &c) override
    {
        auto r = c.exec(cmd_head);
        list.overhead(static_cast<int>(r->integer));
    }
};

class list_opcount_cmd : public list_cmd
{
public:
    list_opcount_cmd(list_type type, list_log &list) : list_cmd(type, list, "opcount") {}

    void exec(redis_client &c) override
    {
        auto r = c.exec(cmd_head);
        printf("%lli\n", r->integer);
    }
};

#endif //BENCH_LIST_CMD_H