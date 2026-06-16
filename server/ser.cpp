#include "ser.h"

bool mysql_client::mysql_ConnectServer()
{
    MYSQL *mysql = mysql_init(&mysql_con);
    if (mysql == NULL)
    {
        return false;
    }
    mysql = mysql_real_connect(mysql, db_ips.c_str(), db_username.c_str(), db_passwd.c_str(), db_dbname.c_str(), 3306, NULL, 0);
    if (mysql == NULL)
    {
        cout << "connect db server err" << endl;
        return false;
    }
    return true;
}
bool mysql_client::mysql_Register(const string &tel, const string &passwd, const string &name)
{
    string sql = string("insert into user_info values(0,'") + tel + string("','") + name + string("','") + passwd + string("',1)");
    if (mysql_query(&mysql_con, sql.c_str()) != 0)
    {
        return false;
    }
    return true;
}

bool mysql_client::mysql_Login(const string &tel, const string &passwd, string &name)
{
    // select username,passwd from user_info where tel=13200000000
    string sql1 = string("select username,passwd from user_info where tel='") + tel + "'";
    if (mysql_query(&mysql_con, sql1.c_str()) != 0)
    {
        return false;
    }
    MYSQL_RES *r = mysql_store_result(&mysql_con); // 获取结果集
    if (r == NULL)
    {
        return false;
    }
    int num = mysql_num_rows(r); // 获取结果集中有多少行，0就是未查询到，则该用户未注册
    if (num == 0)
    {
        mysql_free_result(r);
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(r);

    string password = row[1];

    if (password.compare(passwd) != 0)
    {
        mysql_free_result(r);
        return false;
    }
    name = row[0];
    mysql_free_result(r);
    return true;
}
bool mysql_client::mysql_Show_Ticket(Json::Value &resval)
{ // select tk_id,addr,max,num,use_date from ticket_info;
    string sql = "select tk_id,addr,max,num,use_date from ticket_info";
    if (mysql_query(&mysql_con, sql.c_str()) != 0)
    {
        cout << "show ticket err" << endl;
        return false;
    }
    MYSQL_RES *r = mysql_store_result(&mysql_con);
    if (r == NULL)
    {
        return false;
    }
    int n = mysql_num_rows(r);
    if (n == 0)
    {
        resval["status"] = "OK";
        resval["num"] = 0;
        return true;
    }
    resval["status"] = "OK";
    resval["num"] = n;
    for (int i = 0; i < n; i++)
    {
        MYSQL_ROW row = mysql_fetch_row(r);
        Json::Value tmp;
        tmp["tk_id"] = row[0];
        tmp["addr"] = row[1];
        tmp["max"] = row[2];
        tmp["num"] = row[3];
        tmp["use_date"] = row[4];
        resval["arr"].append(tmp);
    }
    return true;
}

bool mysql_client::mysql_user_begin()
{
    if (mysql_query(&mysql_con, "begin") != 0)
    {
        return false;
    }
    return true;
}
bool mysql_client::mysql_user_commit()
{
    if (mysql_query(&mysql_con, "commit") != 0)
    {
        return false;
    }
    return true;
}
bool mysql_client::mysql_user_rollback()
{
    if (mysql_query(&mysql_con, "rollback") != 0)
    {
        return false;
    }
    return true;
}

bool mysql_client::mysql_Reserve_Ticket(int tk_id, string tel)
{
    mysql_user_begin(); // 启动事务
    string s1 = string("select max,num from ticket_info where tk_id=") + to_string(tk_id);
    if (mysql_query(&mysql_con, s1.c_str()) != 0)
    {
        cout << "查询max num失败" << endl;
        mysql_user_rollback();
        return false;
    }

    MYSQL_RES *r = mysql_store_result(&mysql_con);
    if (r == NULL)
    {
        cout << "获取结果集失败" << endl;
        mysql_user_rollback();
        return false;
    }
    int num = mysql_num_rows(r);
    if (num != 1)
    {
        cout << "记录行不唯一" << endl;
        mysql_user_rollback();
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(r);
    string str_max = row[0];
    string str_num = row[1];
    int tk_max = atoi(str_max.c_str());
    int tk_num = atoi(str_num.c_str());
    if (tk_max <= tk_num)
    {
        cout << "没有可用的票" << endl;
        mysql_user_rollback();
        return false;
    }
    tk_num++;
    string s2 = string("update ticket_info set num=") + to_string(tk_num) + string(" where tk_id=") + to_string(tk_id);
    if (mysql_query(&mysql_con, s2.c_str()) != 0)
    {
        cout << "修改票数失败" << endl;
        mysql_user_rollback();
        return false;
    }
    // 添加一行reserve_ticket;
    // insert into reserve_ticket values(0,1,'13300000000',now())
    string s3 = string("insert into reserve_ticket values(0,") + to_string(tk_id) + string(",'") + tel + string("',now())");
    if (mysql_query(&mysql_con, s3.c_str()) != 0)
    {
        cout << "存入预定信息失败" << endl;
        mysql_user_rollback();
        return false;
    }
    mysql_user_commit();
    return true;
}

bool mysql_client::mysql_MyReserve_Ticket(Json::Value &reserve)
{ // select addr,use_date from ticket_info,reserve_ticket where reserve_ticket.
    // tel="13100000000" and ticket_info.tk_id=reserve_ticket.tk_id;
    string tel = reserve["tel"].asString();
    string sql = string("select yd_id,addr,use_date from ticket_info,reserve_ticket where reserve_ticket.tel='") + tel + string("' and ticket_info.tk_id=reserve_ticket.tk_id");
    if (mysql_query(&mysql_con, sql.c_str()) != 0)
    {
        cout << "show ticket err" << endl;
        return false;
    }
    MYSQL_RES *r = mysql_store_result(&mysql_con);
    if (r == NULL)
    {
        return false;
    }
    int n = mysql_num_rows(r);
    if (n == 0)
    {
        reserve["status"] = "OK";
        reserve["num"] = 0;
        return true;
    }
    reserve["status"] = "OK";
    reserve["num"] = n;
    for (int i = 0; i < n; i++)
    {
        MYSQL_ROW row = mysql_fetch_row(r);
        Json::Value tmp;
        tmp["yd_id"] = row[0];
        tmp["addr"] = row[1];
        tmp["use_date"] = row[2];
        reserve["arr"].append(tmp);
    }
    return true;
}

bool mysql_client::User_Cancel_Reserve_Ticket(int yd_id, string tel)
{
 mysql_user_begin(); // 启动事务
    string s1 = string("select tk_id from reserve_ticket where yd_id=") + to_string(yd_id)+" and tel='"+tel+"'";
    if (mysql_query(&mysql_con, s1.c_str()) != 0)
    {
        cout << "查询tk_id失败" << endl;
        mysql_user_rollback();
        return false;
    }

    MYSQL_RES *r = mysql_store_result(&mysql_con);
    if (r == NULL||mysql_num_rows(r)!=1)
    {
        cout << "获取结果集失败or无此预约" << endl;
        mysql_free_result(r);
        mysql_user_rollback();
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(r);
    //string str_tk_id = row[0];
    int tk_id=atoi(row[0]);
    mysql_free_result(r);
    //删除预约
// 删除一行delete from reserve_ticket where yd_id=yd_id;
    string s2 = string("delete from reserve_ticket where yd_id=") +to_string(yd_id)+ " and tel='"+tel+"'";
    if (mysql_query(&mysql_con, s2.c_str()) != 0)
    {
        cout << "删除预约失败" << endl;
        mysql_user_rollback();
        return false;
    }
    //num-1

string s3 = string("update ticket_info set num=num-1 where tk_id=") + to_string(tk_id);
    if (mysql_query(&mysql_con, s3.c_str()) != 0)
    {
        cout << "修改已订票数失败" << endl;
        mysql_user_rollback();
        return false;
    }
    
    mysql_user_commit();
    return true;
}

// socket_listen
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
    send(c, res_val.toStyledString().c_str(), strlen(res_val.toStyledString().c_str()), 0);
}
void socket_con::Send_ok()
{
    Json::Value res_val;
    res_val["status"] = "OK";
    send(c, res_val.toStyledString().c_str(), strlen(res_val.toStyledString().c_str()), 0);
}
void socket_con::User_Register()
{
    string tel, passwd, username;
    tel = val["user_tel"].asString();
    passwd = val["user_passwd"].asString();
    username = val["user_name"].asString();
    if (tel.empty() || passwd.empty() || username.empty())
    {
        Send_err();
        return;
    }
    mysql_client cli;
    if (!cli.mysql_ConnectServer())
    {
        Send_err();
        return;
    }
    if (!cli.mysql_Register(tel, passwd, username))
    {
        Send_err();
        return;
    }

    Send_ok();
    return;
}
void socket_con::User_Login()
{
    string tel = val["user_tel"].asString();
    string passwd = val["user_passwd"].asString();
    string user_name;

    mysql_client cli;
    if (!cli.mysql_ConnectServer())
    {
        Send_err();
        return;
    }

    if (!cli.mysql_Login(tel, passwd, user_name))
    {
        Send_err();
        return;
    }
    Json::Value res_val;
    res_val["status"] = "OK";
    res_val["user_name"] = user_name;
    send(c, res_val.toStyledString().c_str(), strlen(res_val.toStyledString().c_str()), 0);
}
void socket_con::User_Show_Ticket()
{
    Json::Value resval;
    mysql_client cli;
    if (!cli.mysql_ConnectServer())
    {
        Send_err();
        return;
    }
    if (!cli.mysql_Show_Ticket(resval))
    {
        Send_err();
        return;
    }
    send(c, resval.toStyledString().c_str(), strlen(resval.toStyledString().c_str()), 0);
    return;
}

void socket_con::User_Reserve_Ticket()
{
    // client->tk_id,tel
    int tk_id = val["index"].asInt();
    string tel = val["tel"].asString();

    mysql_client cli;
    if (!cli.mysql_ConnectServer())
    {
        cout << "connect mysql err" << endl;
        Send_err();
        return;
    }

    if (!cli.mysql_Reserve_Ticket(tk_id, tel))
    {
        Send_err();
        return;
    }
    Send_ok();
    return;
}

void socket_con::User_MyReserve_Ticket()
{
    // client->
    string tel = val["tel"].asString();
    Json::Value reserve;
    reserve["tel"] = tel;

    mysql_client cli;
    if (!cli.mysql_ConnectServer())
    {
        Send_err();
        return;
    }
    if (!cli.mysql_MyReserve_Ticket(reserve))
    {
        Send_err();
        return;
    }
    send(c, reserve.toStyledString().c_str(), strlen(reserve.toStyledString().c_str()), 0);
    return;
}
void socket_con::User_Cancel_Reserve_Ticket()
{
    //  cli->yd_id,tel
    int yd_id = val["index"].asInt();
    string tel = val["tel"].asString();

    mysql_client cli;
    if (!cli.mysql_ConnectServer())
    {
        cout << "connect mysql err" << endl;
        Send_err();
        return;
    }

    if (!cli.User_Cancel_Reserve_Ticket(yd_id,tel))
    {
        Send_err();
        return;
    }
    Send_ok();
    return;
}

void socket_con::Recv_data()
{
    char buff[4096] = {0};
    int n = recv(c, buff, 4095, 0);
    if (n <= 0)
    {
        cout << "client close" << endl;
        delete this;
        return;
    }
    // 测试
    cout << "recv:" << buff << endl;

    Json::Reader Read;
    if (!Read.parse(buff, val))
    {
        cout << "Recv_data:解析json失败" << endl;
        Send_err();
        return;
    }
    int ops = val["type"].asInt();
    // Login=1,Register,View,Reserve,MyReserve,Cancel,Exit
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
        break;
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

    // 处理事件
    if (ev & EV_READ) // 当是读事件时才处理
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
        // 添加到libevent
        event_add(c_ev, NULL);
    }
}

int main()
{
    // 监听套接字
    socket_listen sock_ser;
    if (-1 == sock_ser.socket_init())
    {
        cout << "socket init err" << endl;
        exit(1);
    }
    // 创建libevent base
    struct event_base *base = event_init();
    if (base == NULL)
    {
        cout << "base null" << endl;
        exit(1);
    }
    // 设置socket_listen中的libevent的base
    sock_ser.Set_base(base);
    // 添加sockfd到libevent
    struct event *sock_ev = event_new(base, sock_ser.Get_sockfd(), EV_READ | EV_PERSIST, SOCK_LIS_CALLBACK, &sock_ser);
    event_add(sock_ev, NULL);
    // 启动事件循环
    event_base_dispatch(base);

    // 释放资源
    event_free(sock_ev);
    event_base_free(base);
}