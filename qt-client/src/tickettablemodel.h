#ifndef TICKETTABLEMODEL_H
#define TICKETTABLEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QJsonObject>

/*
 * 车票表格模型(M4 核心)
 *
 * 为什么用 QAbstractTableModel + QTableView 而不是 QTableWidget:
 *  - Model/View 分离: 数据(Ticket 列表)与展示(视图)解耦, 换视图零成本
 *  - QTableWidget 内部是 QTableWidgetItem(每格一个堆对象), 大数据量下
 *    内存与刷新开销高; 自定义 Model 只存一份结构体数组
 *  - 数据变化走标准信号(rowCount/data 由视图按需拉取), 视图自动增量重绘
 *
 * 数据更新策略: 整表替换(刷新场景数据全量来自服务端)用
 * beginResetModel/endResetModel, 语义清晰且不易漏发信号;
 * 若是高频局部更新才值得精细到 dataChanged/insertRows。
 */
struct Ticket
{
    int tkId = 0;
    QString addr;
    int max = 0;
    int num = 0;       // 已预约
    QString useDate;
};

class TicketTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { ColTkId = 0, ColAddr, ColMax, ColNum, ColDate, ColCount };

    explicit TicketTableModel(QObject *parent = nullptr);

    // ---- 只读模型的三个必须实现 ----
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // 整表替换(带 beginResetModel/endResetModel 通知视图重绘)
    void setTickets(const QVector<Ticket> &tickets);

    const QVector<Ticket> &tickets() const { return m_tickets; }
    Ticket ticketAt(int row) const;                 // 越界返回默认值
    int rowOfTkId(int tkId) const;                  // 供"选中行->预约"换算, 无则 -1

    // 从查票响应 {status,num,arr:[...]} 解析车票数组(防御性: 字段缺失/类型不符容忍)
    static QVector<Ticket> fromJson(const QJsonObject &resp);

private:
    QVector<Ticket> m_tickets;
};

#endif // TICKETTABLEMODEL_H
