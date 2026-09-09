#include "protocolcodec.h"

#include <QtEndian>
#include <QJsonDocument>

ProtocolCodec::ProtocolCodec(QObject *parent)
    : QObject(parent)
{
}

QByteArray ProtocolCodec::encode(const QJsonObject &obj)
{
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    quint32 beLen = qToBigEndian<quint32>(quint32(body.size()));
    QByteArray packet(reinterpret_cast<const char *>(&beLen), kHeaderSize);
    packet.append(body);
    return packet;
}

quint32 ProtocolCodec::peekLength() const
{
    // 本机为小端, 按 4 个字节手工组装大端值; 等价于 memcpy 后 ntohl
    return (quint32(quint8(buf_[0])) << 24)
         | (quint32(quint8(buf_[1])) << 16)
         | (quint32(quint8(buf_[2])) << 8)
         |  quint32(quint8(buf_[3]));
}

void ProtocolCodec::feed(const QByteArray &bytes)
{
    buf_.append(bytes);

    // 循环拆包: 一次 feed 里可能含多个完整包(粘包)
    while (buf_.size() >= kHeaderSize) {
        const quint32 bodyLen = peekLength();

        // 长度非法: 丢弃缓冲并报错, 防止按错误长度继续切割造成死循环
        if (bodyLen == 0 || bodyLen > quint32(kMaxBodySize)) {
            buf_.clear();
            emit protocolError(QStringLiteral("非法包长 %1, 已丢弃缓冲区").arg(bodyLen));
            return;
        }

        // 半包: 连长度+正文都没收全, 等待下一次 readyRead 再 feed
        if (buf_.size() < kHeaderSize + int(bodyLen))
            return;

        // 取出正文, 移除"长度头+正文"(本工程流量很小, 直接前移即可)
        const QByteArray body = buf_.mid(kHeaderSize, int(bodyLen));
        buf_.remove(0, kHeaderSize + int(bodyLen));

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
        if (doc.isNull() || !doc.isObject()) {
            // 帧完整但 JSON 非法: 报错后继续处理后续帧, 不影响拆包节奏
            emit protocolError(QStringLiteral("JSON 解析失败: %1").arg(parseErr.errorString()));
            continue;
        }
        emit packetReady(doc.object());
    }
}
