/*
 * V1-M3 验收测试: 聊天页签 + 确认卡片(核心场景)
 *
 * 无 Key 部分(始终运行):
 *  - applyAssistantReply 渲染: 助手消息/工具轨迹行/确认卡片出现与移除;
 *  - 卡片按钮语义: [确认预订]/[确认取消] -> 发送"确认", [不订了]/[先不取消]
 *    -> 发送对应文本(经 chatSent 信号断言, 不需要 LLM)。
 * 有 Key 部分(DEEPSEEK_API_KEY 存在时运行, 否则 QSKIP):
 *  - GUI 完整走 查票 -> 订票(出卡) -> 点确认 -> 预约成立 -> 查我的预约
 *    -> 取消(出卡) -> 点确认 -> 预约清空; 另: 点"不订了"不产生预订。
 *
 * 前置: C++ 服务端 :6000 已启动; 测试自行拉起服务层(8908)并注册临时账号。
 * 运行: QT_QPA_PLATFORM=offscreen ./chat_test
 *   (真实场景: DEEPSEEK_API_KEY=sk-... 在环境里, 服务层由本测试启动并继承)
 */
#include "mainwindow.h"
#include "apiclient.h"
#include "chatwidget.h"
#include "tickettablemodel.h"
#include "reservetablemodel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

static const QString kRepoDir = QStringLiteral("/mnt/d/桌面/my/ser-cli");
static const QUrl kTestApi = QUrl(QStringLiteral("http://127.0.0.1:8908"));

class TestChat : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void renderReplyAndTrace();
    void cardButtonsSendEquivalentText();
    void staleCardRemovedOnNextReply();

    void fullScenarioWithRealLlm();

private:
    bool waitServiceReady();
    bool login(MainWindow &w);
    bool sendAndAwait(ChatWidget *chat, ApiClient *api, const QString &text,
                      QJsonObject *bodyOut = nullptr);
    QWidget *liveCard(MainWindow &w);

    QProcess m_svc;
};

void TestChat::initTestCase()
{
    QVERIFY2(waitServiceReady(), "服务层未能在超时内就绪(检查 WSL python3/fastapi)");
}

void TestChat::cleanupTestCase()
{
    m_svc.terminate();
    m_svc.waitForFinished(5000);
}

bool TestChat::waitServiceReady()
{
    m_svc.setWorkingDirectory(kRepoDir);
    m_svc.start(QStringLiteral("python3"),
                { QStringLiteral("-m"), QStringLiteral("agent.service"),
                  QStringLiteral("--port"), QStringLiteral("8908") });
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

bool TestChat::login(MainWindow &w)
{
    const QString tel = QStringLiteral("139%09d")
        .arg(QRandomGenerator::global()->bounded(1000000000));
    ApiClient *api = w.api();
    QSignalSpy regSpy(api, &ApiClient::loginFinished);
    api->registerUser(tel, QStringLiteral("qt对话用户"), QStringLiteral("123456"));
    if (!regSpy.wait(10000) || regSpy.isEmpty() || !regSpy.first().at(0).toBool())
        return false;
    w.setSession(api->sessionId(), api->userTel(), api->userName());
    return true;
}

bool TestChat::sendAndAwait(ChatWidget *chat, ApiClient *api, const QString &text,
                            QJsonObject *bodyOut)
{
    QSignalSpy spy(api, &ApiClient::chatFinished);
    chat->findChild<QLineEdit *>(QStringLiteral("chatInput"))->setText(text);
    QTest::mouseClick(chat->findChild<QPushButton *>(QStringLiteral("chatSendBtn")),
                      Qt::LeftButton);
    if (!spy.wait(90000) || spy.isEmpty() || !spy.first().at(0).toBool())
        return false;
    if (bodyOut)
        *bodyOut = spy.first().at(1).toJsonObject();
    return true;
}

QWidget *TestChat::liveCard(MainWindow &w)
{
    return w.findChild<QWidget *>(QStringLiteral("confirmCard"));
}

// ---- 无 Key: 渲染与按钮语义 ------------------------------------------------

void TestChat::renderReplyAndTrace()
{
    MainWindow w(kTestApi);
    QVERIFY(login(w));
    ChatWidget *chat = w.chat();

    QJsonObject body;
    body.insert(QStringLiteral("reply"), QStringLiteral("共 4 个班次。"));
    QJsonArray trace;
    trace.append(QJsonObject({ { QStringLiteral("name"), QStringLiteral("query_tickets") },
                               { QStringLiteral("ok"), true },
                               { QStringLiteral("reason"), QJsonValue() } }));
    body.insert(QStringLiteral("tool_trace"), trace);
    body.insert(QStringLiteral("confirm_request"), QJsonValue());

    const int before = chat->messageCount();
    chat->applyAssistantReply(body);
    QVERIFY(chat->messageCount() >= before + 2);            // 消息 + 轨迹行
    QVERIFY(chat->lastLabelText(QStringLiteral("msgAssistant"))
                .contains(QStringLiteral("共 4 个班次")));
    QVERIFY(chat->lastLabelText(QStringLiteral("msgTrace"))
                .contains(QStringLiteral("query_tickets(成功)")));
    QVERIFY(liveCard(w) == nullptr);
    qDebug("[PASS] 渲染: 助手消息 + 工具轨迹折叠行, 无卡片时不出现卡片");
}

void TestChat::cardButtonsSendEquivalentText()
{
    MainWindow w(kTestApi);
    QVERIFY(login(w));
    ChatWidget *chat = w.chat();

    QJsonObject cr;
    cr.insert(QStringLiteral("action"), QStringLiteral("reserve_ticket"));
    cr.insert(QStringLiteral("args"), QJsonObject({ { QStringLiteral("tk_id"), 1 } }));
    cr.insert(QStringLiteral("display"),
              QStringLiteral("预订班次 1: 西安-北京, 日期 2026-10-01, 余票 98 张"));
    QJsonObject body;
    body.insert(QStringLiteral("reply"), QStringLiteral("确认预订吗?"));
    body.insert(QStringLiteral("confirm_request"), cr);

    chat->applyAssistantReply(body);
    QWidget *card = liveCard(w);
    QVERIFY(card != nullptr);
    QVERIFY(card->findChild<QLabel *>()->text().contains(QStringLiteral("预订班次 1")));

    auto *accept = card->findChild<QPushButton *>(QStringLiteral("confirmAcceptBtn"));
    auto *decline = card->findChild<QPushButton *>(QStringLiteral("confirmDeclineBtn"));
    QCOMPARE(accept->text(), QStringLiteral("确认预订"));
    QCOMPARE(decline->text(), QStringLiteral("不订了"));

    // 点"确认预订" = 发送"确认"(走完整模型链路; 此处只断言发出的文本)
    QSignalSpy sentSpy(chat, &ChatWidget::chatSent);
    QSignalSpy doneSpy(w.api(), &ApiClient::chatFinished);
    QTest::mouseClick(accept, Qt::LeftButton);
    QVERIFY(!sentSpy.isEmpty());
    QCOMPARE(sentSpy.first().at(0).toString(), QStringLiteral("确认"));
    QVERIFY(!accept->isEnabled());                          // 防双击
    QVERIFY(!decline->isEnabled());
    doneSpy.wait(10000);   // 等这轮应答回来(无 Key 时为 503)再继续, 避免 m_waiting 期间被守卫拦截

    // 取消类卡片
    QJsonObject cc;
    cc.insert(QStringLiteral("action"), QStringLiteral("cancel_reservation"));
    cc.insert(QStringLiteral("args"), QJsonObject({ { QStringLiteral("yd_id"), 7 } }));
    cc.insert(QStringLiteral("display"), QStringLiteral("取消预约 7: 西安-上海"));
    QJsonObject body2;
    body2.insert(QStringLiteral("reply"), QStringLiteral("确认取消吗?"));
    body2.insert(QStringLiteral("confirm_request"), cc);
    chat->applyAssistantReply(body2);
    auto *accept2 = liveCard(w)->findChild<QPushButton *>(QStringLiteral("confirmAcceptBtn"));
    auto *decline2 = liveCard(w)->findChild<QPushButton *>(QStringLiteral("confirmDeclineBtn"));
    QCOMPARE(accept2->text(), QStringLiteral("确认取消"));
    QCOMPARE(decline2->text(), QStringLiteral("先不取消"));

    QSignalSpy sentSpy2(chat, &ChatWidget::chatSent);
    QTest::mouseClick(decline2, Qt::LeftButton);
    QVERIFY(!sentSpy2.isEmpty());
    QCOMPARE(sentSpy2.first().at(0).toString(), QStringLiteral("先不取消"));
    qDebug("[PASS] 卡片按钮语义: 确认/取消 -> 等价文本, 点击后按钮禁用");
}

void TestChat::staleCardRemovedOnNextReply()
{
    MainWindow w(kTestApi);
    QVERIFY(login(w));
    ChatWidget *chat = w.chat();

    QJsonObject cr;
    cr.insert(QStringLiteral("action"), QStringLiteral("reserve_ticket"));
    cr.insert(QStringLiteral("display"), QStringLiteral("预订班次 2"));
    QJsonObject body;
    body.insert(QStringLiteral("confirm_request"), cr);
    chat->applyAssistantReply(body);
    QVERIFY(liveCard(w) != nullptr);

    QJsonObject body2;                                      // 下一轮无卡片
    body2.insert(QStringLiteral("reply"), QStringLiteral("已为您取消本次预订。"));
    body2.insert(QStringLiteral("confirm_request"), QJsonValue());
    chat->applyAssistantReply(body2);
    QVERIFY(liveCard(w) == nullptr);                        // 旧卡片已陈旧化
    QVERIFY(w.findChild<QWidget *>(QStringLiteral("confirmCardStale")) != nullptr);
    qDebug("[PASS] 新一轮应答后旧卡片陈旧化(用户已表态的卡片不复活)");
}

// ---- 有 Key: 真实 LLM 全场景(计划 V1-M3 核心验收) ---------------------------

void TestChat::fullScenarioWithRealLlm()
{
    if (qEnvironmentVariableIsEmpty("DEEPSEEK_API_KEY"))
        QSKIP("需要 DEEPSEEK_API_KEY(真实 LLM): 设置后重跑本用例验证完整门控闭环");

    MainWindow w(kTestApi);
    QVERIFY(login(w));
    ChatWidget *chat = w.chat();
    ApiClient *api = w.api();
    QJsonObject body;

    // 1. 查票
    QVERIFY(sendAndAwait(chat, api, QStringLiteral("有哪些票"), &body));
    QVERIFY(body.value(QStringLiteral("reply")).toString().length() > 4);
    QVERIFY(body.value(QStringLiteral("confirm_request")).isNull());
    QTRY_VERIFY_WITH_TIMEOUT(w.ticketModel()->rowCount() > 0, 10000);  // 自动刷新
    const int used1 = w.ticketModel()->ticketAt(0).num;
    qDebug("[PASS] 查票: 回复+表格自动刷新(tk1 已预约=%d)", used1);

    // 2. 订票 -> 出卡(意图明确, 门控复述; 模型偶发反问则再试一次)
    bool cardShown = false;
    for (int i = 0; i < 2 && !cardShown; ++i) {
        QVERIFY(sendAndAwait(chat, api, QStringLiteral("订10月1日去北京的"), &body));
        cardShown = (liveCard(w) != nullptr);
    }
    QVERIFY(cardShown);
    QVERIFY(liveCard(w)->findChild<QLabel *>()
                ->text().contains(QStringLiteral("西安-北京")));
    qDebug("[PASS] 订票: 确认卡片出现(复述文案含线路)");

    // 3. 点[确认预订] -> 预约成立, 表格联动
    QSignalSpy rSpy(api, &ApiClient::reservationsFinished);
    QTest::mouseClick(liveCard(w)->findChild<QPushButton *>(
                          QStringLiteral("confirmAcceptBtn")),
                      Qt::LeftButton);
    QVERIFY(rSpy.wait(90000));                              // 对话后自动刷新预约表
    QTRY_COMPARE(w.reserveModel()->rowCount(), 1);
    QVERIFY(w.reserveModel()->reservationAt(0).addr.contains(QStringLiteral("北京")));
    QTRY_VERIFY_WITH_TIMEOUT(
        w.ticketModel()->ticketAt(0).num == used1 + 1, 10000);         // 已预约+1
    QVERIFY(liveCard(w) == nullptr);
    qDebug("[PASS] 点确认: 预约成立, 车票已预约数+1, 我的预约出现 1 条");

    // 4. 查我的预约
    QVERIFY(sendAndAwait(chat, api, QStringLiteral("我订了哪些预约"), &body));
    QVERIFY(body.value(QStringLiteral("reply")).toString()
                .contains(QStringLiteral("北京")));
    qDebug("[PASS] 查我的预约: 回复含刚订的班次");

    // 5. 取消 -> 出卡 -> 点确认 -> 预约清空
    QVERIFY(sendAndAwait(chat, api, QStringLiteral("取消我的预约"), &body));
    QTRY_VERIFY_WITH_TIMEOUT(liveCard(w) != nullptr, 500);
    QVERIFY(liveCard(w)->findChild<QPushButton *>(
                QStringLiteral("confirmAcceptBtn"))->text()
                == QStringLiteral("确认取消"));
    QSignalSpy r2Spy(api, &ApiClient::reservationsFinished);
    QTest::mouseClick(liveCard(w)->findChild<QPushButton *>(
                          QStringLiteral("confirmAcceptBtn")),
                      Qt::LeftButton);
    QVERIFY(r2Spy.wait(90000));
    QTRY_COMPARE(w.reserveModel()->rowCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(
        w.ticketModel()->ticketAt(0).num == used1, 10000);             // 余票复原
    qDebug("[PASS] 取消闭环: 出卡->点确认->预约清空, 余票复原");

    // 6. 点"不订了"不产生预订
    QVERIFY(sendAndAwait(chat, api, QStringLiteral("订10月2日去上海的"), &body));
    QTRY_VERIFY_WITH_TIMEOUT(liveCard(w) != nullptr, 500);
    QSignalSpy r3Spy(api, &ApiClient::reservationsFinished);
    QTest::mouseClick(liveCard(w)->findChild<QPushButton *>(
                          QStringLiteral("confirmDeclineBtn")),
                      Qt::LeftButton);
    QVERIFY(r3Spy.wait(90000));
    QTRY_COMPARE(w.reserveModel()->rowCount(), 0);          // 仍然 0 条预约
    qDebug("[PASS] 拒绝路径: 点\"不订了\"后没有任何预约产生");
}

QTEST_MAIN(TestChat)
#include "main.moc"
