#ifndef RESERVETABLEMODEL_H
#define RESERVETABLEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QJsonObject>

/*
 * "我的预约"表格模型: 与 TicketTableModel 同一套路(QAbstractTableModel)
 * 列: 预约ID / 线路 / 日期
 */
struct Reservation
{
    int ydId = 0;
    QString addr;
    QString useDate;
};

class ReserveTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { ColYdId = 0, ColAddr, ColDate, ColCount };

    explicit ReserveTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setReservations(const QVector<Reservation> &list);

    const QVector<Reservation> &reservations() const { return m_list; }
    Reservation reservationAt(int row) const;          // 越界返回默认值

    // 从"我的预约"响应 {status,num,arr:[{yd_id,addr,use_date}]} 解析(防御性)
    static QVector<Reservation> fromJson(const QJsonObject &resp);

private:
    QVector<Reservation> m_list;
};

#endif // RESERVETABLEMODEL_H
