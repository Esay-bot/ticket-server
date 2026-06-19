#include "connection.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

TcpConnection::TcpConnection(int fd, struct event_base* base, MessageCallback cb)
    : fd_(fd), base_(base), readEv_(nullptr), msgCb_(cb), db_(std::make_unique<DBManager>())
{
    // 创建持久读事件
    readEv_ = event_new(base_, fd, EV_READ | EV_PERSIST, TcpConnection::readEventCallback, this);
}
TcpConnection::~TcpConnection() // 释放所有资源，防止fd、event、内存泄漏
{
    if (readEv_)
    {
        event_del(readEv_);  // 从事件循环移除
        event_free(readEv_); // 释放事件内存
    }
    if (fd_ > 0)
    {
        close(fd_);
    }
}
void TcpConnection::enableRead() // 将读事件注册到事件循环，开始监听客户端数据
{
    if (readEv_)
        event_add(readEv_, nullptr);
}
void TcpConnection::closeConn() // 关闭连接：先从全局管理器移除，再销毁自身
{
    ConnManager::getInstance().delConn(fd_);
    delete this;
}
void TcpConnection::sendResponse(const std::string &jsonResp)//统一发包函数，内部封装(4字节长度头+json)
{
    Buffer sendBuf;
    sendBuf.appendInt32(jsonResp.size());//写入网络序4字节长度
    sendBuf.append(jsonResp);//追加报文
    send(fd_, sendBuf.peek(), sendBuf.readableBytes(), 0);
}
void TcpConnection::readEventCallback(int fd,short ev,void *arg)//libevent静态读回调，转发到成员函数handleRead
{
    TcpConnection* conn=static_cast<TcpConnection*>(arg);
    if(ev& EV_READ)
    {
        conn->handleRead();
    }
}
void TcpConnection::handleRead()
{
    int saveErrno=0;
    ssize_t n=inputBuf_.readFd(fd_,&saveErrno);//从fd读数据存入当前连接的inputBuf缓冲区
    if(n<=0)//客户端关闭连接/读取出错，释放资源
    {
        std::cout<<"client fd:"<<fd_<<"disconnect"<<std::endl;
        closeConn();
        return;
    }
    while(inputBuf_.readableBytes()>=sizeof(int32_t))//循环拆报，处理粘宝，半包
    {
        //读取前4字节包长度
        int32_t bodyLen=inputBuf_.peekInt32();
        //缓冲区剩余数据不够一个包，跳出循环等待下次recv
        if(inputBuf_.readableBytes()<sizeof(int32_t)+bodyLen)
        break;
        //跳过4字节长度头
        inputBuf_.retrieve(sizeof(int32_t));
        //取出完整json报文
        std::string jsonStr=inputBuf_.retrieveAsString(bodyLen);
        std::cout<<"recv json:"<<jsonStr<<std::endl;
        msgCb_(this,jsonStr);//调用外部传入的业务回调，将报文交给ser处理登录/购票逻辑
    }

}