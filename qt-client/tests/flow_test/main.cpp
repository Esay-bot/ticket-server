/*
 * V1-M2 数据链路测试(HTTP 形态, offscreen, 需真实 C++ 服务端):
 *  1. 登录 -> /tickets 表格模型填充 -> /reservations 预约模型填充
 *  2. 退出登录: 服务层销毁会话, 之后的请求 404 人话提示
 *  3. 断链: kill 服务层进程 -> 请求失败提示"请先启动 agent 服务"
 * 对应旧 TCP flow_test(手动预约/取消闭环) —— 危险操作已移入对话+门控,
 * 见 V1-M3 聊天页签。
 */
#include "mainwindow.h"
#include "apiclient.h"
#include "tickettablemodel.h"
#include "reservetablemodel.h"

#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStatusBar>
#include <QUrl>
#include <QtTest>

static const QString kRepoDir = QStringLiteral("/mnt/d/桌面/my/ser-cli");
static const QUrl kTestApi = QUrl(QStringLiteral("http://127.0.0.1:8906"));

class TestFlow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void dataLinkAndLogout();
    void serviceKilledShowsStartupHint();

private:
    bool startServiceAndWait();           // 拉起服务层并等 /health
    bool login(MainWindow &w);            // 注册临时账号并登录(不依赖种子数据)
    QProcess m_svc;
};

bool TestFlow::login(MainWindow &w)
{
    const QString tel = QStringLiteral("139%09d")
        .arg(QRandomGenerator::global()->bounded(1000000000));
    ApiClient *api = w.api();
    QSignalSpy regSpy(api, &ApiClient::loginFinished);
    api->registerUser(tel, QStringLiteral("qt流程用户"), QStringLiteral("123456"));
    if (!regSpy.wait(10000) || regSpy.isEmpty() || !regSpy.first().at(0).toBool()) {
        
        return false;
    }
    w.setSession(api->sessionId(), api->userTel(), api->userName());
    return api->hasSession();
}

void TestFlow::initTestCase()
{
    QVERIFY2(startServiceAndWait(), "服务层未能在超时内就绪(检查 WSL python3/fastapi)");
}

bool TestFlow::startServiceAndWait()
{
    m_svc.setWorkingDirectory(kRepoDir);
    m_svc.start(QStringLiteral("python3"),
                { QStringLiteral("-m"), QStringLiteral("agent.service"),
                  QStringLiteral("--port"), QStringLiteral("8906") });
    // 连接被拒(服务还在启动)时 finished 几乎同步到达且 up=false,
    // 因此每轮换新 spy 重试, 直到真正探活成功
    // 连接被拒(python 还在启动)的失败是瞬时的, 需要在轮与轮之间留节拍,
    // 否则 20 轮瞬间耗尽而服务还没起完
    for (int i = 0; i < 40; ++i) {
        ApiClient probe(kTestApi, this);
        QSignalSpy spy(&probe, &ApiClient::healthChecked);
        probe.checkHealth();
        if (spy.wait(2000) && !spy.isEmpty() && spy.first().at(0).toBool())
            return true;
        QTest::qWait(500);
    }
    return false;
}

void TestFlow::cleanupTestCase()
{
    m_svc.terminate();
    m_svc.waitForFinished(5000);
}

void TestFlow::dataLinkAndLogout()
{
    MainWindow w(kTestApi);
    QVERIFY(login(w));
    ApiClient *api = w.api();
    QVERIFY(api->hasSession());

    // 1. 车票表: 点"刷新车票"按钮 -> /tickets -> TicketTableModel(经 fromHttpArray)
    QSignalSpy tSpy(api, &ApiClient::ticketsFinished);
    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("refreshBtn")), Qt::LeftButton);
    QVERIFY(tSpy.wait(10000));
    QVERIFY(tSpy.first().at(0).toBool());
    QVERIFY(w.ticketModel()->rowCount() > 0);
    const Ticket t1 = w.ticketModel()->ticketAt(0);
    QVERIFY(t1.tkId > 0);
    QVERIFY(!t1.addr.isEmpty());
    QVERIFY(QTest::qVerify(t1.max >= t1.num, "max>=num", "余票非负", __FILE__, __LINE__));
    qDebug("[PASS] /tickets -> 表格模型: %d 条记录, 首行 编号=%d 线路=%s",
           w.ticketModel()->rowCount(), t1.tkId, qPrintable(t1.addr));

    // 2. 我的预约表: /reservations -> ReserveTableModel
    QSignalSpy rSpy(api, &ApiClient::reservationsFinished);
    w.refreshMyReserve();
    QVERIFY(rSpy.wait(10000));
    QVERIFY(rSpy.first().at(0).toBool());
    qDebug("[PASS] /reservations -> 预约模型: %d 条记录", w.reserveModel()->rowCount());

    // 3. 退出登录: 服务层销毁会话, 再请求 -> 404 人话
    QSignalSpy loSpy(api, &ApiClient::loggedOut);
    w.logoutAndRelogin();
    QVERIFY(loSpy.wait(10000));
    QVERIFY(w.reloginRequested());
    QVERIFY(!api->hasSession());
    QSignalSpy t2Spy(api, &ApiClient::ticketsFinished);
    api->fetchTickets();                  // 直接发: 会话已注销
    QVERIFY(t2Spy.wait(10000));
    QVERIFY(!t2Spy.first().at(0).toBool());
    QVERIFY(t2Spy.first().at(2).toString().contains(QStringLiteral("重新登录")));
    qDebug("[PASS] 退出登录: 会话销毁, 后续请求提示重新登录");
}

void TestFlow::serviceKilledShowsStartupHint()
{
    MainWindow w(kTestApi);
    QVERIFY(login(w));
    ApiClient *api = w.api();

    // kill 服务层进程: 模拟"断网/服务被停"
    m_svc.terminate();
    m_svc.waitForFinished(5000);

    QSignalSpy tSpy(api, &ApiClient::ticketsFinished);
    w.refreshTickets();
    QVERIFY(tSpy.wait(10000));
    QVERIFY(!tSpy.first().at(0).toBool());
    QVERIFY(tSpy.first().at(2).toString()
                .contains(QStringLiteral("python -m agent.service")));
    qDebug("[PASS] 服务层被停: 请求失败并提示先启动 agent 服务(重登流程见 dialog_test)");
}

QTEST_MAIN(TestFlow)
#include "main.moc"
