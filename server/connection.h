#ifndef CONNECTION_H
#define CONNECTION_H

#include "buffer.h"
#include "db_manager.h"
#include <event.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <jsoncpp/json/json.h>

// 前置声明
class TcpConnection;

// 业务回调函数类型：收到完整数据包后执行业务处理
using MessageCallback = void (*)(TcpConnection *conn, const std::string &jsonData);
class TcpConnection
{
public:
    TcpConnection(int fd, struct event_base *base, MessageCallback cb);
    ~TcpConnection();
    void enableRead();                              // 添加读事件到libevent,开始监听客户端数据
    void closeConn();                               // 关闭连接，释放资源从全局管理器移除
    void sendResponse(const std::string &jsonResp); // 给客户端的数据加上4字节长度头，好直接发
    // 获取缓冲区，数据库，fd接口
    Buffer &getInputBuf() { return inputBuf_; }
    DBManager *getDB() { return db_.get(); }
    int getFd() { return fd_; }

private:
    static void readEventCallback(int fd, short ev, void *arg); // libevent静态回调
    void handleRead();                                          // 内部读数据，拆包
private:
    int fd_;                        // 客户端套接字
    struct event_base *base_;       // 所属事件循环
    struct event *readEv_;          // 读事件对象
    MessageCallback msgCb_;         // 业务回调函数指针(自带当前连接对象和json字符串)
    Buffer inputBuf_;               // 接收缓冲区
    std::unique_ptr<DBManager> db_; // 连接独立数据库实例
    Json::Value jsonVal_;           // 临时json存储
};

// 全局连接管理器：保存所有在线客户端，方便统一管理
//单例模式
class ConnManager
{
    public:
    static ConnManager& getInstance()//获取单例实例
    {
        static ConnManager ins;
        return ins;
    }
    void addConn(int fd,std::unique_ptr<TcpConnection> conn)//新增连接：fd为key，智能指针存连接对象
    {
        connMap_[fd]=std::move(conn);
    }
    void delConn(int fd)//删除连接(客户端断开连接)
    {
        connMap_.erase(fd);
    }
    TcpConnection* getConn(int fd)//根据fd查找连接
    {
        if(connMap_.count(fd))
        {
            return connMap_[fd].get();
        }
        return nullptr;
    }
    private:
    ConnManager()=default;//私有构造，禁止外部创建实例，保证单例
    std::unordered_map<int,std::unique_ptr<TcpConnection>> connMap_;
    //fd->TcpConnection 映射表，统一管理所有连接
};
#endif