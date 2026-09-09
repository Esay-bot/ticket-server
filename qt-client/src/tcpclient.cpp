#include "tcpclient.h"

#include <QJsonDocument>
#include <QDebug>

TcpClient::TcpClient(QObject *parent)
    : QObject(parent)
{
    connect(&socket_, &QAbstractSocket::connected, this, &TcpClient::onConnected);
    connect(&socket_, &QAbstractSocket::disconnected, this, &TcpClient::onDisconnected);
    connect(&socket_, &QIODevice::readyRead, this, &TcpClient::onReadyRead);
    // QTcpSocket::errorOccurred(QAbstractSocket::SocketError) 自 Qt 5.15 起可用
    connect(&socket_, &QAbstractSocket::errorOccurred, this, &TcpClient::onSocketError);

    connect(&codec_, &ProtocolCodec::packetReady, this, [this](const QJsonObject &obj) {
        qInfo("[net] <- %s",
              qPrintable(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))));
        unlock();
        emit jsonReceived(obj);
    });
    connect(&codec_, &ProtocolCodec::protocolError, this, [this](const QString &reason) {
        unlock();
        emit errorOccurred(reason);
    });

    timer_.setSingleShot(true);
    timer_.setInterval(kTimeoutMs);
    connect(&timer_, &QTimer::timeout, this, &TcpClient::onTimeout);
}

TcpClient::~TcpClient()
{
    // 析构顺序陷阱: QTcpSocket 关闭连接时会发出 disconnected/errorOccurred,
    // 若在成员销毁阶段触发本类槽函数, 将访问已析构的 timer_/codec_(未定义行为,
    // 实测在 model_test 收尾阶段段错误)。先停定时器, 再屏蔽 socket 信号后关闭。
    timer_.stop();
    socket_.blockSignals(true);
    socket_.abort();
}

void TcpClient::connectToHost(const QString &host, quint16 port)
{
    if (socket_.state() == QAbstractSocket::ConnectedState
        || socket_.state() == QAbstractSocket::ConnectingState) {
        return;
    }
    // 复用之前断线的 socket 对象前清理半包残留
    codec_.clear();
    socket_.abort();
    busy_ = false;
    timer_.stop();
    socket_.connectToHost(host, port);
}

void TcpClient::disconnectFromHost()
{
    timer_.stop();
    busy_ = false;
    socket_.disconnectFromHost();
}

bool TcpClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

bool TcpClient::send(const QJsonObject &obj)
{
    if (!isConnected()) {
        qWarning("[net] 发送失败: 未连接");
        return false;
    }
    if (busy_) {
        qWarning("[net] 发送失败: 上一请求尚未收到响应(串行锁)");
        return false;
    }

    const QByteArray packet = codec_.encode(obj);
    qInfo("[net] -> %s",
          qPrintable(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))));
    if (socket_.write(packet) != packet.size()) {
        emit errorOccurred(QStringLiteral("写入 socket 不完整"));
        return false;
    }

    busy_ = true;          // 上锁: 收到响应或超时前禁止再发
    timer_.start();        // 启动 5 秒响应超时
    return true;
}

void TcpClient::unlock()
{
    busy_ = false;
    timer_.stop();
}

void TcpClient::onReadyRead()
{
    // 一次 readyRead 不保证是一个完整包(可能是半包/粘包), 全部交给 codec 缓冲拆包
    codec_.feed(socket_.readAll());
}

void TcpClient::onConnected()
{
    qInfo("[net] 已连接服务器");
    emit connStateChanged(true);
}

void TcpClient::onDisconnected()
{
    qInfo("[net] 与服务器断开");
    unlock();
    emit connStateChanged(false);
}

void TcpClient::onSocketError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err);
    emit errorOccurred(socket_.errorString());
}

void TcpClient::onTimeout()
{
    // 5 秒未收到响应: 解锁并上报, 界面层据此提示
    busy_ = false;
    emit errorOccurred(QStringLiteral("服务器响应超时(%1 秒)").arg(kTimeoutMs / 1000));
}
