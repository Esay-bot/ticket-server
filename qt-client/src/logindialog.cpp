#include "logindialog.h"
#include "tcpclient.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginDialog::LoginDialog(TcpClient *client, QWidget *parent)
    : QDialog(parent), m_client(client)
{
    setWindowTitle(QStringLiteral("票务预约系统 - 登录"));
    setMinimumWidth(360);

    m_phoneEdit = new QLineEdit(this);
    m_phoneEdit->setObjectName(QStringLiteral("phoneEdit"));
    m_phoneEdit->setPlaceholderText(QStringLiteral("11 位手机号"));

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));

    m_passEdit = new QLineEdit(this);
    m_passEdit->setObjectName(QStringLiteral("passEdit"));
    m_passEdit->setEchoMode(QLineEdit::Password);

    m_pass2Edit = new QLineEdit(this);
    m_pass2Edit->setObjectName(QStringLiteral("pass2Edit"));
    m_pass2Edit->setEchoMode(QLineEdit::Password);

    m_nameLabel = new QLabel(QStringLiteral("用户名:"), this);
    m_pass2Label = new QLabel(QStringLiteral("确认密码:"), this);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("手机号:"), m_phoneEdit);
    form->addRow(m_nameLabel, m_nameEdit);
    form->addRow(QStringLiteral("密码:"), m_passEdit);
    form->addRow(m_pass2Label, m_pass2Edit);

    m_primaryBtn = new QPushButton(QStringLiteral("登录"), this);
    m_primaryBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_switchBtn = new QPushButton(QStringLiteral("没有账号？注册"), this);
    m_switchBtn->setObjectName(QStringLiteral("switchBtn"));
    m_switchBtn->setFlat(true);

    auto *btns = new QHBoxLayout;
    btns->addWidget(m_primaryBtn, 1);
    btns->addWidget(m_switchBtn);

    m_statusLabel = new QLabel(QStringLiteral("正在连接服务器..."), this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(true);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(btns);
    root->addWidget(m_statusLabel);

    applyMode();

    connect(m_primaryBtn, &QPushButton::clicked, this, &LoginDialog::onPrimaryAction);
    connect(m_switchBtn, &QPushButton::clicked, this, &LoginDialog::onSwitchMode);

    // 响应/错误/连接状态都来自共享的 TcpClient, 对话框存活期间它是唯一请求发起方
    connect(m_client, &TcpClient::jsonReceived, this, &LoginDialog::onJsonReceived);
    connect(m_client, &TcpClient::errorOccurred, this, &LoginDialog::onConnError);
    connect(m_client, &TcpClient::connStateChanged, this, &LoginDialog::onConnStateChanged);
}

void LoginDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    // 首次显示即发起连接(重复调用由 TcpClient 内部去重)
    if (!m_client->isConnected())
        m_client->connectToHost(QStringLiteral("127.0.0.1"), 6000);
}

void LoginDialog::applyMode()
{
    // 注册模式多两行字段: 用户名 + 二次密码确认
    m_nameLabel->setVisible(m_registerMode);
    m_nameEdit->setVisible(m_registerMode);
    m_pass2Label->setVisible(m_registerMode);
    m_pass2Edit->setVisible(m_registerMode);

    m_primaryBtn->setText(m_registerMode ? QStringLiteral("注册并登录")
                                         : QStringLiteral("登录"));
    m_switchBtn->setText(m_registerMode ? QStringLiteral("已有账号？返回登录")
                                         : QStringLiteral("没有账号？注册"));
    adjustSize();
}

void LoginDialog::setWaiting(bool on)
{
    m_waiting = on;
    m_primaryBtn->setEnabled(!on);
    m_switchBtn->setEnabled(!on);
    for (QLineEdit *e : { m_phoneEdit, m_nameEdit, m_passEdit, m_pass2Edit })
        e->setEnabled(!on);
}

bool LoginDialog::validateInput()
{
    if (m_phoneEdit->text().trimmed().isEmpty()
        || m_passEdit->text().isEmpty()) {
        m_statusLabel->setText(QStringLiteral("手机号和密码不能为空"));
        return false;
    }
    if (m_registerMode) {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_statusLabel->setText(QStringLiteral("用户名不能为空"));
            return false;
        }
        if (m_passEdit->text() != m_pass2Edit->text()) {
            m_statusLabel->setText(QStringLiteral("两次输入的密码不一致"));
            return false;
        }
    }
    return true;
}

void LoginDialog::onPrimaryAction()
{
    if (m_waiting)
        return;                       // 等待态防连点(按钮已禁用, 双保险)

    if (!validateInput())             // 输入校验不依赖连接状态, 先做
        return;

    if (!m_client->isConnected()) {
        m_statusLabel->setText(QStringLiteral("尚未连接服务器，正在连接，请稍候重试"));
        m_client->connectToHost(QStringLiteral("127.0.0.1"), 6000);
        return;
    }

    QJsonObject req;
    if (m_registerMode) {
        req.insert(QStringLiteral("type"), 2);
        req.insert(QStringLiteral("user_tel"), m_phoneEdit->text().trimmed());
        req.insert(QStringLiteral("user_name"), m_nameEdit->text().trimmed());
        req.insert(QStringLiteral("user_passwd"), m_passEdit->text());
    } else {
        req.insert(QStringLiteral("type"), 1);
        req.insert(QStringLiteral("user_tel"), m_phoneEdit->text().trimmed());
        req.insert(QStringLiteral("user_passwd"), m_passEdit->text());
    }

    if (!m_client->send(req)) {
        m_statusLabel->setText(QStringLiteral("发送失败，请重试"));
        return;
    }
    m_pendingReq = m_registerMode ? 2 : 1;
    setWaiting(true);
    m_statusLabel->setText(m_registerMode ? QStringLiteral("正在注册...")
                                          : QStringLiteral("正在登录..."));
}

void LoginDialog::onSwitchMode()
{
    m_registerMode = !m_registerMode;
    applyMode();
    m_statusLabel->setText(m_registerMode ? QStringLiteral("填写信息完成注册")
                                          : QStringLiteral("输入手机号和密码登录"));
}

void LoginDialog::onJsonReceived(const QJsonObject &obj)
{
    if (m_pendingReq == 0)
        return;                       // 不属于本对话框的响应

    const QString status = obj.value(QStringLiteral("status")).toString();
    setWaiting(false);

    if (m_pendingReq == 1) {          // 登录响应
        if (status == QStringLiteral("OK")) {
            m_userName = obj.value(QStringLiteral("user_name")).toString();
            m_userTel = m_phoneEdit->text().trimmed();
            accept();                 // 关闭对话框, 主窗口接管连接
            return;
        }
        m_statusLabel->setText(status == QStringLiteral("ERR")
                                   ? QStringLiteral("登录失败：手机号或密码错误")
                                   : QStringLiteral("登录响应异常(status=%1)").arg(status));
    } else if (m_pendingReq == 2) {   // 注册响应
        if (status == QStringLiteral("OK")) {
            // 注册成功自动登录(与服务端行为对齐: 账号即刻可用)
            m_statusLabel->setText(QStringLiteral("注册成功，正在自动登录..."));
            QJsonObject loginReq;
            loginReq.insert(QStringLiteral("type"), 1);
            loginReq.insert(QStringLiteral("user_tel"), m_phoneEdit->text().trimmed());
            loginReq.insert(QStringLiteral("user_passwd"), m_passEdit->text());
            if (m_client->send(loginReq)) {
                m_pendingReq = 1;
                setWaiting(true);
                return;
            }
            m_statusLabel->setText(QStringLiteral("注册成功，自动登录发送失败，请手动登录"));
        } else if (status == QStringLiteral("ERR")) {
            m_statusLabel->setText(QStringLiteral("注册失败：手机号可能已被注册"));
        } else {
            m_statusLabel->setText(QStringLiteral("注册响应异常(status=%1)").arg(status));
        }
    }
    m_pendingReq = 0;
}

void LoginDialog::onConnError(const QString &reason)
{
    if (m_waiting) {
        setWaiting(false);
        m_pendingReq = 0;
        m_statusLabel->setText(QStringLiteral("请求失败：%1").arg(reason));
    }
}

void LoginDialog::onConnStateChanged(bool up)
{
    if (m_waiting)
        return;                       // 等待期间的状态翻转由错误回调处理
    m_statusLabel->setText(up ? QStringLiteral("已连接服务器，请登录")
                              : QStringLiteral("与服务器断开"));
}
