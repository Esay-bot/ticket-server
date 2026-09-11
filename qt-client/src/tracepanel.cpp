#include "tracepanel.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

TracePanel::TracePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(QStringLiteral("执行轨迹 (Function Calling)"), this);
    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("traceView"));
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(500);     // 防长演示内存无界
    m_view->setPlaceholderText(
        QStringLiteral("工具调用与门控状态将在这里实时滚动…"));

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(title);
    lay->addWidget(m_view, 1);
}

void TracePanel::appendUserText(const QString &text)
{
    m_view->appendPlainText(QStringLiteral("[你] %1").arg(text));
}

void TracePanel::appendEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("tool_start")) {
        m_view->appendPlainText(QStringLiteral("▶ %1(%2)")
                                     .arg(event.value(QStringLiteral("name")).toString(),
                                          event.value(QStringLiteral("args")).toString()));
    } else if (type == QStringLiteral("tool_result")) {
        const QString name = event.value(QStringLiteral("name")).toString();
        if (event.value(QStringLiteral("ok")).toBool()) {
            m_view->appendPlainText(QStringLiteral("✔ %1 成功").arg(name));
        } else {
            const QString reason = event.value(QStringLiteral("reason")).toString();
            const QString gate = reason == QStringLiteral("confirm_required")
                                     ? QStringLiteral("门控拦截, 等待用户确认")
                                     : reason;
            m_view->appendPlainText(QStringLiteral("✖ %1 %2").arg(name, gate));
        }
    } else if (type == QStringLiteral("confirm_request")) {
        m_view->appendPlainText(QStringLiteral("🟡 确认卡片: %1")
                                     .arg(event.value(QStringLiteral("display")).toString()));
    } else if (type == QStringLiteral("done")) {
        const QJsonObject usage = event.value(QStringLiteral("usage")).toObject();
        const int prompt = usage.value(QStringLiteral("prompt_tokens")).toInt();
        const int completion = usage.value(QStringLiteral("completion_tokens")).toInt();
        m_view->appendPlainText(QStringLiteral("— 完成 (tokens: prompt %1 + completion %2)")
                                     .arg(prompt).arg(completion));
    } else if (type == QStringLiteral("error")) {
        m_view->appendPlainText(QStringLiteral("⚠ %1")
                                     .arg(event.value(QStringLiteral("message")).toString()));
    }
}

void TracePanel::clear()
{
    m_view->clear();
}
