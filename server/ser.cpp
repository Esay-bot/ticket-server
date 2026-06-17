#include "ser.h"
#include <memory>

bool socket_listen::socket_init()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd)
        return false;

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(m_port);
    saddr.sin_addr.s_addr = inet_addr(m_ips.c_str());

    int res = bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (-1 == res)
    {
        cout << "bind err" << endl;
        close(sockfd);
        return false;
    }
    res = listen(sockfd, LIS_MAX);
    if (-1 == res)
        return false;

    return true;
}

int socket_listen::accept_client()
{
    int c = accept(sockfd, NULL, NULL);
    return c;
}

//---socket_con
void socket_con::Send_err()
{
    Json::Value res_val;
    res_val["status"] = "ERR";
    string jsonResp = res_val.toStyledString();

    // Buffer封装协议：4字节长度 + json内容
    Buffer sendBuf;
    sendBuf.appendInt32(jsonResp.size());
    sendBuf.append(jsonResp);

    // 一次性发送
    send(c, sendBuf.peek(), sendBuf.readableBytes(), 0);
}

void socket_con::Send_ok()
{
    Json::Value res_val;
    res_val["status"] = "OK";
    string jsonResp = res_val.toStyledString();

    // 使用Buffer封装协议：4字节长度 + json内容
    Buffer sendBuf;
    sendBuf.appendInt32(jsonResp.size());
    sendBuf.append(jsonResp);

    // 一次性发送
    send(c, sendBuf.peek(), sendBuf.readableBytes(), 0);
}

void socket_con::User_Register()
{
    string tel = val["user_tel"].asString();
    string passwd = val["user_passwd"].asString();
    string username = val["user_name"].asString();

    if (tel.empty() || passwd.empty() || username.empty())
    {
        Send_err();
        return;
    }

    // 连接数据库并注册
    if (!db_manager_->connect())
    {
        cerr << "db connect failed" << endl;
        Send_err();
        return;
    }

    if (!db_manager_->userRegister(tel, passwd, username))
    {
        Send_err();
        return;
    }

    Send_ok();
}

void socket_con::User_Login()
{
    string tel = val["user_tel"].asString();
    string passwd = val["user_passwd"].asString();
    string user_name;

    if (!db_manager_->connect())
    {
        cerr << "db connect failed" << endl;
        Send_err();
        return;
    }

    if (!db_manager_->userLogin(tel, passwd, user_name))
    {
        Send_err();
        return;
    }

    Json::Value res_val;
    res_val["status"] = "OK";
    res_val["user_name"] = user_name;
    string resp = res_val.toStyledString();
    send(c, resp.c_str(), resp.size(), 0);
}

void socket_con::User_Show_Ticket()
{
    Json::Value resval;

    if (!db_manager_->connect())
    {
        cerr << "db connect failed" << endl;
        Send_err();
        return;
    }

    if (!db_manager_->showTickets(resval))
    {
        Send_err();
        return;
    }

    string resp = resval.toStyledString();
    send(c, resp.c_str(), resp.size(), 0);
}

void socket_con::User_Reserve_Ticket()
{
    int tk_id = val["index"].asInt();
    string tel = val["tel"].asString();

    if (!db_manager_->connect())
    {
        cerr << "connect mysql err" << endl;
        Send_err();
        return;
    }

    if (!db_manager_->reserveTicket(tk_id, tel))
    {
        Send_err();
        return;
    }

    Send_ok();
}

void socket_con::User_MyReserve_Ticket()
{
    string tel = val["tel"].asString();
    Json::Value reserve;

    if (!db_manager_->connect())
    {
        Send_err();
        return;
    }

    if (!db_manager_->getMyReservedTickets(tel, reserve))
    {
        Send_err();
        return;
    }

    string resp = reserve.toStyledString();
    send(c, resp.c_str(), resp.size(), 0);
}

void socket_con::User_Cancel_Reserve_Ticket()
{
    int yd_id = val["index"].asInt();
    string tel = val["tel"].asString();

    if (!db_manager_->connect())
    {
        cerr << "connect mysql err" << endl;
        Send_err();
        return;
    }

    if (!db_manager_->cancelReservedTicket(yd_id, tel))
    {
        Send_err();
        return;
    }

    Send_ok();
}

void socket_con::Recv_data()
{
    int saveErrno = 0;
    // 从fd读取数据存入Buffer
    ssize_t n = inputBuf_.readFd(c, &saveErrno);
    if (n <= 0)
    {
        cout << "client close or read error, fd=" << c << endl;
        delete this;
        return;
    }

    // 循环拆包：4字节长度头 + JSON消息体
    while (inputBuf_.readableBytes() >= sizeof(int32_t))
    {
        // 读取网络序长度头
        int32_t bodyLen = inputBuf_.peekInt32();
        // 数据不足一个完整包，半包跳出循环
        if (inputBuf_.readableBytes() < sizeof(int32_t) + bodyLen)
        {
            break;
        }

        // 跳过4字节长度头
        inputBuf_.retrieve(sizeof(int32_t));
        // 取出完整JSON报文
        string jsonStr = inputBuf_.retrieveAsString(bodyLen);
        cout << "recv complete json: " << jsonStr << endl;

        Json::Reader Read;
        if (!Read.parse(jsonStr, val))
        {
            cout << "json parse failed" << endl;
            Send_err();
            continue;
        }

        int ops = val["type"].asInt();
        switch (ops)
        {
        case Login:
            User_Login();
            break;
        case Register:
            User_Register();
            break;
        case View:
            User_Show_Ticket();
            break;
        case Reserve:
            User_Reserve_Ticket();
            break;
        case MyReserve:
            User_MyReserve_Ticket();
            break;
        case Cancel:
            User_Cancel_Reserve_Ticket();
            break;
        case Exit:
            break;
        default:
            Send_err();
            break;
        }
    }
}

// callback
void SOCK_CON_CALLBACK(int fd, short ev, void *arg)
{
    socket_con *q = (socket_con *)arg;
    if (ev & EV_READ)
    {
        q->Recv_data();
    }
}

void SOCK_LIS_CALLBACK(int sockfd, short ev, void *arg)
{
    socket_listen *p = (socket_listen *)arg;
    if (p == NULL)
        return;

    if (ev & EV_READ)
    {
        int c = p->accept_client();
        if (c == -1)
            return;

        cout << "accept : c=" << c << endl;

        socket_con *q = new socket_con(c);
        struct event *c_ev = event_new(p->Get_base(), c, EV_READ | EV_PERSIST, SOCK_CON_CALLBACK, q);

        if (c_ev == NULL)
        {
            close(c);
            delete q;
            return;
        }

        q->Set_ev(c_ev);
        event_add(c_ev, NULL);
    }
}

int main()
{
    socket_listen sock_ser;
    if (!sock_ser.socket_init())
    {
        cout << "socket init err" << endl;
        exit(1);
    }

    struct event_base *base = event_init();
    if (base == NULL)
    {
        cout << "base null" << endl;
        exit(1);
    }

    sock_ser.Set_base(base);
    struct event *sock_ev = event_new(base, sock_ser.Get_sockfd(), EV_READ | EV_PERSIST, SOCK_LIS_CALLBACK, &sock_ser);
    event_add(sock_ev, NULL);

    event_base_dispatch(base);

    event_free(sock_ev);
    event_base_free(base);

    return 0;
}