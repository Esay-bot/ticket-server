#include <iostream>
#include <string.h>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event.h>
#include<jsoncpp/json/json.h>
using namespace std;
const int OFFSET=2;

            //登录     注册    查看预定 预定   查看我的预定 取消预定 退出 
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

    void print_info();

    ~socket_client()
    {
        close(sockfd);
    }

    bool Connect_server();
    void User_Register();
    void User_Login();
    void User_Show_Ticket();
    void User_Reserve_Ticket();
    //MyReserve,Cancel
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