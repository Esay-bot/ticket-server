#include "tickettablemodel.h"
#include "jsonutil.h"

#include <QJsonArray>

TicketTableModel::TicketTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TicketTableModel::rowCount(const QModelIndex &parent) const
{
    // 表格模型没有层级子节点, 父索引有效时为 0(树形才用得到)
    return parent.isValid() ? 0 : m_tickets.size();
}

int TicketTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant TicketTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tickets.size())
        return QVariant();                       // 越界防御: 返回无效 QVariant

    if (role == Qt::TextAlignmentRole) {
        // 数值列居中, 文本列左对齐
        return (index.column() == ColAddr || index.column() == ColDate)
                   ? QVariant(int(Qt::AlignVCenter | Qt::AlignLeft))
                   : QVariant(int(Qt::AlignCenter));
    }

    if (role != Qt::DisplayRole)
        return QVariant();                       // 其他角色(编辑/装饰等)不支持

    const Ticket &t = m_tickets.at(index.row());
    switch (index.column()) {
    case ColTkId:  return t.tkId;
    case ColAddr:  return t.addr;
    case ColMax:   return t.max;
    case ColNum:   return t.num;
    case ColDate:  return t.useDate;
    }
    return QVariant();
}

QVariant TicketTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColTkId:  return QStringLiteral("编号");
        case ColAddr:  return QStringLiteral("线路");
        case ColMax:   return QStringLiteral("总票数");
        case ColNum:   return QStringLiteral("已预约");
        case ColDate:  return QStringLiteral("日期");
        }
    }
    return QVariant();                           // 垂直行号交给默认实现(空白)
}

void TicketTableModel::setTickets(const QVector<Ticket> &tickets)
{
    beginResetModel();                           // 通知视图: 即将整表换数据
    m_tickets = tickets;
    endResetModel();                             // 视图自动重新拉取 rowCount/data
}

Ticket TicketTableModel::ticketAt(int row) const
{
    return (row >= 0 && row < m_tickets.size()) ? m_tickets.at(row) : Ticket();
}

int TicketTableModel::rowOfTkId(int tkId) const
{
    for (int i = 0; i < m_tickets.size(); ++i)
        if (m_tickets.at(i).tkId == tkId)
            return i;
    return -1;
}

QVector<Ticket> TicketTableModel::fromHttpArray(const QJsonArray &arr)
{
    QVector<Ticket> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        Ticket t;
        t.tkId    = JsonUtil::asInt(o, QStringLiteral("tk_id"));
        t.addr    = JsonUtil::asStr(o, QStringLiteral("addr"));
        t.max     = JsonUtil::asInt(o, QStringLiteral("total"));
        t.num     = JsonUtil::asInt(o, QStringLiteral("used"));
        t.useDate = JsonUtil::asStr(o, QStringLiteral("use_date"));
        out.append(t);
    }
    return out;
}

QVector<Ticket> TicketTableModel::fromJson(const QJsonObject &resp)
{
    QVector<Ticket> out;
    const QJsonValue arrVal = resp.value(QStringLiteral("arr"));
    if (!arrVal.isArray())
        return out;                              // 缺 arr 字段: 容忍, 视为空表

    const QJsonArray arr = arrVal.toArray();
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;                            // 脏数据跳过, 不中断整表
        const QJsonObject o = v.toObject();
        Ticket t;
        t.tkId    = JsonUtil::asInt(o, QStringLiteral("tk_id"));
        t.addr    = JsonUtil::asStr(o, QStringLiteral("addr"));
        t.max     = JsonUtil::asInt(o, QStringLiteral("max"));
        t.num     = JsonUtil::asInt(o, QStringLiteral("num"));
        t.useDate = JsonUtil::asStr(o, QStringLiteral("use_date"));
        out.append(t);
    }
    return out;
}
