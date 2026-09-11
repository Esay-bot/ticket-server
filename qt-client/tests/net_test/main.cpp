/*
 * TcpClient 集成测试(需真实服务端运行在 127.0.0.1:6000)
 * 覆盖计划 M2 验收: 真实注册->登录请求-响应往返, 日志打印收发内容
 * 另测: 串行请求锁 / 错误密码 ERR / 连接被拒
 * 账号自注册(随机 tel), 不依赖数据库种子数据。
 */
#include "tcpclient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QTimer>
#include <QDebug>

static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { qDebug("[PASS] %s", msg); } \
        else { qDebug("[FAIL] %s  (%s:%d)", msg, __FILE__, __LINE__); ++g_failed; } \
    } while (0)

class Runner : public QObject
{
    Q_OBJECT
public:
    explicit Runner(QObject *parent = nullptr) : QObject(parent), bad(this)
    {
        cli = new TcpClient(this);
        connect(cli, &TcpClient::jsonReceived, this, &Runner::onJson);
        connect(cli, &TcpClient::connStateChanged, this, [this](bool up) {
            if (up) {
                CHECK(true, "连接: connStateChanged(true), 服务器 127.0.0.1:6000");
                stepRegister();
            }
        });
        connect(cli, &TcpClient::errorOccurred, this, [this](const QString &r) {
            qWarning("[cli-err] %s", qPrintable(r));
        });

        // 连接被拒路径
        connect(&bad, &TcpClient::errorOccurred, this, [this](const QString &r) {
            CHECK(r.contains(QStringLiteral("refused"), Qt::CaseInsensitive),
                  "连接被拒: errorOccurred 报 Connection refused");
            finish();
        });
        connect(&bad, &TcpClient::connStateChanged, this, [](bool up) {
            if (up) { CHECK(false, "连接被拒: 不应成功"); }
        });
    }

    void start()
    {
        cli->connectToHost(QStringLiteral("127.0.0.1"), 6000);
        // 6100 无服务, 应触发 ConnectionRefused
        bad.connectToHost(QStringLiteral("127.0.0.1"), 6100);
    }

private slots:
    void onJson(const QJsonObject &obj)
    {
        if (step_ == StepRegister) {
            CHECK(obj.value(QStringLiteral("status")).toString() == QStringLiteral("OK"),
                  "注册(随机账号): status=OK");
            stepLoginOk();
        } else if (step_ == StepLoginOk) {
            CHECK(obj.value(QStringLiteral("status")).toString() == QStringLiteral("OK"),
                  "登录(正确密码): status=OK");
            CHECK(obj.value(QStringLiteral("user_name")).toString() == m_name,
                  "登录(正确密码): user_name 往返一致(中文正常)");
            stepLoginWrong();
        } else if (step_ == StepLoginWrong) {
            CHECK(obj.value(QStringLiteral("status")).toString() == QStringLiteral("ERR"),
                  "登录(错误密码): status=ERR");
            // 全部主流程完成, bad 的 refused 事件到达后 finish
        }
    }

private:
    enum Step { StepRegister, StepLoginOk, StepLoginWrong };
    int step_ = StepRegister;
    QString m_tel;
    QString m_name = QStringLiteral("网络测试用户");

    void stepRegister()
    {
        // 随机账号: 不依赖 M0 时代注册的种子账号(库可能被重置)
        m_tel = QStringLiteral("139%09d")
            .arg(QDateTime::currentMSecsSinceEpoch() % 1000000000);
        QJsonObject req;
        req.insert(QStringLiteral("type"), 2);
        req.insert(QStringLiteral("user_tel"), m_tel);
        req.insert(QStringLiteral("user_name"), m_name);
        req.insert(QStringLiteral("user_passwd"), QStringLiteral("123456"));
        CHECK(cli->send(req), "发送注册请求: send 返回 true");

        // 串行锁: 响应未到前再发应被拒绝(响应只能经事件循环到达, 此处必为 busy)
        CHECK(!cli->send(req), "串行锁: 未收到响应前第二次 send 被拒绝");
        step_ = StepRegister;
    }

    void stepLoginOk()
    {
        QJsonObject req;
        req.insert(QStringLiteral("type"), 1);
        req.insert(QStringLiteral("user_tel"), m_tel);
        req.insert(QStringLiteral("user_passwd"), QStringLiteral("123456"));
        CHECK(cli->send(req), "发送登录请求: send 返回 true");
        step_ = StepLoginOk;
    }

    void stepLoginWrong()
    {
        step_ = StepLoginWrong;
        QJsonObject req;
        req.insert(QStringLiteral("type"), 1);
        req.insert(QStringLiteral("user_tel"), m_tel);
        req.insert(QStringLiteral("user_passwd"), QStringLiteral("wrongpw"));
        CHECK(cli->send(req), "发送错误密码登录: send 返回 true");
    }

    void finish()
    {
        // 给 wrong-password 响应留出到达时间(refused 与它并发)
        QTimer::singleShot(500, []() {
            if (g_failed == 0) {
                qDebug("\n===== net_test 全部通过 =====");
                QCoreApplication::exit(0);
            } else {
                qWarning("\n===== net_test 有 %d 项失败 =====", g_failed);
                QCoreApplication::exit(1);
            }
        });
    }

    TcpClient *cli = nullptr;
    TcpClient bad;
};

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    Runner runner;
    QTimer::singleShot(12000, []() {   // 总超时兜底
        qWarning("总超时, 测试未完成");
        QCoreApplication::exit(2);
    });

    QTimer::singleShot(0, &runner, &Runner::start);
    return a.exec();
}

#include "main.moc"
