#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/*
 * Agent 服务层(FastAPI)的 HTTP 客户端(V1-M2 起, 替代直连 TCP 的 TcpClient)
 *
 * - 全部请求经 QNetworkAccessManager 异步发出, 结果以信号送回创建线程(UI 线程),
 *   界面永不阻塞 —— 与服务层的会话(TCP+登录态)都住在 Python 侧, Qt 只会 HTTP;
 * - 会话信息(sessionId/userName/userTel)由本类持有, /chat /tickets 等自动携带;
 * - 错误人话化两级(对应服务层错误分级):
 *     传输层失败(服务层未启动): 无 HTTP 状态 -> "请先启动 python -m agent.service";
 *     HTTP 4xx/5xx: 服务层返回 {"detail": 人话}, 解析后直接展示。
 * - busy(): 在途请求计数, 调用方用它防连点(登录对话框/聊天输入)。
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);
    explicit ApiClient(const QUrl &baseUrl, QObject *parent = nullptr); // 测试可指定地址

    // ---- 会话 ----
    void checkHealth();                                              // 探活(登录页提示用)
    void login(const QString &tel, const QString &passwd);
    void registerUser(const QString &tel, const QString &name, const QString &passwd);
    void logout();
    void adoptSession(const QString &sessionId, const QString &tel,   // 测试缝: 注入会话
                      const QString &name);

    // ---- 数据 / 对话 ----
    void fetchTickets();                                             // GET /tickets
    void fetchReservations();                                        // GET /reservations
    void sendChat(const QString &text);                              // POST /chat
    // V2-M2: SSE 流式对话(POST /chat/stream); 事件经 chatEvent 逐个送达,
    // done 事件后流结束; 传输层/HTTP 错误走 chatStreamFailed
    void sendChatStream(const QString &text);

    // ---- 状态 ----
    bool hasSession() const { return !m_sessionId.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    QString sessionId() const { return m_sessionId; }
    QString userName() const { return m_userName; }
    QString userTel() const { return m_userTel; }
    QUrl baseUrl() const { return m_baseUrl; }

signals:
    void healthChecked(bool up, const QString &message);
    void loginFinished(bool ok, const QString &message);
    void loggedOut();
    void ticketsFinished(bool ok, const QJsonArray &tickets, const QString &message);
    void reservationsFinished(bool ok, const QJsonArray &reservations,
                              const QString &message);
    // body: {reply, tool_trace, usage, confirm_request, error}(完整一轮对话)
    void chatFinished(bool ok, const QJsonObject &body, const QString &message);
    // V2-M2: SSE 事件(token/tool_start/tool_result/confirm_request/done/error)
    void chatEvent(const QJsonObject &event);
    void chatStreamFailed(const QString &message);

private:
    // 统一请求出口: 发 JSON 请求, 应答回到 UI 线程后按 HTTP 状态解析为 ok/message
    void requestJson(const char *method, const QUrl &url,
                     const QJsonObject &body,
                     const std::function<void(bool ok, const QJsonObject &response,
                                              const QString &message)> &handler);
    QString errorMessage(QNetworkReply *reply) const;                // 错误 -> 人话
    QUrl endpoint(const QString &path) const;                        // baseUrl + path

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_baseUrl;
    int m_pending = 0;                 // 在途请求计数(busy 用)
    QString m_sessionId;
    QString m_userName;
    QString m_userTel;
};

#endif // APICLIENT_H
