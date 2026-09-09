/*
 * ProtocolCodec 单元测试
 * 覆盖计划 M1 验收要求的三组数据:
 *   1. 半包: 一个包分两次 feed(含"长度头本身被截断"的情形)
 *   2. 粘包: 两个包拼在一起一次 feed
 *   3. 非法长度: 0 与 >4096, 丢弃缓冲并报错, 不死循环
 * 另加: 大端编码格式逐字节校验 / 合法帧但 JSON 非法的容错
 *
 * 同线程直连信号在 emit 处同步回调, 无需事件循环即可断言。
 */
#include "protocolcodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <QDebug>
#include <QVector>
#include <QStringList>
#include <cstdlib>

static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { qDebug("[PASS] %s", msg); } \
        else { qDebug("[FAIL] %s  (%s:%d)", msg, __FILE__, __LINE__); ++g_failed; } \
    } while (0)

struct Sink
{
    QVector<QJsonObject> packets;
    QStringList errors;
};

static void wire(ProtocolCodec &c, Sink &s)
{
    QObject::connect(&c, &ProtocolCodec::packetReady, [&s](const QJsonObject &o) {
        s.packets.append(o);
    });
    QObject::connect(&c, &ProtocolCodec::protocolError, [&s](const QString &r) {
        s.errors.append(r);
    });
}

// 构造一个固定字段的对象, 便于跨 encode/feed 对比
static QJsonObject sample(const QString &tag)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), 3);
    o.insert(QStringLiteral("tag"), tag);
    QJsonObject nested;
    nested.insert(QStringLiteral("n"), 42);
    o.insert(QStringLiteral("nested"), nested);
    return o;
}

static void testEncodeBigEndian()
{
    ProtocolCodec c;
    const QByteArray pkt = c.encode(sample(QStringLiteral("fmt")));

    // 长度头 4 字节必须是大端: 高位字节在前
    const int bodyLen = pkt.size() - ProtocolCodec::kHeaderSize;
    CHECK(quint8(pkt[0]) == quint8((bodyLen >> 24) & 0xff)
          && quint8(pkt[1]) == quint8((bodyLen >> 16) & 0xff)
          && quint8(pkt[2]) == quint8((bodyLen >> 8) & 0xff)
          && quint8(pkt[3]) == quint8(bodyLen & 0xff),
          "编码: 长度头为 4 字节大端, 值=正文字节数");

    // 正文必须是合法 JSON 文本(Compact, 无换行)
    const QByteArray body = pkt.mid(ProtocolCodec::kHeaderSize);
    CHECK(!body.contains('\n') && QJsonDocument::fromJson(body).isObject(),
          "编码: 正文为紧凑 JSON 文本");
}

static void testRoundTrip()
{
    ProtocolCodec c;
    Sink s;
    wire(c, s);
    c.feed(c.encode(sample(QStringLiteral("rt"))));
    CHECK(s.packets.size() == 1 && s.errors.isEmpty(), "整包一次 feed: 收到 1 包, 无错误");
    CHECK(s.packets.at(0).value(QStringLiteral("tag")).toString() == QStringLiteral("rt")
          && s.packets.at(0).value(QStringLiteral("nested")).toObject().value(QStringLiteral("n")).toInt() == 42,
          "整包一次 feed: 字段(含嵌套对象/数值类型)完整还原");
    CHECK(c.pendingBytes() == 0, "整包一次 feed: 缓冲区清空");
}

static void testHalfPacket()
{
    // 半包主体: 长度头完整, 正文只有前半
    {
        ProtocolCodec c;
        Sink s;
        wire(c, s);
        const QByteArray pkt = c.encode(sample(QStringLiteral("half")));
        c.feed(pkt.left(4 + pkt.size() / 3));   // 头 + 1/3 正文
        CHECK(s.packets.isEmpty() && s.errors.isEmpty(),
              "半包: 第一次 feed(头+部分正文) 不应产出包");
        c.feed(pkt.mid(4 + pkt.size() / 3));    // 剩余正文
        CHECK(s.packets.size() == 1
              && s.packets.at(0).value(QStringLiteral("tag")).toString() == QStringLiteral("half"),
              "半包: 第二次 feed 补齐后产出该包");
    }
    // 半包头: 连 4 字节长度头都被截断(只到 3 字节)
    {
        ProtocolCodec c;
        Sink s;
        wire(c, s);
        const QByteArray pkt = c.encode(sample(QStringLiteral("hh")));
        c.feed(pkt.left(3));
        c.feed(pkt.mid(3));
        CHECK(s.packets.size() == 1
              && s.packets.at(0).value(QStringLiteral("tag")).toString() == QStringLiteral("hh"),
              "半包: 长度头被截断为 3 字节再补齐, 仍正确产出");
    }
}

static void testStickyPackets()
{
    ProtocolCodec c;
    Sink s;
    wire(c, s);
    const QByteArray two = c.encode(sample(QStringLiteral("A")))
                         + c.encode(sample(QStringLiteral("B")));
    c.feed(two);
    CHECK(s.packets.size() == 2 && s.errors.isEmpty(), "粘包: 一次 feed 两包, 产出 2 包");
    CHECK(s.packets.at(0).value(QStringLiteral("tag")).toString() == QStringLiteral("A")
          && s.packets.at(1).value(QStringLiteral("tag")).toString() == QStringLiteral("B"),
          "粘包: 两包顺序正确(A 在前 B 在后)");
    CHECK(c.pendingBytes() == 0, "粘包: 缓冲区无残留");

    // 粘包 + 尾部半包: 两整包 + 第三个包的前半
    const QByteArray three = two + c.encode(sample(QStringLiteral("C")));
    ProtocolCodec c2;
    Sink s2;
    wire(c2, s2);
    c2.feed(three.left(three.size() - 5));
    CHECK(s2.packets.size() == 2, "粘包+半包: 前两整包立即产出");
    c2.feed(three.right(5));
    CHECK(s2.packets.size() == 3
          && s2.packets.at(2).value(QStringLiteral("tag")).toString() == QStringLiteral("C"),
          "粘包+半包: 补齐后第三包产出");
}

static void testIllegalLength()
{
    // 长度为 0
    {
        ProtocolCodec c;
        Sink s;
        wire(c, s);
        QByteArray bad;
        bad.append(char(0)).append(char(0)).append(char(0)).append(char(0));
        bad.append("garbage");
        c.feed(bad);
        CHECK(s.packets.isEmpty() && s.errors.size() == 1,
              "非法长度 0: 报错一次, 不产包");
        CHECK(c.pendingBytes() == 0, "非法长度 0: 缓冲区已丢弃");
        // 丢弃后还能继续正常工作
        c.feed(c.encode(sample(QStringLiteral("after"))));
        CHECK(s.packets.size() == 1, "非法长度 0: 丢弃缓冲后后续包仍可解析(未死循环)");
    }
    // 长度 > 4096 (0xFFFFFFFF)
    {
        ProtocolCodec c;
        Sink s;
        wire(c, s);
        QByteArray bad;
        bad.append(char(0xFF)).append(char(0xFF)).append(char(0xFF)).append(char(0xFF));
        c.feed(bad);
        CHECK(s.packets.isEmpty() && s.errors.size() == 1,
              "非法长度 0xFFFFFFFF: 报错一次, 不产包");
        CHECK(c.pendingBytes() == 0, "非法长度 0xFFFFFFFF: 缓冲区已丢弃");
    }
}

static void testBadJsonBody()
{
    ProtocolCodec c;
    Sink s;
    wire(c, s);
    // 手工组一个"长度合法但正文不是 JSON"的帧
    const QByteArray body = "{ not json !!!";
    quint32 be = qToBigEndian<quint32>(body.size());
    QByteArray frame(reinterpret_cast<const char *>(&be), 4);
    frame.append(body);
    // 后面紧跟一个正常帧(粘包)
    frame.append(c.encode(sample(QStringLiteral("next"))));

    c.feed(frame);
    CHECK(s.errors.size() == 1 && s.packets.size() == 1,
          "坏 JSON 帧: 报错一次, 且不影响后续帧解析");
    CHECK(s.packets.at(0).value(QStringLiteral("tag")).toString() == QStringLiteral("next"),
          "坏 JSON 帧: 后续正常帧字段完整");
}

int main()
{
    testEncodeBigEndian();
    testRoundTrip();
    testHalfPacket();
    testStickyPackets();
    testIllegalLength();
    testBadJsonBody();

    if (g_failed == 0) {
        qDebug("\n===== codec_test 全部通过 =====");
        return 0;
    }
    qWarning("\n===== codec_test 有 %d 项失败 =====", g_failed);
    return 1;
}
