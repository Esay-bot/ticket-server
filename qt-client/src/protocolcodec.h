#ifndef PROTOCOLCODEC_H
#define PROTOCOLCODEC_H

#include <QObject>
#include <QByteArray>
#include <QJsonObject>

/*
 * 应用层协议编解码: 4 字节大端(网络序)长度头 + JSON 文本
 *
 * 与服务端 muduo 风格 Buffer(server/buffer.cpp) 的对应关系:
 *   encode()      ~= Buffer::appendInt32 + Buffer::append
 *   feed() 拆包   ~= TcpConnection::handleRead 里的循环:
 *                     peekInt32 读长度 -> 不够整包 break(半包)
 *                     -> retrieve 跳过长度头 -> retrieveAsString 取正文(粘包循环)
 *
 * 长度合法性: bodyLen 为 0 或 > 4096 视为对端数据错乱, 丢弃整个缓冲区并报错,
 * 保证不会死循环(与服务端旧客户端 recvPacket 的校验一致)。
 */
class ProtocolCodec : public QObject
{
    Q_OBJECT
public:
    static const int kHeaderSize = 4;   // 4 字节长度头
    static const int kMaxBodySize = 4096;

    explicit ProtocolCodec(QObject *parent = nullptr);

    // 封包: QJsonObject -> 4 字节大端长度 + JSON 文本
    // 服务端用 jsoncpp toStyledString(带缩进换行), 这里用 Compact——
    // 双方都只做 JSON 解析, 紧凑格式更短, 长度头按实际字节数计算即可。
    QByteArray encode(const QJsonObject &obj);

    // 解包入口: 追加到内部缓冲区, 循环取出所有完整包
    // 够一个完整包 -> emit packetReady(QJsonObject)   (粘包: 可能连发多次)
    // 不够整包     -> 保留在缓冲区等下次 feed          (半包)
    void feed(const QByteArray &bytes);

    // 当前缓冲区剩余未拆出的字节数(调试/测试用)
    int pendingBytes() const { return buf_.size(); }

    // 清空缓冲区(重连/换连接前调用, 避免半包残留串流)
    void clear() { buf_.clear(); }

signals:
    void packetReady(const QJsonObject &obj);
    void protocolError(const QString &reason);

private:
    // 从缓冲区头部窥视 4 字节大端长度(不移除数据)
    quint32 peekLength() const;

    QByteArray buf_;
};

#endif // PROTOCOLCODEC_H
