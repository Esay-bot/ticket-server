#include "ser.h"
#include <memory>

void businessMessageCallback(TcpConnection* conn, const std::string& jsonStr)
{
    Json::Reader rd;
    Json::Value val;
    if (!rd.parse(jsonStr, val))
    {
        Json::Value err;
        err["status"] = "ERR";
        conn->sendResponse(err.toStyledString());
        return;
    }

    int op = val["type"].asInt();
    DBManager* db = conn->getDB();
    if (!db->connect())
    {
        Json::Value err;
        err["status"] = "ERR";
        conn->sendResponse(err.toStyledString());
        return;
    }

    Json::Value resp;
    switch (op)
    {
    case Login:
    {
        string tel = val["user_tel"].asString();
        string pwd = val["user_passwd"].asString();
        string name;
        if (db->userLogin(tel, pwd, name))
        {
            resp["status"] = "OK";
            resp["user_name"] = name;
        }
        else
            resp["status"] = "ERR";
        break;
    }
    case Register:
    {
        string tel = val["user_tel"].asString();
        string pwd = val["user_passwd"].asString();
        string name = val["user_name"].asString();
        if (db->userRegister(tel, pwd, name))
            resp["status"] = "OK";
        else
            resp["status"] = "ERR";
        break;
    }
    case View:
    {
        db->showTickets(resp);
        break;
    }
    case Reserve:
    {
        int tkId = val["index"].asInt();
        string tel = val["tel"].asString();
        if (db->reserveTicket(tkId, tel))
            resp["status"] = "OK";
        else
            resp["status"] = "ERR";
        break;
    }
    case MyReserve:
    {
        string tel = val["tel"].asString();
        db->getMyReservedTickets(tel, resp);
        break;
    }
    case Cancel:
    {
        int ydId = val["index"].asInt();
        string tel = val["tel"].asString();
        if (db->cancelReservedTicket(ydId, tel))
            resp["status"] = "OK";
        else
            resp["status"] = "ERR";
        break;
    }
    default:
        resp["status"] = "ERR";
        break;
    }
    conn->sendResponse(resp.toStyledString());
}

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

void SOCK_LIS_CALLBACK(int sockfd, short ev, void *arg)
{
    socket_listen *p = (socket_listen *)arg;
    if (!(ev & EV_READ)) return;

    int cfd = p->accept_client();
    if (cfd < 0) return;
    cout << "new client fd = " << cfd << endl;

    auto newConn = make_unique<TcpConnection>(cfd, p->Get_base(), businessMessageCallback);
    newConn->enableRead();
    ConnManager::getInstance().addConn(cfd, move(newConn));
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