#ifndef SER_H
#define SER_H
#include <iostream>
#include <string.h>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event.h>
#include <memory>
#include <jsoncpp/json/json.h>
#include "db_manager.h"
#include "buffer.h"
#include "connection.h"
#include "threadpool.h"


using namespace std;
const int LIS_MAX = 10;


class socket_listen
{
private:
    int sockfd;
    short m_port;
    string m_ips;
    struct event_base *base;
    Json::Value val;

public:
    socket_listen()
    {
        sockfd = -1;
        m_port = 6000;
        m_ips = "127.0.0.1";
    }
    socket_listen(string ips, short port) : m_port(port), m_ips(ips)
    {
        sockfd = -1;
    }
    bool socket_init();
    int accept_client(); // 返回连接套接字
    void Set_base(struct event_base *base)
    {
        this->base = base;
    }

    int Get_sockfd() const
    {
        return sockfd;
    }
    struct event_base *Get_base() const
    {
        return base;
    }
};
#endif