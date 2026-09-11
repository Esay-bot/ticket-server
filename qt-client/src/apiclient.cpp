#include "apiclient.h"
#include "appconfig.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

ApiClient::ApiClient(QObject *parent)
    : ApiClient(QUrl(QString::fromLatin1(AppConfig::kApiBaseUrl)), parent)
{
}

ApiClient::ApiClient(const QUrl &baseUrl, QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_baseUrl(baseUrl)
{
}

QUrl ApiClient::endpoint(const QString &path) const
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + path);
    return url;
}

// ---- 统一请求出口 ----------------------------------------------------------

void ApiClient::requestJson(const char *method, const QUrl &url,
                            const QJsonObject &body,
                            const std::function<void(bool, const QJsonObject &,
                                                     const QString &)> &handler)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = nullptr;
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (qstrcmp(method, "GET") == 0)
        reply = m_nam->get(req);
    else if (qstrcmp(method, "POST") == 0)
        reply = m_nam->post(req, payload);
    else
        reply = m_nam->sendCustomRequest(req, method, payload);

    ++m_pending;
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler]() {
        --m_pending;
        const QString msg = errorMessage(reply);
        const bool ok = msg.isEmpty();
        QJsonObject response;
        if (ok) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject())
                response = doc.object();
        }
        reply->deleteLater();
        handler(ok, response, msg);
    });
}

QString ApiClient::errorMessage(QNetworkReply *reply) const
{
    // 1) 连 HTTP 状态都没有: 传输层失败(服务层未启动/网络不可达)
    const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusVar.isValid()) {
        return QStringLiteral("无法连接 Agent 服务(%1)，请先启动: python -m agent.service")
            .arg(m_baseUrl.toString());
    }
    // 2) 2xx 成功; 其余展示服务层 {"detail": 人话}, 解析不出再退化为状态码
    const int status = statusVar.toInt();
    if (status / 100 == 2)
        return QString();

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonValue detail = doc.object().value(QStringLiteral("detail"));
    if (detail.isString())
        return detail.toString();
    if (detail.isArray()) {           // 422 校验错误: [{loc, msg, type}...]
        const QJsonArray arr = detail.toArray();
        if (!arr.isEmpty()) {
            const QJsonObject first = arr.at(0).toObject();
            const QString loc = first.value(QStringLiteral("loc")).toArray().last().toString();
            const QString msg = first.value(QStringLiteral("msg")).toString();
            return QStringLiteral("输入有误(%1): %2").arg(loc, msg);
        }
    }
    return QStringLiteral("请求失败(HTTP %1)").arg(status);
}

// ---- 会话 ------------------------------------------------------------------

void ApiClient::checkHealth()
{
    requestJson("GET", endpoint(QStringLiteral("/health")), QJsonObject(),
                [this](bool ok, const QJsonObject &, const QString &message) {
                    emit healthChecked(ok, message);
                });
}

void ApiClient::login(const QString &tel, const QString &passwd)
{
    QJsonObject body;
    body.insert(QStringLiteral("tel"), tel);
    body.insert(QStringLiteral("passwd"), passwd);
    requestJson("POST", endpoint(QStringLiteral("/login")), body,
                [this](bool ok, const QJsonObject &response, const QString &message) {
                    if (ok) {
                        m_sessionId = response.value(QStringLiteral("session_id")).toString();
                        m_userTel = response.value(QStringLiteral("tel")).toString();
                        m_userName = response.value(QStringLiteral("user_name")).toString();
                    }
                    emit loginFinished(ok, message);
                });
}

void ApiClient::registerUser(const QString &tel, const QString &name, const QString &passwd)
{
    QJsonObject body;
    body.insert(QStringLiteral("tel"), tel);
    body.insert(QStringLiteral("user_name"), name);
    body.insert(QStringLiteral("passwd"), passwd);
    // 服务层注册成功即自动登录, 返回 session —— 与旧 TCP 客户端行为一致
    requestJson("POST", endpoint(QStringLiteral("/register")), body,
                [this](bool ok, const QJsonObject &response, const QString &message) {
                    if (ok) {
                        m_sessionId = response.value(QStringLiteral("session_id")).toString();
                        m_userTel = response.value(QStringLiteral("tel")).toString();
                        m_userName = response.value(QStringLiteral("user_name")).toString();
                    }
                    emit loginFinished(ok, message);
                });
}

void ApiClient::logout()
{
    QJsonObject body;
    body.insert(QStringLiteral("session_id"), m_sessionId);
    requestJson("POST", endpoint(QStringLiteral("/logout")), body,
                [this](bool, const QJsonObject &, const QString &) {
                    m_sessionId.clear();
                    m_userTel.clear();
                    m_userName.clear();
                    emit loggedOut();
                });
}

void ApiClient::adoptSession(const QString &sessionId, const QString &tel,
                             const QString &name)
{
    m_sessionId = sessionId;
    m_userTel = tel;
    m_userName = name;
}

// ---- 数据 / 对话 -----------------------------------------------------------

void ApiClient::fetchTickets()
{
    QUrl url = endpoint(QStringLiteral("/tickets"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("session_id"), m_sessionId);
    url.setQuery(q);
    requestJson("GET", url, QJsonObject(),
                [this](bool ok, const QJsonObject &response, const QString &message) {
                    emit ticketsFinished(
                        ok && response.value(QStringLiteral("ok")).toBool(),
                        response.value(QStringLiteral("tickets")).toArray(),
                        ok ? message
                           : QStringLiteral("查询车票失败：") + message);
                });
}

void ApiClient::fetchReservations()
{
    QUrl url = endpoint(QStringLiteral("/reservations"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("session_id"), m_sessionId);
    url.setQuery(q);
    requestJson("GET", url, QJsonObject(),
                [this](bool ok, const QJsonObject &response, const QString &message) {
                    emit reservationsFinished(
                        ok && response.value(QStringLiteral("ok")).toBool(),
                        response.value(QStringLiteral("reservations")).toArray(),
                        ok ? message
                           : QStringLiteral("查询预约失败：") + message);
                });
}

void ApiClient::sendChat(const QString &text)
{
    QJsonObject body;
    body.insert(QStringLiteral("session_id"), m_sessionId);
    body.insert(QStringLiteral("text"), text);
    requestJson("POST", endpoint(QStringLiteral("/chat")), body,
                [this](bool ok, const QJsonObject &response, const QString &message) {
                    emit chatFinished(ok, response, message);
                });
}
