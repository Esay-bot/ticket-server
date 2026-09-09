#include "reservetablemodel.h"
#include "jsonutil.h"

#include <QJsonArray>

ReserveTableModel::ReserveTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ReserveTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_list.size();
}

int ReserveTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant ReserveTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_list.size())
        return QVariant();

    if (role == Qt::TextAlignmentRole) {
        return (index.column() == ColAddr)
                   ? QVariant(int(Qt::AlignVCenter | Qt::AlignLeft))
                   : QVariant(int(Qt::AlignCenter));
    }
    if (role != Qt::DisplayRole)
        return QVariant();

    const Reservation &r = m_list.at(index.row());
    switch (index.column()) {
    case ColYdId: return r.ydId;
    case ColAddr: return r.addr;
    case ColDate: return r.useDate;
    }
    return QVariant();
}

QVariant ReserveTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case ColYdId: return QStringLiteral("预约ID");
    case ColAddr: return QStringLiteral("线路");
    case ColDate: return QStringLiteral("日期");
    }
    return QVariant();
}

void ReserveTableModel::setReservations(const QVector<Reservation> &list)
{
    beginResetModel();
    m_list = list;
    endResetModel();
}

Reservation ReserveTableModel::reservationAt(int row) const
{
    return (row >= 0 && row < m_list.size()) ? m_list.at(row) : Reservation();
}

QVector<Reservation> ReserveTableModel::fromJson(const QJsonObject &resp)
{
    QVector<Reservation> out;
    const QJsonValue arrVal = resp.value(QStringLiteral("arr"));
    if (!arrVal.isArray())
        return out;

    const QJsonArray arr = arrVal.toArray();
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        Reservation r;
        r.ydId    = JsonUtil::asInt(o, QStringLiteral("yd_id"));
        r.addr    = JsonUtil::asStr(o, QStringLiteral("addr"));
        r.useDate = JsonUtil::asStr(o, QStringLiteral("use_date"));
        out.append(r);
    }
    return out;
}
