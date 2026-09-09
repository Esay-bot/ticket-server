#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QJsonObject>

class TcpClient;
class TicketTableModel;
class ReserveTableModel;
class QTableView;
class QPushButton;
class QAbstractItemModel;

/*
 * 主窗口(M3 登录入口 / M4 车票表格 / M5 全流程与断线处理)
 *
 * 串行协议下的响应关联: m_pendingReq 记录当前未完成请求 type,
 * jsonReceived 按 type 分发(服务端一问一答且响应不回显 type)。
 *
 * 断线状态机: connStateChanged(false) -> 状态栏"已断线", 全部操作按钮禁用,
 * 仅"重连"可用; 重连成功 -> 自动刷新车票恢复界面。全部异步信号驱动, UI 不阻塞。
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
    TicketTableModel *ticketModel() const { return m_ticketModel; }
    ReserveTableModel *reserveModel() const { return m_reserveModel; }

public slots:
    void refreshTickets();               // 查票(type=3)
    void refreshMyReserve();             // 我的预约(type=5)
    void reserveSelected();              // 预约选中行车票(type=4)
    void cancelSelected();               // 取消选中行预约(type=6)
    void reconnect();                    // 断线重连

private slots:
    void onJsonReceived(const QJsonObject &obj);
    void onConnStateChanged(bool up);
    void onConnError(const QString &reason);

private:
    void buildUi();
    QTableView *makeView(const QString &name, QAbstractItemModel *model);
    bool sendRequest(int type, const QJsonObject &req);   // 守卫+发送+置 pending
    void handleViewResp(const QJsonObject &obj);
    void handleReserveResp(const QJsonObject &obj);
    void handleMyReserveResp(const QJsonObject &obj);
    void handleCancelResp(const QJsonObject &obj);
    void updateUiState();                // 按连接态+等待态统一刷新控件可用性
    QString notePrefix() const;          // "预约成功 · " 之类的状态栏前缀

    TcpClient *m_client = nullptr;
    TicketTableModel *m_ticketModel = nullptr;
    ReserveTableModel *m_reserveModel = nullptr;
    QTableView *m_ticketView = nullptr;
    QTableView *m_reserveView = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_reserveBtn = nullptr;
    QPushButton *m_myReserveBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_reconnectBtn = nullptr;

    int m_pendingReq = 0;                // 0=空闲; 3查票/4预约/5我的预约/6取消
    bool m_chainMyReserve = false;       // 预约/取消成功后链式刷新我的预约
    QString m_actionNote;                // 最近操作结果(预约成功/取消预约成功),
                                         // 拼进链式刷新的最终状态, 避免提示被瞬刷覆盖
    QString m_userName;
    QString m_userTel;
};

#endif // MAINWINDOW_H
