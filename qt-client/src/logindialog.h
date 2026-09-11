#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;
class ApiClient;

/*
 * 登录/注册对话框(同一个 QDialog 的两种模式)
 *
 * V1-M2 起提交走 Agent 服务层(FastAPI /login /register), 不再直连 TCP:
 *   - showEvent 先 /health 探活: 服务层未启动时状态栏直接提示
 *     "请先启动 python -m agent.service";
 *   - 失败原因来自服务层 {"detail": 人话}(401 密码错 / 409 已注册 / 503 后端不可达),
 *     原样展示, 不再需要客户端猜错误码;
 *   - 会话(session_id/用户名/tel)由 ApiClient 持有, 登录成功 accept() 后由主窗口取用。
 *
 * 等待态: 请求发出后禁用全部按钮, 防止连点造成重复请求; 收到响应/出错后恢复。
 */
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    // api 为外部(主窗口)持有的共享客户端, 对话框只借用不拥有
    explicit LoginDialog(ApiClient *api, QWidget *parent = nullptr);

    QString userName() const;          // 登录成功后的用户名(取自 ApiClient 会话)
    QString userTel() const;

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void onPrimaryAction();            // 登录模式=/login, 注册模式=/register(自动登录)
    void onSwitchMode();
    void onHealthChecked(bool up, const QString &message);
    void onLoginFinished(bool ok, const QString &message);

private:
    void setWaiting(bool on);          // 等待态: 禁用按钮 + 提示
    void applyMode();                  // 按当前模式显隐字段/改按钮文字
    bool validateInput();              // 空值/两次密码一致校验, 不通过则提示

    ApiClient *m_api = nullptr;

    QLineEdit *m_phoneEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;   // 仅注册模式可见
    QLineEdit *m_passEdit = nullptr;
    QLineEdit *m_pass2Edit = nullptr;  // 仅注册模式可见
    QLabel *m_nameLabel = nullptr;
    QLabel *m_pass2Label = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_primaryBtn = nullptr;
    QPushButton *m_switchBtn = nullptr;

    bool m_registerMode = false;
    bool m_waiting = false;
};

#endif // LOGINDIALOG_H
