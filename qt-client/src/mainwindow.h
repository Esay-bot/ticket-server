#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonArray>
#include <QMainWindow>
#include <QString>

#include "apiclient.h"

class ApiClient;
class ChatWidget;
class TracePanel;
class TicketTableModel;
class ReserveTableModel;
class QTableView;
class QPushButton;
class QAbstractItemModel;

/*
 * 主窗口(V1-M2 起数据源从直连 TCP 换成 Agent 服务层 HTTP)
 *
 * 布局: 页签[AI 助手(聊天+确认卡片) / 车票列表 / 我的预约]
 *       + 工具栏[刷新车票 / 刷新我的预约 / 退出登录]。
 * 手动"预约/取消"按钮已移除 —— 危险操作只能经对话(Agent 门控)发起,
 * 界面上不存在绕过门控的捷径, 这正是桌面端要"演"的核心。
 * 每轮对话结束后自动刷新两张表格(余票/预约随对话实时变化)。
 *
 * 会话: session_id/tel/用户名由 ApiClient 持有; "退出登录"调 /logout
 * (服务层关 TCP)后带 reloginRequested 标记关窗, main.cpp 循环重新弹登录
 * —— 覆盖"断网重启服务后可重新登录继续"的验收场景。
 *
 * 全部异步: QNetworkAccessManager 发请求, 信号回 UI 线程刷新表格/状态栏。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // apiBaseUrl: 测试注入服务层地址; 缺省(空 QUrl)用 AppConfig::kApiBaseUrl
    explicit MainWindow(const QUrl &apiBaseUrl = QUrl(), QWidget *parent = nullptr);
    ~MainWindow();

    // 弹出模态登录对话框(走 /login /register); 登录成功返回 true
    bool login();

    // 测试缝: 不弹对话框直接注入会话(自动化测试用)
    void setSession(const QString &sessionId, const QString &tel, const QString &name);

    // "退出登录"关窗后为 true: main.cpp 据此重新走登录流程
    bool reloginRequested() const { return m_reloginRequested; }

    ApiClient *api() const { return m_api; }
    ChatWidget *chat() const { return m_chat; }
    TracePanel *trace() const { return m_trace; }
    QString userTel() const { return m_api->userTel(); }
    QString userName() const { return m_api->userName(); }
    TicketTableModel *ticketModel() const { return m_ticketModel; }
    ReserveTableModel *reserveModel() const { return m_reserveModel; }

public slots:
    void refreshTickets();               // GET /tickets
    void refreshMyReserve();             // GET /reservations
    void logoutAndRelogin();             // POST /logout -> 关窗 -> main 重新登录

private slots:
    void onTicketsFinished(bool ok, const QJsonArray &tickets, const QString &message);
    void onReservationsFinished(bool ok, const QJsonArray &reservations,
                                const QString &message);

private:
    void buildUi();
    QTableView *makeView(const QString &name, QAbstractItemModel *model);

    ApiClient *m_api = nullptr;
    ChatWidget *m_chat = nullptr;
    TracePanel *m_trace = nullptr;
    TicketTableModel *m_ticketModel = nullptr;
    ReserveTableModel *m_reserveModel = nullptr;
    QTableView *m_ticketView = nullptr;
    QTableView *m_reserveView = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_myReserveBtn = nullptr;
    QPushButton *m_logoutBtn = nullptr;
    bool m_reloginRequested = false;
};

#endif // MAINWINDOW_H
