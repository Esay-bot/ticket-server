#ifndef CHATWIDGET_H
#define CHATWIDGET_H

#include <QJsonObject>
#include <QWidget>

class ApiClient;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/*
 * AI 助手聊天页签(V1-M3 核心): 消息流 + 输入框 + 确认卡片 + 工具轨迹折叠行
 *
 * 确认卡片(门控的产品化表达):
 *  - 服务层在"模型想订票/取消但用户未确认"时返回 confirm_request{action,args,display};
 *    界面渲染复述文案 + [确认预订/不订了] 或 [确认取消/先不取消];
 *  - 点击按钮 = 发送等价文本("确认"/"不订了"/"先不取消")走完整模型链路 ——
 *    门控代码零改动, 界面上不存在直接调工具的捷径;
 *  - 每轮应答开始时移除旧卡片(用户已表态的卡片不复活);
 *    点过的按钮立即禁用防双击。
 *
 * tool_trace 以单行折叠文本显示(V2-M2 再面板化):
 *   "工具: query_tickets(成功) · reserve_ticket(需确认)"
 *
 * 测试缝: applyAssistantReply(body) 直接喂 /chat 响应体, 无需网络即可验证渲染。
 */
class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWidget(ApiClient *api, QWidget *parent = nullptr);

    // 渲染一轮 /chat 应答: 助手消息 + 工具轨迹行 + (可选)确认卡片
    void applyAssistantReply(const QJsonObject &body);

    // V2-M2: 逐个消费 /chat/stream 的 SSE 事件(打字机渲染);
    // token 增量拼接, done 收尾(异常覆盖已流出文本), confirm_request 出卡片
    void onChatEvent(const QJsonObject &event);

    // 测试辅助: 消息流里最后一条指定 objectName 的 QLabel 文本
    QString lastLabelText(const QString &objectName) const;
    int messageCount() const;

signals:
    // 每次向服务层发出一条对话文本(输入框或卡片按钮), 供测试断言按钮语义
    void chatSent(const QString &text);

private slots:
    void sendMessage();                    // 输入框 -> api->sendChat
    void onChatFinished(bool ok, const QJsonObject &body, const QString &message);

private:
    void addMessage(const QString &who, const QString &text,
                    const QString &objectName);
    QLabel *addTraceLine(const QJsonArray &trace);   // 工具轨迹折叠行
    void showCard(const QJsonObject &confirmRequest);
    void removeCard();
    void sendEquivalent(const QString &text);        // 卡片按钮的等价文本发送
    void setWaiting(bool on);                        // 等待应答: 禁输入
    void scrollToEnd();
    QLabel *streamLabel();                           // 当前打字机消息(懒建)

    ApiClient *m_api = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_holder = nullptr;
    QVBoxLayout *m_flow = nullptr;         // 消息流(自上而下)
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_thinking = nullptr;          // "正在思考..."占位
    QWidget *m_card = nullptr;             // 当前存活的确认卡片(每轮移除)
    QLabel *m_streaming = nullptr;         // V2-M2 打字机中的助手消息
    bool m_waiting = false;
};

#endif // CHATWIDGET_H
