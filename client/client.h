#include <iostream>
#include <string.h>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jsoncpp/json/json.h>
#include <errno.h>
using namespace std;

const int OFFSET=2;
// 和服务端统一操作枚举
enum OP_TYPE {Login=1,Register,View,Reserve,MyReserve,Cancel,Exit};

class socket_client
{
public:
    socket_client()
    {
        sockfd = -1;
        ips = "127.0.0.1";
        port = 6000;
        dl_flg = false;
        user_op = 0;
        runing=true;
    };
    socket_client(string ips, short port)
    {
        sockfd = -1;
        this->ips = ips;
        this->port = port;
        dl_flg = false;
        user_op = 0;
        runing=true;
    }
    ~socket_client()
    {
        if(sockfd > 0) close(sockfd);
    }

    // 协议封包：4字节大端长度 + json正文
    string encodePacket(const string& body);
    // 协议解包：读取完整数据包，返回json字符串
    bool recvPacket(string& outJson);

    void print_info();
    bool Connect_server();
    void User_Register();
    void User_Login();
    void User_Show_Ticket();
    void User_Reserve_Ticket();
    void User_MyResere_Ticket();
    void User_Cancel_Reserve_Ticket();
    void Run();
private:
    string ips;
    short port;
    int sockfd;
    bool dl_flg;
    string username;
    string usertel;
    int user_op;
    bool runing;
    Json::Value m_val;
};