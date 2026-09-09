#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QJsonObject>

class TcpClient;
class TicketTableModel;
class QTableView;
class QPushButton;

/*
 * 主窗口: 持有全局唯一的 TcpClient(与服务端的串行连接)
 * M4: 车票表格(QTableView + TicketTableModel) + 刷新
 * M5: 预约/取消/我的预约 + 断线处理(在 updateUiState 上扩展)
 *
 * 响应关联: 串行协议下同一时刻只有一个未完成请求, m_pendingReq 记录
 * 当前请求 type, jsonReceived 到达时按它分发(服务端响应不回显 type)。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 弹出模态登录对话框; 登录成功返回 true 并记录用户信息
    bool login();

    // 测试缝: 不弹对话框直接注入会话(自动化测试用)
    void setSession(const QString &tel, const QString &name);

    TcpClient *client() const { return m_client; }
    QString userTel() const { return m_userTel; }
    QString userName() const { return m_userName; }
    TicketTableModel *ticketModel() const { return m_model; }

public slots:
    void refreshTickets();               // 发送查票请求(type=3)

private slots:
    void onJsonReceived(const QJsonObject &obj);
    void onConnStateChanged(bool up);
    void onConnError(const QString &reason);

private:
    void buildUi();
    void handleViewResp(const QJsonObject &obj);
    void updateUiState();                // 按连接态+等待态统一刷新控件可用性

    TcpClient *m_client = nullptr;
    TicketTableModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QPushButton *m_refreshBtn = nullptr;

    int m_pendingReq = 0;                // 0=空闲; 3=查票(M5 再扩 4/5/6)
    QString m_userName;
    QString m_userTel;
};

#endif // MAINWINDOW_H
