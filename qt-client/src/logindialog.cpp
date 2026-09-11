#include "logindialog.h"
#include "apiclient.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginDialog::LoginDialog(ApiClient *api, QWidget *parent)
    : QDialog(parent), m_api(api)
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

    m_statusLabel = new QLabel(QStringLiteral("正在连接 Agent 服务..."), this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(true);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(btns);
    root->addWidget(m_statusLabel);

    applyMode();

    connect(m_primaryBtn, &QPushButton::clicked, this, &LoginDialog::onPrimaryAction);
    connect(m_switchBtn, &QPushButton::clicked, this, &LoginDialog::onSwitchMode);

    // 应答/探活结果来自共享的 ApiClient, 信号在 UI 线程回槽
    connect(m_api, &ApiClient::healthChecked, this, &LoginDialog::onHealthChecked);
    connect(m_api, &ApiClient::loginFinished, this, &LoginDialog::onLoginFinished);
}

void LoginDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    // 首次显示即探活: 服务层没起时直接告诉用户怎么起, 而不是等点登录才报错
    m_api->checkHealth();
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

    if (!validateInput())             // 输入校验纯本地, 先做
        return;

    if (m_registerMode) {
        m_api->registerUser(m_phoneEdit->text().trimmed(),
                            m_nameEdit->text().trimmed(), m_passEdit->text());
        m_statusLabel->setText(QStringLiteral("正在注册(成功后自动登录)..."));
    } else {
        m_api->login(m_phoneEdit->text().trimmed(), m_passEdit->text());
        m_statusLabel->setText(QStringLiteral("正在登录..."));
    }
    setWaiting(true);
}

void LoginDialog::onSwitchMode()
{
    m_registerMode = !m_registerMode;
    applyMode();
    m_statusLabel->setText(m_registerMode ? QStringLiteral("填写信息完成注册")
                                          : QStringLiteral("输入手机号和密码登录"));
}

void LoginDialog::onHealthChecked(bool up, const QString &message)
{
    if (m_waiting)
        return;                       // 等待登录应答期间不被探活结果覆盖
    m_statusLabel->setText(up ? QStringLiteral("已连接 Agent 服务，请登录") : message);
}

void LoginDialog::onLoginFinished(bool ok, const QString &message)
{
    setWaiting(false);
    if (ok) {
        accept();                     // 会话信息(session_id/用户名)在 ApiClient 里
        return;
    }
    m_statusLabel->setText(message);  // 服务层的人话错误: 401 密码错/409 已注册/503 后端不可达
}

QString LoginDialog::userName() const
{
    return m_api->userName();
}

QString LoginDialog::userTel() const
{
    return m_api->userTel();
}
