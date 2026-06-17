#include <iostream>
#include <string.h>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event.h>
#include <jsoncpp/json/json.h>
#include "db_manager.h"
#include "buffer.h"
#include <memory>

using namespace std;
const int LIS_MAX = 10;

// 登录     注册    查看预定 预定   查看我的预定 取消预定 退出
enum OP_TYPE
{
    Login = 1,
    Register,
    View,
    Reserve,
    MyReserve,
    Cancel,
    Exit
};

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

/// @brief
class socket_con
{
public:
    socket_con(int fd) : c(fd)
    {
        c_ev = NULL;
        // 初始化数据库管理器
        db_manager_ = std::make_unique<DBManager>();
    }
    void Set_ev(struct event *ev)
    {
        c_ev = ev;
    }
    ~socket_con()
    {
        if (c_ev)
        {
            event_free(c_ev);
        }
        close(c);
    }
    void Recv_data();
    void Send_err();
    void Send_ok();

    void User_Register();
    void User_Login();
    void User_Show_Ticket();           // 查看预约信息
    void User_Reserve_Ticket();        // 预定
    void User_MyReserve_Ticket();      // 查看我的预约
    void User_Cancel_Reserve_Ticket(); // 取消我的预约

private:
    int c;
    struct event *c_ev;
    Json::Value val;
    std::unique_ptr<DBManager> db_manager_;
    // 新增：每个连接独立缓冲区
    Buffer inputBuf_;
};