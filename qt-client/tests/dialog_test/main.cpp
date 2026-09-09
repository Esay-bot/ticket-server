/*
 * LoginDialog 自动化测试(offscreen, 对真实服务端)
 * 覆盖计划 M3 验收: 真实账号登录成功 / 错误密码明确提示 / 重复注册提示 /
 *                  请求期间按钮不可连点 / 空值与两次密码不一致校验
 */
#include "logindialog.h"
#include "tcpclient.h"

#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QtTest>

class TestLoginDialog : public QObject
{
    Q_OBJECT

private slots:
    void loginSuccess();
    void loginWrongPassword();
    void duplicateRegister();
    void emptyInputValidation();
    void passwordMismatchValidation();
    void waitingDisablesButtons();

private:
    // QtTest 的 QTRY 宏只能用于 void 函数(宏内部含裸 return), 故用双出参
    void makeDialog(TcpClient *&clientOut, LoginDialog *&dlgOut);
};

void TestLoginDialog::makeDialog(TcpClient *&clientOut, LoginDialog *&dlgOut)
{
    clientOut = new TcpClient(this);
    dlgOut = new LoginDialog(clientOut);
    dlgOut->show();
    QTRY_VERIFY(clientOut->isConnected());   // showEvent 里发起连接
}

void TestLoginDialog::loginSuccess()
{
    TcpClient *client = nullptr;
    LoginDialog *dlg = nullptr;
    makeDialog(client, dlg);

    dlg->findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(QStringLiteral("13800001111"));
    dlg->findChild<QLineEdit *>(QStringLiteral("passEdit"))->setText(QStringLiteral("123456"));
    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QTRY_COMPARE(dlg->result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dlg->userName(), QStringLiteral("测试用户"));
    QCOMPARE(dlg->userTel(), QStringLiteral("13800001111"));
    qDebug("[PASS] 真实账号登录成功进入(对话框 accept, 用户名/tel 正确)");
    delete dlg;
}

void TestLoginDialog::loginWrongPassword()
{
    TcpClient *client = nullptr;
    LoginDialog *dlg = nullptr;
    makeDialog(client, dlg);

    dlg->findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(QStringLiteral("13800001111"));
    dlg->findChild<QLineEdit *>(QStringLiteral("passEdit"))->setText(QStringLiteral("badpw"));
    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QTRY_VERIFY(dlg->findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                    .contains(QStringLiteral("登录失败")));
    QVERIFY(dlg->isVisible());       // 对话框不关闭
    QVERIFY(dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn"))->isEnabled()); // 按钮恢复
    qDebug("[PASS] 错误密码: 明确提示且可重试");
    delete dlg;
}

void TestLoginDialog::duplicateRegister()
{
    TcpClient *client = nullptr;
    LoginDialog *dlg = nullptr;
    makeDialog(client, dlg);

    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("switchBtn")), Qt::LeftButton); // 切到注册
    dlg->findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(QStringLiteral("13800001111"));
    dlg->findChild<QLineEdit *>(QStringLiteral("nameEdit"))->setText(QStringLiteral("重复号"));
    dlg->findChild<QLineEdit *>(QStringLiteral("passEdit"))->setText(QStringLiteral("123456"));
    dlg->findChild<QLineEdit *>(QStringLiteral("pass2Edit"))->setText(QStringLiteral("123456"));
    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QTRY_VERIFY(dlg->findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                    .contains(QStringLiteral("已被注册")));
    QVERIFY(dlg->isVisible());
    qDebug("[PASS] 重复注册: 明确提示(手机号已被注册)");
    delete dlg;
}

void TestLoginDialog::emptyInputValidation()
{
    TcpClient *client = nullptr;
    LoginDialog *dlg = nullptr;
    makeDialog(client, dlg);

    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);
    QVERIFY(dlg->findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                .contains(QStringLiteral("不能为空")));
    QVERIFY(!client->isBusy());      // 未发出任何请求
    qDebug("[PASS] 空值校验: 提示且不发送请求");
    delete dlg;
}

void TestLoginDialog::passwordMismatchValidation()
{
    TcpClient *client = nullptr;
    LoginDialog *dlg = nullptr;
    makeDialog(client, dlg);

    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("switchBtn")), Qt::LeftButton);
    dlg->findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(QStringLiteral("13800009999"));
    dlg->findChild<QLineEdit *>(QStringLiteral("nameEdit"))->setText(QStringLiteral("x"));
    dlg->findChild<QLineEdit *>(QStringLiteral("passEdit"))->setText(QStringLiteral("aaa"));
    dlg->findChild<QLineEdit *>(QStringLiteral("pass2Edit"))->setText(QStringLiteral("bbb"));
    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    QVERIFY(dlg->findChild<QLabel *>(QStringLiteral("statusLabel"))->text()
                .contains(QStringLiteral("不一致")));
    QVERIFY(!client->isBusy());
    qDebug("[PASS] 注册两次密码不一致校验");
    delete dlg;
}

void TestLoginDialog::waitingDisablesButtons()
{
    TcpClient *client = nullptr;
    LoginDialog *dlg = nullptr;
    makeDialog(client, dlg);

    dlg->findChild<QLineEdit *>(QStringLiteral("phoneEdit"))->setText(QStringLiteral("13800001111"));
    dlg->findChild<QLineEdit *>(QStringLiteral("passEdit"))->setText(QStringLiteral("123456"));
    QTest::mouseClick(dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn")), Qt::LeftButton);

    // 点击后立即(响应到达前)按钮应处于禁用 —— 防连点
    QVERIFY(!dlg->findChild<QPushButton *>(QStringLiteral("primaryBtn"))->isEnabled());
    QVERIFY(!dlg->findChild<QPushButton *>(QStringLiteral("switchBtn"))->isEnabled());

    QTRY_COMPARE(dlg->result(), static_cast<int>(QDialog::Accepted));
    qDebug("[PASS] 等待态: 请求期间登录/切换按钮均禁用");
    delete dlg;
}

QTEST_MAIN(TestLoginDialog)
#include "main.moc"
