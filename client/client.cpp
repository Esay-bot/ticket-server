#include "client.h"

bool socket_client::Connect_server()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd)
    {
        cout << "creat socket err" << endl;
        return false;
    }
    struct sockaddr_in saddr; // 服务器地址
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = inet_addr(ips.c_str());
    int res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (-1 == res)
    {
        cout << "connect ser err" << endl;
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
        cout << "1:查看预约  2:预定  3:查看我的预约  4:取消预约  5:退出" << endl;
        cout << "----------------------" << endl;
        cout << "请输入您的选择编号:" << endl;
        cin >> user_op;
        user_op += OFFSET;
    }
    else
    {
        cout << "---未登录----------游客---------" << endl;
        cout << "1:登录  2:注册  3:退出" << endl;
        cout << "----------------------" << endl;
        cout << "请输入您的选择编号:" << endl;
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
        cout << "密码不一致" << endl;
        return;
    }
    Json::Value val;
    val["type"] = Register;
    val["user_tel"] = usertel;
    val["user_name"] = username;
    val["user_passwd"] = passwd;

    send(sockfd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);

    char buff[256] = {0};
    if (recv(sockfd, buff, 255, 0) <= 0)
    {
        cout << "ser close" << endl;
        return;
    }
    val.clear();
    Json::Reader Read;
    if (!Read.parse(buff, val))
    {
        cout << "json 解析失败" << endl;
        return;
    }
    string s = val["status"].asString();
    if (s.compare("OK") != 0)
    {
        cout << "注册失败" << endl;
        return;
    }
    dl_flg = true;
    cout << "注册成功" << endl;
    return;
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

    send(sockfd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);

    char buff[255] = {0};
    int n = recv(sockfd, buff, 255, 0);
    if (n <= 0)
    {
        cout << "ser close" << endl;
        return;
    }
    val.clear();
    Json::Reader Read;
    if (!Read.parse(buff, val))
    {
        cout << "解析json失败" << endl;
        return;
    }
    string st = val["status"].asString();
    if (st.compare("OK") != 0)
    {
        cout << "登录失败" << endl;
        return;
    }
    dl_flg = true;
    username = val["user_name"].asString();
    usertel = tel;

    cout << "登录成功" << endl;
    return;
}

void socket_client::User_Show_Ticket()
{
    Json::Value val;
    val["type"] = View;
    send(sockfd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);
    // 接数据
    char buff[4096] = {0};
    int n = recv(sockfd, buff, 4095, 0);
    if (n <= 0)
    {
        cout << "ser close" << endl;
        return;
    }
    m_val.clear();
    Json::Reader Read;
    if (!Read.parse(buff, m_val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = m_val["status"].asString();
    if (st.compare("OK") != 0)
    {
        cout << "查询预约信息失败" << endl;
        return;
    }
    int num = m_val["num"].asInt();
    if (num == 0)
    {
        cout << "没有可预约信息" << endl;
        return;
    }
    cout << "编号   地点名称   总票数    已预定     时间" << endl;
    for (int i = 0; i < num; i++)
    {
        cout << "----------------------------------------------------" << endl;
        cout << "|" << m_val["arr"][i]["tk_id"].asString() << "   ";
        cout << m_val["arr"][i]["addr"].asString() << "   ";
        cout << m_val["arr"][i]["max"].asString() << "   ";
        cout << m_val["arr"][i]["num"].asString() << "   ";
        cout << m_val["arr"][i]["use_date"].asString() << "   |" << endl;
        cout << "----------------------------------------------------" << endl;
    }
    cout << endl;
}

void socket_client::User_Reserve_Ticket()
{
    User_Show_Ticket();
    cout << "请输入要预定票的编号" << endl;
    int index = 0;
    cin >> index;
    // index有效性检查
    //  Json::Reader Read;
    //  char buff[4096]={0};
    //  Read.parse(buff,m_val);
    int num = m_val["num"].asInt();
    if (index <= 0 || index > num)
    {
        cout << "选择编号错误" << endl;
    }
    Json::Value val;
    val["type"] = Reserve;
    val["tel"] = usertel;
    val["index"] = index;
    send(sockfd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);

    char buff[4096] = {0};
    int n = recv(sockfd, buff, 4095, 0);
    if (n <= 0)
    {
        cout << "ser close" << endl;
        return;
    }
    val.clear();
    Json::Reader Read;
    if (!Read.parse(buff, val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = val["status"].asString();
    if (st.compare("OK")!=0)
    {
        cout << "预定失败" << endl;
        return;
    }
    cout << "预定成功" << endl;
    return;
}

void socket_client::User_MyResere_Ticket()
{
    Json::Value val;
    val["type"] = MyReserve;
    val["tel"] = usertel;
    send(sockfd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);
    char buff[4096] = {0};
    int n = recv(sockfd, buff, 4095, 0);
    if (n <= 0)
    {
        cout << "ser close" << endl;
        return;
    }
    m_val.clear();
    Json::Reader Read;
    if (!Read.parse(buff, m_val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = m_val["status"].asString();
    if (st.compare("OK") != 0)
    {
        cout << "本次查询失败" << endl;
        return;
    }
    int num = m_val["num"].asInt();
    if (num == 0)
    {
        cout << "没有预约信息" << endl;
        return;
    }
    cout << "-----------------------------------------" << endl;
    cout << "|预定id号   地点    时间|" << endl;
    for (int i = 0; i < num; i++)
    {
         cout << "|" << m_val["arr"][i]["yd_id"].asString() << "  ";
        cout << "     " << m_val["arr"][i]["addr"].asString() << "  ";
        cout << "  |  " << m_val["arr"][i]["use_date"].asString() << "  |" << endl;
    }
    cout << "-----------------------------------------" << endl;
}
void socket_client::User_Cancel_Reserve_Ticket()
{
    User_MyResere_Ticket();
    cout<<"请输入要取消票的编号"<<endl;
    int index;
    cin>>index;
    Json::Value val;
    val["type"] = Cancel;
    val["tel"] = usertel;
    val["index"] = index;//用index和tel在reserve_ticket里删信息，再用前面查处的tk_id，把num-1;
    send(sockfd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);

    char buff[4096] = {0};
    int n = recv(sockfd, buff, 4095, 0);
    if (n <= 0)
    {
        cout << "ser close" << endl;
        return;
    }
    val.clear();
    Json::Reader Read;
    if (!Read.parse(buff, val))
    {
        cout << "json解析失败" << endl;
        return;
    }
    string st = val["status"].asString();
    if (st.compare("OK")!=0)
    {
        cout << "取消失败" << endl;
        return;
    }
    cout << "取消成功" << endl;
    return;

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
    exit(0);
}