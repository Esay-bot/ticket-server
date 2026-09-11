/*
 * LoginDialog 自动化测试(HTTP 形态, offscreen)
 * 前置: WSL 里 C++ 服务端(127.0.0.1:6000)已启动; 本测试自行拉起 FastAPI 服务层(8901)。
 * 覆盖 V1-M2 验收: 登录成功 / 错误密码人话提示 / 重复注册提示 /
 *                  服务层未启动时提示"请先启动 agent 服务" / 等待态防连点 / 输入校验
 */
#include "logindialog.h"
#include "apiclient.h"

#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

static const QString kRepoDir = QStringLiteral("/mnt/d/桌面/my/ser-cli");
static const QUrl kTestApi = QUrl(QStringLiteral("http://127.0.0.1:8901"));
static const QUrl kDeadApi = QUrl(QStringLiteral("http://127.0.0.1:8999")); // 故意无服务

class TestLoginDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();                   // 拉起服务层并等 /health 就绪
    void cleanupTestCase();

    void serviceDownShowsStartupHint();   // 验收: 服务层未启动的提示
    void loginSuccess();
    void loginWrongPassword();
    void duplicateRegister();
    void emptyInputValidation();
    void passwordMismatchValidation();
    void waitingDisablesButtons();

private:
    bool waitHealth(ApiClient *api);       // 轮询探活(python 启动要数秒)
    QString ensureAccount(ApiClient *api); // 注册临时账号(不依赖数据库种子)
    QProcess m_svc;
};

void TestLoginDialog::initTestCase()
{
    m_svc.setWorkingDirectory(kRepoDir);
    m_svc.start(QStringLiteral("python3"),
                { QStringLiteral("-m"), QStringLiteral("agent.service"),
                  QStringLiteral("--port"), QStringLiteral("8901") });
    ApiClient probe(kTestApi, this);
    QVERIFY2(waitHealth(&probe), "服务层未能在超时内就绪(检查 WSL python3/fastapi)");
}

QString TestLoginDialog::ensureAccount(ApiClient *api)
{
    const QString tel = QStringLiteral("139%09d")
        .arg(QRandomGenerator::global()->bounded(1000000000));
    QSignalSpy spy(api, &ApiClient::loginFinished);
    api->registerUser(tel, QStringLiteral("qt测试用户"), QStringLiteral("123456"));
    if (!spy.wait(10000) || !spy.first().at(0).toBool()) {
        qWarning() << "预注册临时账号失败";
        return QString();
    }
    return tel;
}

void TestLoginDialog::cleanupTestCase()
{
    m_svc.terminate();
    m_svc.waitForFinished(5000);
}

bool TestLoginDialog::waitHealth(ApiClient *api)
{
    // 连接被拒(python 还在启动)的失败是瞬时的, 需要在轮与轮之间留节拍,
    // 否则重试轮瞬间耗尽而服务还没起完
    for (int i = 0; i < 40; ++i) {
        QSignalSpy spy(api, &ApiClient::healthChecked);
        api->checkHealth();
        if (spy.wait(2000) && !spy.isEmpty() && spy.first().at(0).toBool())
            return true;
        QTest::qWait(500);
    }
    return false;
}

void TestLoginDialog::serviceDownShowsStartupHint()
{
    ApiClient api(kDeadApi, this);
    QSignalSpy spy(&api, &ApiClient::healthChecked);
    api.checkHealth();
    QVERIFY(spy.wait(3000));
    QVERIFY(!spy.first().at(0).toBool());
    QVERIFY(spy.first().at(1).toString()
                .contains(QStringLiteral("python -m agent.service")));   // 告诉用户怎么启动

    LoginDialog dlg(&api);
    dlg.show();
    QTRY_VERIFY(dlg.findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                    .contains(QStringLiteral("无法连接")));
    qDebug("[PASS] 服务层未启动: 状态栏提示先启动 python -m agent.service");
}

void TestLoginDialog::loginSuccess()
{
    ApiClient api(kTestApi, this);
    const QString tel = ensureAccount(&api);
    QVERIFY(!tel.isEmpty());

    LoginDialog dlg(&api);
    dlg.show();
    QTRY_VERIFY(!dlg.findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                    .contains(QStringLiteral("正在连接")));              // 探活先回来

    dlg.findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(tel);
    dlg.findChild<QLineEdit *>(QStringLiteral("passEdit"))
        ->setText(QStringLiteral("123456"));
    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QTRY_COMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dlg.userName(), QStringLiteral("qt测试用户"));
    QCOMPARE(dlg.userTel(), tel);
    QVERIFY(api.hasSession());            // session_id 已在 ApiClient 里
    qDebug("[PASS] 临时账号登录成功(accept, 用户名/tel/session_id 正确)");
}

void TestLoginDialog::loginWrongPassword()
{
    ApiClient api(kTestApi, this);
    const QString tel = ensureAccount(&api);
    QVERIFY(!tel.isEmpty());

    LoginDialog dlg(&api);
    dlg.show();

    dlg.findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(tel);
    dlg.findChild<QLineEdit *>(QStringLiteral("passEdit"))
        ->setText(QStringLiteral("badpw"));
    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QTRY_VERIFY(dlg.findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                    .contains(QStringLiteral("登录失败")));               // 服务层 401 人话
    QVERIFY(dlg.isVisible());             // 对话框不关闭
    QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn"))->isEnabled());
    qDebug("[PASS] 错误密码: 明确提示且可重试");
}

void TestLoginDialog::duplicateRegister()
{
    ApiClient api(kTestApi, this);
    const QString tel = ensureAccount(&api);
    QVERIFY(!tel.isEmpty());

    LoginDialog dlg(&api);
    dlg.show();

    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("switchBtn")), Qt::LeftButton);
    dlg.findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(tel);
    dlg.findChild<QLineEdit *>(QStringLiteral("nameEdit"))
        ->setText(QStringLiteral("重复号"));
    dlg.findChild<QLineEdit *>(QStringLiteral("passEdit"))
        ->setText(QStringLiteral("123456"));
    dlg.findChild<QLineEdit *>(QStringLiteral("pass2Edit"))
        ->setText(QStringLiteral("123456"));
    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QTRY_VERIFY(dlg.findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                    .contains(QStringLiteral("已被注册")));               // 服务层 409 人话
    QVERIFY(dlg.isVisible());
    qDebug("[PASS] 重复注册: 明确提示(手机号已被注册)");
}

void TestLoginDialog::emptyInputValidation()
{
    ApiClient api(kTestApi, this);
    LoginDialog dlg(&api);
    dlg.show();

    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);
    QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                .contains(QStringLiteral("不能为空")));
    QTRY_VERIFY(!api.busy());             // 未发出任何请求
    qDebug("[PASS] 空值校验: 提示且不发送请求");
}

void TestLoginDialog::passwordMismatchValidation()
{
    ApiClient api(kTestApi, this);
    LoginDialog dlg(&api);
    dlg.show();

    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("switchBtn")), Qt::LeftButton);
    dlg.findChild<QLineEdit *>(QStringLiteral("phoneEdit"))
        ->setText(QStringLiteral("13800009999"));
    dlg.findChild<QLineEdit *>(QStringLiteral("nameEdit"))->setText(QStringLiteral("x"));
    dlg.findChild<QLineEdit *>(QStringLiteral("passEdit"))->setText(QStringLiteral("aaa"));
    dlg.findChild<QLineEdit *>(QStringLiteral("pass2Edit"))->setText(QStringLiteral("bbb"));
    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                .contains(QStringLiteral("不一致")));
    QTRY_VERIFY(!api.busy());
    qDebug("[PASS] 注册两次密码不一致校验");
}

void TestLoginDialog::waitingDisablesButtons()
{
    ApiClient api(kTestApi, this);
    const QString tel = ensureAccount(&api);
    QVERIFY(!tel.isEmpty());

    LoginDialog dlg(&api);
    dlg.show();

    dlg.findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(tel);
    dlg.findChild<QLineEdit *>(QStringLiteral("passEdit"))
        ->setText(QStringLiteral("123456"));
    QTest::mouseClick(dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    // 点击后立即(响应到达前)按钮应处于禁用 —— 防连点
    QVERIFY(!dlg.findChild<QPushButton *>(QStringLiteral("primaryBtn"))->isEnabled());
    QVERIFY(!dlg.findChild<QPushButton *>(QStringLiteral("switchBtn"))->isEnabled());

    QTRY_COMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
    qDebug("[PASS] 等待态: 请求期间登录/切换按钮均禁用");
}

QTEST_MAIN(TestLoginDialog)
#include "main.moc"
