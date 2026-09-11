#ifndef TRACEPANEL_H
#define TRACEPANEL_H

#include <QJsonObject>
#include <QWidget>

class QPlainTextEdit;

/*
 * 执行轨迹面板(V2-M2 右栏): 把 Function Calling 循环"演"给观众看。
 *
 * 逐事件滚动显示: 用户输入 -> 工具调用(名称/参数) -> 执行结果(含门控状态
 * confirm_required=拦截) -> 本轮 token 计数。观众能直接看到:
 *   query_tickets → reserve_ticket(门控拦截, 未放行) → 用户确认后放行
 */
class TracePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TracePanel(QWidget *parent = nullptr);

    void appendUserText(const QString &text);     // [你] xxx
    void appendEvent(const QJsonObject &event);   // tool_start/tool_result/done/error
    void clear();

private:
    QPlainTextEdit *m_view = nullptr;             // objectName: traceView
};

#endif // TRACEPANEL_H
