#include "chatwidget.h"
#include "apiclient.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

ChatWidget::ChatWidget(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    // 消息流: 可滚动区域 + 自上而下的布局
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("chatScroll"));
    m_scroll->setWidgetResizable(true);
    m_holder = new QWidget(m_scroll);
    m_flow = new QVBoxLayout(m_holder);
    m_flow->setAlignment(Qt::AlignTop);
    m_flow->setSpacing(8);
    m_flow->addStretch(1);                 // 消息少时靠顶
    m_scroll->setWidget(m_holder);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("chatInput"));
    m_input->setPlaceholderText(
        QStringLiteral("试试: 有哪些票 / 订10月1日去北京的 / 取消我的预约"));

    m_sendBtn = new QPushButton(QStringLiteral("发送"), this);
    m_sendBtn->setObjectName(QStringLiteral("chatSendBtn"));

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendBtn);

    auto *root = new QVBoxLayout(this);
    root->addWidget(m_scroll, 1);
    root->addLayout(inputRow);

    connect(m_sendBtn, &QPushButton::clicked, this, &ChatWidget::sendMessage);
    connect(m_input, &QLineEdit::returnPressed, this, &ChatWidget::sendMessage);
    connect(m_api, &ApiClient::chatFinished, this, &ChatWidget::onChatFinished);
    // V2-M2: 传输层/HTTP 层的流失败(服务层被停等), 与 done.error 分开处理
    connect(m_api, &ApiClient::chatStreamFailed, this, [this](const QString &message) {
        m_streaming = nullptr;
        setWaiting(false);
        addMessage(QStringLiteral("助手"),
                   QStringLiteral("（请求失败）%1").arg(message),
                   QStringLiteral("msgAssistant"));
    });

    addMessage(QStringLiteral("助手"),
               QStringLiteral("你好，我是票务助手。可以帮你：查票、订票、查我的预约、取消预约。"
                              "订票/取消前我会先和你确认。"),
               QStringLiteral("msgAssistant"));
}

// ---- 消息渲染 --------------------------------------------------------------

void ChatWidget::addMessage(const QString &who, const QString &text,
                            const QString &objectName)
{
    auto *label = new QLabel(QStringLiteral("%1：%2").arg(who, text), m_holder);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_flow->insertWidget(m_flow->count() - 1, label);   // stretch 之前
    scrollToEnd();
}

QLabel *ChatWidget::addTraceLine(const QJsonArray &trace)
{
    if (trace.isEmpty())
        return nullptr;
    QStringList parts;
    for (const QJsonValue &v : trace) {
        const QJsonObject t = v.toObject();
        const QString name = t.value(QStringLiteral("name")).toString();
        const QString state = t.value(QStringLiteral("ok")).toBool()
                                  ? QStringLiteral("成功")
                                  : (t.value(QStringLiteral("reason")).toString().isEmpty()
                                         ? QStringLiteral("失败")
                                         : t.value(QStringLiteral("reason")).toString());
        parts << QStringLiteral("%1(%2)").arg(name, state);
    }
    auto *label = new QLabel(QStringLiteral("　└ 工具: %1")
                                 .arg(parts.join(QStringLiteral(" · "))),
                             m_holder);
    label->setObjectName(QStringLiteral("msgTrace"));
    label->setWordWrap(true);
    m_flow->insertWidget(m_flow->count() - 1, label);
    scrollToEnd();
    return label;
}

void ChatWidget::applyAssistantReply(const QJsonObject &body)
{
    removeCard();                          // 用户已表态的旧卡片不复活
    addMessage(QStringLiteral("助手"),
               body.value(QStringLiteral("reply")).toString(),
               QStringLiteral("msgAssistant"));
    addTraceLine(body.value(QStringLiteral("tool_trace")).toArray());
    const QJsonValue cr = body.value(QStringLiteral("confirm_request"));
    if (cr.isObject())
        showCard(cr.toObject());
    scrollToEnd();
}

// ---- 确认卡片 --------------------------------------------------------------

void ChatWidget::showCard(const QJsonObject &confirmRequest)
{
    const QString action = confirmRequest.value(QStringLiteral("action")).toString();
    const bool isReserve = (action == QStringLiteral("reserve_ticket"));
    const QString acceptText = isReserve ? QStringLiteral("确认预订")
                                         : QStringLiteral("确认取消");
    const QString declineText = isReserve ? QStringLiteral("不订了")
                                          : QStringLiteral("先不取消");

    auto *card = new QWidget(m_holder);
    card->setObjectName(QStringLiteral("confirmCard"));
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 8, 8, 8);

    auto *text = new QLabel(confirmRequest.value(QStringLiteral("display")).toString(), card);
    text->setObjectName(QStringLiteral("confirmText"));
    text->setWordWrap(true);
    lay->addWidget(text);

    auto *btns = new QHBoxLayout;
    auto *accept = new QPushButton(acceptText, card);
    accept->setObjectName(QStringLiteral("confirmAcceptBtn"));
    auto *decline = new QPushButton(declineText, card);
    decline->setObjectName(QStringLiteral("confirmDeclineBtn"));
    btns->addWidget(accept);
    btns->addWidget(decline);
    btns->addStretch(1);
    lay->addLayout(btns);

    // 点击 = 发送等价文本走完整模型链路(门控零改动); 立即禁用防双击
    connect(accept, &QPushButton::clicked, this, [this, accept, decline]() {
        accept->setEnabled(false);
        decline->setEnabled(false);
        sendEquivalent(QStringLiteral("确认"));
    });
    connect(decline, &QPushButton::clicked, this, [this, accept, decline, isReserve]() {
        accept->setEnabled(false);
        decline->setEnabled(false);
        sendEquivalent(isReserve ? QStringLiteral("不订了")
                                 : QStringLiteral("先不取消"));
    });

    m_flow->insertWidget(m_flow->count() - 1, card);
    m_card = card;
    scrollToEnd();
}

void ChatWidget::removeCard()
{
    if (m_card == nullptr)
        return;
    m_card->setDisabled(true);             // 留在历史里但不可再点
    m_card->setObjectName(QStringLiteral("confirmCardStale"));
    m_card = nullptr;
}

// ---- 发送 ------------------------------------------------------------------

void ChatWidget::sendMessage()
{
    if (m_waiting)
        return;
    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;
    m_input->clear();
    sendEquivalent(text);
}

void ChatWidget::sendEquivalent(const QString &text)
{
    if (m_waiting)
        return;
    emit chatSent(text);
    addMessage(QStringLiteral("你"), text, QStringLiteral("msgUser"));
    removeCard();                         // 用户已表态(文本或按钮): 旧卡片作废
    setWaiting(true);
    m_api->sendChatStream(text);          // V2-M2: SSE 流式(打字机+轨迹面板)
}

// ---- V2-M2: SSE 事件消费(打字机) -------------------------------------------

QLabel *ChatWidget::streamLabel()
{
    if (m_streaming == nullptr) {
        m_streaming = new QLabel(m_holder);
        m_streaming->setObjectName(QStringLiteral("msgAssistant"));
        m_streaming->setWordWrap(true);
        m_streaming->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_flow->insertWidget(m_flow->count() - 1, m_streaming);
    }
    return m_streaming;
}

void ChatWidget::onChatEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("token")) {
        QLabel *label = streamLabel();
        const QString head = label->text().isEmpty()
                                 ? QStringLiteral("助手：") : QString();
        label->setText(label->text() + head
                       + event.value(QStringLiteral("text")).toString());
        scrollToEnd();
    } else if (type == QStringLiteral("tool_start")) {
        // 工具轮开始: 细节在右栏轨迹面板, 这里只留一行轻提示
        if (m_thinking != nullptr)
            m_thinking->setText(QStringLiteral("助手正在调用工具 %1 ...")
                                    .arg(event.value(QStringLiteral("name")).toString()));
    } else if (type == QStringLiteral("confirm_request")) {
        showCard(event);
    } else if (type == QStringLiteral("done")) {
        const QString finalText =
            event.value(QStringLiteral("content")).toString();
        if (m_streaming != nullptr && !finalText.isEmpty()) {
            // 异常轮: 用道歉话术覆盖已流出的部分 token; 正常轮文本一致
            m_streaming->setText(QStringLiteral("助手：%1").arg(finalText));
        }
        m_streaming = nullptr;
        setWaiting(false);
        scrollToEnd();
    }
}

void ChatWidget::setWaiting(bool on)
{
    m_waiting = on;
    m_input->setEnabled(!on);
    m_sendBtn->setEnabled(!on);
    if (on && m_thinking == nullptr) {
        m_thinking = new QLabel(QStringLiteral("助手正在思考(可能调用工具)..."), m_holder);
        m_thinking->setObjectName(QStringLiteral("msgThinking"));
        m_flow->insertWidget(m_flow->count() - 1, m_thinking);
        scrollToEnd();
    } else if (!on && m_thinking != nullptr) {
        m_thinking->deleteLater();
        m_thinking = nullptr;
    }
}

void ChatWidget::onChatFinished(bool ok, const QJsonObject &body, const QString &message)
{
    setWaiting(false);
    if (!ok) {
        removeCard();
        addMessage(QStringLiteral("助手"),
                   QStringLiteral("（请求失败）%1").arg(message),
                   QStringLiteral("msgAssistant"));
        return;
    }
    applyAssistantReply(body);
}

void ChatWidget::scrollToEnd()
{
    QTimer::singleShot(0, this, [this]() {
        QScrollBar *bar = m_scroll->verticalScrollBar();
        if (bar)
            bar->setValue(bar->maximum());
    });
}

// ---- 测试辅助 --------------------------------------------------------------

int ChatWidget::messageCount() const
{
    // 除 stretch 外的布局项数 = 消息/轨迹/卡片总数
    return m_flow->count() - 1;
}

QString ChatWidget::lastLabelText(const QString &objectName) const
{
    for (int i = m_flow->count() - 1; i >= 0; --i) {
        QLayoutItem *it = m_flow->itemAt(i);
        QLabel *label = qobject_cast<QLabel *>(it ? it->widget() : nullptr);
        if (label && label->objectName() == objectName)
            return label->text();
    }
    return QString();
}
