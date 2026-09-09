#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>
#include "protocolcodec.h"

/*
 * 网络层封装: 持有一个 QTcpSocket(组合而非继承), 信号驱动, 无需手写收发线程
 *
 * 串行请求(同一时刻只允许一个未完成请求):
 *   服务端是"一问一答"且响应不回显请求 type —— 收到响应前无法知道它属于
 *   哪个请求, 若并发发出两个请求, 响应就无法配对。故 send() 后置 busy_,
 *   收到响应(jsonReceived)或超时后解锁, 期间 send() 直接返回 false。
 *
 * 5 秒响应超时用 QTimer 单发实现, 等价旧客户端的 SO_RCVTIMEO。
 */
class TcpClient : public QObject
{
    Q_OBJECT
public:
    static const int kTimeoutMs = 5000;   // 与旧客户端 SO_RCVTIMEO 对齐

    explicit TcpClient(QObject *parent = nullptr);

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();

    bool isConnected() const;
    bool isBusy() const { return busy_; }        // 是否有未完成请求

    // 发送一个请求; 未连接或上一请求未完成时返回 false(不发送)
    bool send(const QJsonObject &obj);

signals:
    void jsonReceived(const QJsonObject &obj);   // 收到完整响应(已解锁)
    void connStateChanged(bool up);              // 连接建立/断开
    void errorOccurred(const QString &reason);   // 超时/协议错/socket 错的统一出口

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError err);
    void onTimeout();

private:
    void unlock();   // 响应到达/超时/出错时解除串行锁

    QTcpSocket socket_;
    ProtocolCodec codec_;
    QTimer timer_;    // 响应超时定时器(单发)
    bool busy_ = false;
};

#endif // TCPCLIENT_H
