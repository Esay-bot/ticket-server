/*
 * M4 测试: TicketTableModel 单元测试 + 真实服务端查票(经 MainWindow 全链路)
 * 单元部分不需要服务端; 集成部分需要 127.0.0.1:6000 与种子数据(3 条车票)。
 */
#include "tickettablemodel.h"
#include "mainwindow.h"
#include "tcpclient.h"

#include <QJsonArray>
#include <QHeaderView>
#include <QTableView>
#include <QPushButton>
#include <QtTest>

class TestTicketModel : public QObject
{
    Q_OBJECT

private slots:
    void modelBasics();
    void resetNotifiesView();
    void fromJsonDefensive();
    void viewModelDestructionOrder();
    void liveViewThroughMainWindow();

private:
    static QJsonObject ticketJson(const char *id, const char *addr,
                                  const char *max, const char *num, const char *date);
};

QJsonObject TestTicketModel::ticketJson(const char *id, const char *addr,
                                        const char *max, const char *num, const char *date)
{
    // 与服务端一致: 全部字段为 JSON 字符串
    QJsonObject o;
    o.insert(QStringLiteral("tk_id"), QString::fromUtf8(id));
    o.insert(QStringLiteral("addr"), QString::fromUtf8(addr));
    o.insert(QStringLiteral("max"), QString::fromUtf8(max));
    o.insert(QStringLiteral("num"), QString::fromUtf8(num));
    o.insert(QStringLiteral("use_date"), QString::fromUtf8(date));
    return o;
}

void TestTicketModel::modelBasics()
{
    TicketTableModel m;

    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.columnCount(), TicketTableModel::ColCount);
    QCOMPARE(m.headerData(TicketTableModel::ColTkId, Qt::Horizontal).toString(),
             QStringLiteral("编号"));
    QCOMPARE(m.headerData(TicketTableModel::ColAddr, Qt::Horizontal).toString(),
             QStringLiteral("线路"));
    QCOMPARE(m.headerData(TicketTableModel::ColMax, Qt::Horizontal).toString(),
             QStringLiteral("总票数"));
    QCOMPARE(m.headerData(TicketTableModel::ColNum, Qt::Horizontal).toString(),
             QStringLiteral("已预约"));
    QCOMPARE(m.headerData(TicketTableModel::ColDate, Qt::Horizontal).toString(),
             QStringLiteral("日期"));
    QVERIFY(!m.headerData(0, Qt::Vertical).isValid());        // 行号列不用默认内容
    QVERIFY(!m.data(QModelIndex()).isValid());               // 无效索引防御

    QVector<Ticket> ts;
    Ticket t1; t1.tkId = 1; t1.addr = QStringLiteral("北京-上海");
    t1.max = 100; t1.num = 12; t1.useDate = QStringLiteral("2026-09-20");
    Ticket t2; t2.tkId = 2; t2.addr = QStringLiteral("北京-广州");
    t2.max = 80; t2.num = 5; t2.useDate = QStringLiteral("2026-09-21");
    ts << t1 << t2;
    m.setTickets(ts);

    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(0, TicketTableModel::ColTkId)).toInt(), 1);
    QCOMPARE(m.data(m.index(0, TicketTableModel::ColAddr)).toString(), QStringLiteral("北京-上海"));
    QCOMPARE(m.data(m.index(0, TicketTableModel::ColMax)).toInt(), 100);
    QCOMPARE(m.data(m.index(1, TicketTableModel::ColNum)).toInt(), 5);
    QCOMPARE(m.data(m.index(1, TicketTableModel::ColDate)).toString(), QStringLiteral("2026-09-21"));
    QCOMPARE(m.data(m.index(1, TicketTableModel::ColAddr),
                    Qt::TextAlignmentRole).toInt(),
             int(Qt::AlignVCenter | Qt::AlignLeft));         // 文本列左对齐
    QVERIFY(!m.data(m.index(0, 0), Qt::DecorationRole).isValid());
    QVERIFY(!m.data(m.index(99, 0)).isValid());              // 越界防御
    QCOMPARE(m.rowOfTkId(2), 1);
    QCOMPARE(m.rowOfTkId(42), -1);
    QCOMPARE(m.ticketAt(1).addr, QStringLiteral("北京-广州"));
    QCOMPARE(m.ticketAt(7).tkId, 0);                         // 越界返回默认值
    qDebug("[PASS] 模型基础: 行列数/表头/DisplayRole/对齐/越界/rowOfTkId");
}

void TestTicketModel::resetNotifiesView()
{
    // setTickets 必须触发 modelAboutToBeReset/modelReset, 否则视图不会重绘
    TicketTableModel m;
    QSignalSpy aboutReset(&m, &QAbstractTableModel::modelAboutToBeReset);
    QSignalSpy reset(&m, &QAbstractTableModel::modelReset);
    QSignalSpy layoutChanged(&m, &QAbstractTableModel::layoutChanged);

    QVector<Ticket> ts;
    Ticket t; t.tkId = 9; ts << t;
    m.setTickets(ts);

    QCOMPARE(aboutReset.count(), 1);
    QCOMPARE(reset.count(), 1);
    QCOMPARE(layoutChanged.count(), 0);                      // 走 reset 而非 layout 信号
    QCOMPARE(m.rowCount(), 1);
    qDebug("[PASS] 数据更新: beginResetModel/endResetModel 成对发出");
}

void TestTicketModel::fromJsonDefensive()
{
    // 正常响应(字符串数值)
    QJsonObject ok;
    ok.insert(QStringLiteral("status"), QStringLiteral("OK"));
    QJsonArray arr;
    arr.append(ticketJson("1", "北京-上海", "100", "12", "2026-09-20"));
    arr.append(ticketJson("2", "北京-广州", "80", "5", "2026-09-21"));
    ok.insert(QStringLiteral("arr"), arr);
    const QVector<Ticket> ts = TicketTableModel::fromJson(ok);
    QCOMPARE(ts.size(), 2);
    QCOMPARE(ts.at(0).tkId, 1);                              // 字符串 "1" 正确转数值
    QCOMPARE(ts.at(0).max, 100);
    QCOMPARE(ts.at(1).num, 5);

    // 数字型数值也兼容(防御未来服务端改序列化方式)
    QJsonObject o2;
    o2.insert(QStringLiteral("tk_id"), 7);
    o2.insert(QStringLiteral("addr"), QStringLiteral("X-Y"));
    QJsonObject resp2;
    QJsonArray a2; a2.append(o2);
    resp2.insert(QStringLiteral("arr"), a2);
    QCOMPARE(TicketTableModel::fromJson(resp2).at(0).tkId, 7);

    // 字段缺失 -> 默认值; 非对象元素 -> 跳过; 无 arr -> 空表
    QJsonObject resp3;
    QJsonArray a3;
    a3.append(QJsonObject());                                // 空对象
    a3.append(QStringLiteral("garbage"));                    // 非对象
    resp3.insert(QStringLiteral("arr"), a3);
    QCOMPARE(TicketTableModel::fromJson(resp3).size(), 1);
    QCOMPARE(TicketTableModel::fromJson(resp3).at(0).tkId, 0);
    QVERIFY(TicketTableModel::fromJson(QJsonObject()).isEmpty());
    qDebug("[PASS] fromJson 防御: 字符串/数字数值/缺失字段/脏元素/无 arr");
}

void TestTicketModel::viewModelDestructionOrder()
{
    // 复现窗口析构: model 与 view 同为窗口子对象, model 先创建(先销毁)
    QMainWindow w;
    auto *model = new TicketTableModel(&w);
    auto *view = new QTableView(&w);
    view->setModel(model);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->horizontalHeader()->setStretchLastSection(true);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    w.show();

    QVector<Ticket> ts;
    for (int i = 0; i < 3; ++i) {
        Ticket t; t.tkId = i; t.addr = QStringLiteral("L%1").arg(i);
        t.max = 10; t.num = i; t.useDate = QStringLiteral("2026-01-0%1").arg(i + 1);
        ts << t;
    }
    model->setTickets(ts);
    QTest::qWait(50);
    qDebug("[PASS] 析构顺序用例执行完毕(未崩溃即通过)");
}

void TestTicketModel::liveViewThroughMainWindow()
{
    // 全链路: MainWindow.setSession -> refreshTickets(点按钮) -> 真实服务端 -> 模型更新
    MainWindow w;
    w.setSession(QStringLiteral("13800001111"), QStringLiteral("测试用户"));
    w.client()->connectToHost(QStringLiteral("127.0.0.1"), 6000);
    QTRY_VERIFY(w.client()->isConnected());

    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("refreshBtn")), Qt::LeftButton);
    QTRY_COMPARE(w.ticketModel()->rowCount(), 3);            // 种子数据 3 条
    QCOMPARE(w.ticketModel()->data(w.ticketModel()->index(0, TicketTableModel::ColAddr)).toString(),
             QStringLiteral("北京-上海"));
    QCOMPARE(w.ticketModel()->data(w.ticketModel()->index(0, TicketTableModel::ColNum)).toInt(), 12);
    QCOMPARE(w.ticketModel()->data(w.ticketModel()->index(2, TicketTableModel::ColAddr)).toString(),
             QStringLiteral("北京-成都"));

    // 等待期按钮禁用(防连点), 响应后恢复
    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("refreshBtn")), Qt::LeftButton);
    QVERIFY(!w.findChild<QPushButton *>(QStringLiteral("refreshBtn"))->isEnabled());
    QTRY_VERIFY(w.findChild<QPushButton *>(QStringLiteral("refreshBtn"))->isEnabled());
    qDebug("[PASS] 真实服务端全链路: 刷新按钮 -> 3 条车票入表, 等待期按钮禁用");
}

QTEST_MAIN(TestTicketModel)
#include "main.moc"
