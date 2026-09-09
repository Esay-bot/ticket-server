#include "client.h"

// 封包：生成 4字节网络序长度头 + json报文
string socket_client::encodePacket(const string& body)
{
    uint32_t netLen = htonl(static_cast<uint32_t>(body.size()));
    string pkg;
    pkg.append((char*)&netLen, sizeof(uint32_t));
    pkg += body;
    return pkg;
}

// 解包：先读4字节长度，再读取对应长度json
bool socket_client::recvPacket(string& outJson)
{
    // 设置5秒接收超时，防止卡死
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 第一步：读取4字节长度头
    uint32_t netLen = 0;
    ssize_t n = recv(sockfd, &netLen, sizeof(netLen), 0);
    if(n != sizeof(netLen))
    {
        cerr << "recv header failed, server disconnect or timeout" << endl;
        return false;
    }
    uint32_t bodyLen = ntohl(netLen);
    if(bodyLen == 0 || bodyLen > 4096)
    {
        cerr << "invalid body length" << endl;
        return false;
    }

    // 第二步：读取完整json正文
    char buf[4096] = {0};
    size_t recvTotal = 0;
    while(recvTotal < bodyLen)
    {
        ssize_t readn = recv(sockfd, buf + recvTotal, bodyLen - recvTotal, 0);
        if(readn <= 0)
        {
            cerr << "recv body failed" << endl;
            return false;
        }
        recvTotal += readn;
    }
    outJson.assign(buf, bodyLen);
    return true;
}

bool socket_client::Connect_server()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd)
    {
        cout << "creat socket err" << endl;
        return false;
    }
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = inet_addr(ips.c_str());
    int res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (-1 == res)
    {
        cout << "connect ser err: " << strerror(errno) << endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }
    cout << "connect to server success" << endl;
    return true;
}

void socket_client::print_info()
{
    if (dl_flg)
    {
        cout << "---已登录-----用户名:" << username << "-------" << endl;
        cout << "1:查看车票  2:预定  3:查看我的预约  4:取消预约  5:退出" << endl;
        cout << "----------------------" << endl;
        cin >> user_op;
        user_op += OFFSET;
    }
    else
    {
        cout << "---未登录----------游客---------" << endl;
        cout << "1:登录  2:注册  3:退出" << endl;
        cout << "----------------------" << endl;
        cin >> user_op;
        if (user_op == 3)
        {
            user_op = Exit;
        }
    }
}

void socket_client::User_Register()
{
    cout << "请输入用户手机号码" << endl;
    cin >> usertel;
    cout << "请输入用户名" << endl;
    cin >> username;
    string passwd, tmp;
    cout << "请输入密码" << endl;
    cin >> passwd;
    cout << "请再次输入密码" << endl;
    cin >> tmp;
    if (usertel.empty() || username.empty())
    {
        cout << "手机号或用户名不能为空" << endl;
        return;
    }
    if (passwd.compare(tmp) != 0)
    {
        cout << "两次输入密码不一致" << endl;
        return;
    }
    Json::Value val;
    val["type"] = Register;
    val["user_tel"] = usertel;
    val["user_name"] = username;
    val["user_passwd"] = passwd;

    // 封包发送
    string jsonStr = val.toStyledString();
    string sendPkg = encodePacket(jsonStr);
    send(sockfd, sendPkg.c_str(), sendPkg.size(), 0);

    // 接收响应
    string recvJson;
    if(!recvPacket(recvJson))
    {
        cout << "服务器断开连接" << endl;
        runing = false;
        return;
    }
    Json::Reader Read;
    if (!Read.parse(recvJson, val))
    {
        cout << "json 解析失败" << endl;
        return;
    }
    string s = val["status"].asString();
    if (s != "OK")
    {
        cout << "注册失败" << endl;
        return;
    }
    dl_flg = true;
    cout << "注册成功" << endl;
}

void socket_client::User_Login()
{
    string tel, passwd;
    cout << "请输入手机号" << endl;
    cin >> tel;
    cout << "请输入密码" << endl;
    cin >> passwd;
    if (tel.empty() || passwd.empty())
    {
        cout << "账号或密码不能为空" << endl;
        return;
    }
    Json::Value val;
    val["type"] = Login;
    val["user_tel"] = tel;
    val["user_passwd"] = passwd;

    // 封包发送
    string jsonStr = val.toStyledString();
    string sendPkg = encodePacket(jsonStr);
    send(sockfd, sendPkg.c_str(), sendPkg.size(), 0);

    // 接收响应
    string recvJson;
    if(!recvPacket(recvJson))
    {
        cout << "服务器断开连接" << endl;
        runing = false;
        return;
    }
    Json::Reader Read;
    if (!Read.parse(recvJson, val))
    {
        cout << "解析json失败" << endl;
        return;
    }
    string st = val["status"].asString();
    if (st != "OK")
    {
        cout << "登录失败" << endl;
        return;
    }
    dl_flg = true;
    username = val["user_name"].asString();
    usertel = tel;
    cout << "登录成功" << endl;
}

void socket_client::User_Show_Ticket()
{
    Json::Value val;
    val["type"] = View;
    string jsonStr = val.toStyledString();
    string sendPkg = encodePacket(jsonStr);
    send(sockfd, sendPkg.c_str(), sendPkg.size(), 0);

    string recvJson;
    if(!recvPacket(recvJson))
    {
        cout << "服务器断开连接" << endl;
        runing = false;
        return;
    }
    m_val.clear();
    Json::Reader Read;
    if (!Read.parse(recvJson, m_val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = m_val["status"].asString();
    if (st != "OK")
    {
        cout << "查询车票信息失败" << endl;
        return;
    }
    int num = m_val["num"].asInt();
    if (num == 0)
    {
        cout << "没有可预约车票" << endl;
        return;
    }
    cout << "编号\t出发到达\t总票数\t已预约\t日期" << endl;
    for (int i = 0; i < num; i++)
    {
        cout << m_val["arr"][i]["tk_id"].asString() << "\t"
             << m_val["arr"][i]["addr"].asString() << "\t"
             << m_val["arr"][i]["max"].asString() << "\t"
             << m_val["arr"][i]["num"].asString() << "\t"
             << m_val["arr"][i]["use_date"].asString() << endl;
    }
    cout << endl;
}

void socket_client::User_Reserve_Ticket()
{
    User_Show_Ticket();
    cout << "请输入要预定票的编号(tk_id)" << endl;
    int index = 0;
    cin >> index;
    int num = m_val["num"].asInt();
    if (index <= 0 || index > num)
    {
        cout << "选择编号错误" << endl;
        return;
    }
    Json::Value val;
    val["type"] = Reserve;
    val["tel"] = usertel;
    val["index"] = index;
    string jsonStr = val.toStyledString();
    string sendPkg = encodePacket(jsonStr);
    send(sockfd, sendPkg.c_str(), sendPkg.size(), 0);

    string recvJson;
    if(!recvPacket(recvJson))
    {
        cout << "服务器断开连接" << endl;
        runing = false;
        return;
    }
    val.clear();
    Json::Reader Read;
    if (!Read.parse(recvJson, val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = val["status"].asString();
    if (st != "OK")
    {
        cout << "预定失败" << endl;
        return;
    }
    cout << "预定成功" << endl;
}

void socket_client::User_MyResere_Ticket()
{
    Json::Value val;
    val["type"] = MyReserve;
    val["tel"] = usertel;
    string jsonStr = val.toStyledString();
    string sendPkg = encodePacket(jsonStr);
    send(sockfd, sendPkg.c_str(), sendPkg.size(), 0);

    string recvJson;
    if(!recvPacket(recvJson))
    {
        cout << "服务器断开连接" << endl;
        runing = false;
        return;
    }
    m_val.clear();
    Json::Reader Read;
    if (!Read.parse(recvJson, m_val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = m_val["status"].asString();
    if (st != "OK")
    {
        cout << "查询预约失败" << endl;
        return;
    }
    int num = m_val["num"].asInt();
    if (num == 0)
    {
        cout << "暂无预约信息" << endl;
        return;
    }
    cout << "预约ID\t线路\t日期" << endl;
    for (int i = 0; i < num; i++)
    {
        cout << m_val["arr"][i]["yd_id"].asString() << "\t"
             << m_val["arr"][i]["addr"].asString() << "\t"
             << m_val["arr"][i]["use_date"].asString() << endl;
    }
}

void socket_client::User_Cancel_Reserve_Ticket()
{
    User_MyResere_Ticket();
    cout<<"请输入要取消预约的yd_id编号"<<endl;
    int index;
    cin>>index;
    Json::Value val;
    val["type"] = Cancel;
    val["tel"] = usertel;
    val["index"] = index;
    string jsonStr = val.toStyledString();
    string sendPkg = encodePacket(jsonStr);
    send(sockfd, sendPkg.c_str(), sendPkg.size(), 0);

    string recvJson;
    if(!recvPacket(recvJson))
    {
        cout << "服务器断开连接" << endl;
        runing = false;
        return;
    }
    val.clear();
    Json::Reader Read;
    if (!Read.parse(recvJson, val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = val["status"].asString();
    if (st != "OK")
    {
        cout << "取消失败" << endl;
        return;
    }
    cout << "取消预约成功" << endl;
}

void socket_client::Run()
{
    while (runing)
    {
        print_info();
        switch (user_op)
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
            User_MyResere_Ticket();
            break;
        case Cancel:
            User_Cancel_Reserve_Ticket();
            break;
        case Exit:
            runing = false;
            cout << "客户端退出" << endl;
            break;
        default:
            cout << "输入无效" << endl;
            break;
        }
    }
}

int main()
{
    socket_client cli;
    if (!cli.Connect_server())
    {
        exit(1);
    }
    cli.Run();
    return 0;
}