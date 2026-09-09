#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QJsonObject>

class QLineEdit;
class QLabel;
class QPushButton;
class TcpClient;

/*
 * 登录/注册对话框(同一个 QDialog 的两种模式, 共用与服务端的唯一一条连接)
 *
 * 串行协议下的请求-响应关联: 对话框用 pendingReq_ 记录当前未完成的请求类型,
 * jsonReceived 到达时按它分发处理(TcpClient 保证同一时刻只有一个未完成请求)。
 *
 * 等待态: 请求发出后禁用全部按钮, 防止连点造成重复请求;
 *         收到响应/出错/超时后恢复。
 */
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    // client 为外部(主窗口)持有的共享连接, 对话框只借用不拥有
    explicit LoginDialog(TcpClient *client, QWidget *parent = nullptr);

    QString userName() const { return m_userName; }   // 登录成功后的用户名
    QString userTel() const { return m_userTel; }

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void onPrimaryAction();     // 登录模式=发登录请求, 注册模式=发注册请求
    void onSwitchMode();
    void onJsonReceived(const QJsonObject &obj);
    void onConnError(const QString &reason);
    void onConnStateChanged(bool up);

private:
    void setWaiting(bool on);   // 等待态: 禁用按钮 + 提示
    void applyMode();           // 按当前模式显隐字段/改按钮文字
    bool validateInput();       // 空值/两次密码一致校验, 不通过则提示

    TcpClient *m_client = nullptr;

    QLineEdit *m_phoneEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;    // 仅注册模式可见
    QLineEdit *m_passEdit = nullptr;
    QLineEdit *m_pass2Edit = nullptr;   // 仅注册模式可见
    QLabel *m_nameLabel = nullptr;
    QLabel *m_pass2Label = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_primaryBtn = nullptr;
    QPushButton *m_switchBtn = nullptr;

    bool m_registerMode = false;
    bool m_waiting = false;
    int  m_pendingReq = 0;             // 1=登录 2=注册(注册成功后自动登录改回 1)
    QString m_userName;
    QString m_userTel;
};

#endif // LOGINDIALOG_H
