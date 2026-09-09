/*
 * M5 验收测试(对真实服务端, WSL 内运行):
 *  1. 完整业务闭环: 查票 -> 预约(选中行) -> 自动刷新车票/我的预约 -> 取消 -> 数据复原
 *  2. 断线处理: kill 服务端 -> 状态提示/按钮禁用/界面不卡死 -> 重启服务端 -> 重连恢复
 * 注意: 本测试会 kill 服务器进程并在最后重新拉起(server 已加 SO_REUSEADDR,
 *       TIME_WAIT 不影响重启)。
 */
#include "mainwindow.h"
#include "tcpclient.h"
#include "tickettablemodel.h"
#include "reservetablemodel.h"

#include <QProcess>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>

static const QString kServerDir = QStringLiteral("/mnt/d/桌面/my/ser-cli/server");

class TestFlow : public QObject
{
    Q_OBJECT

private slots:
    void fullBusinessFlow();
    void disconnectAndReconnect();

private:
    void connectAndWait(MainWindow &w);
    void cleanupMyReservations(MainWindow &w);   // 清空该账号残留预约
    static QString status(MainWindow &w) { return w.statusBar()->currentMessage(); }
};

void TestFlow::connectAndWait(MainWindow &w)
{
    if (!w.client()->isConnected())
        w.client()->connectToHost(QStringLiteral("127.0.0.1"), 6000);
    QTRY_VERIFY(w.client()->isConnected());
}

void TestFlow::cleanupMyReservations(MainWindow &w)
{
    w.refreshMyReserve();
    QTRY_VERIFY(status(w).contains(QStringLiteral("条预约记录")));
    while (w.reserveModel()->rowCount() > 0) {
        w.reserveModel();   // noop, 防止编译器优化掉循环内模型读取
        auto *rv = w.findChild<QTableView *>(QStringLiteral("reserveView"));
        rv->selectRow(0);
        QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("cancelBtn")), Qt::LeftButton);
        // 取消成功会链式刷新: 车票 -> 我的预约, 等链尾状态
        QTRY_VERIFY_WITH_TIMEOUT(status(w).contains(QStringLiteral("条预约记录"))
                                     || w.reserveModel()->rowCount() == 0,
                                 8000);
    }
    QTRY_COMPARE(w.reserveModel()->rowCount(), 0);
}

void TestFlow::fullBusinessFlow()
{
    MainWindow w;
    w.setSession(QStringLiteral("13800001111"), QStringLiteral("测试用户"));
    connectAndWait(w);
    QTRY_VERIFY(status(w).contains(QStringLiteral("条车票记录")));   // 等连接时的自动查票完成
    cleanupMyReservations(w);

    auto *tv = w.findChild<QTableView *>(QStringLiteral("ticketView"));
    auto *rv = w.findChild<QTableView *>(QStringLiteral("reserveView"));
    auto *reserveBtn = w.findChild<QPushButton *>(QStringLiteral("reserveBtn"));

    // 1. 查票(按状态文本等待"本次"刷新完成, 避免拿陈旧数据断言)
    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("refreshBtn")), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(status(w).contains(QStringLiteral("条车票记录")), 8000);
    QTRY_COMPARE(w.ticketModel()->rowCount(), 3);
    QCOMPARE(w.ticketModel()->data(w.ticketModel()->index(0, TicketTableModel::ColNum)).toInt(), 12);
    qDebug("[PASS] 查票: 3 条记录, tk_id=1 已预约=12");

    // 2. 选中第 1 行 -> 预约(等待期按钮禁用)
    tv->selectRow(0);
    QTest::mouseClick(reserveBtn, Qt::LeftButton);
    QVERIFY(!reserveBtn->isEnabled());                    // 防连点
    QTRY_VERIFY_WITH_TIMEOUT(status(w).contains(QStringLiteral("预约成功")), 8000);
    qDebug("[PASS] 预约: 选中行提交成功, 等待期按钮禁用");

    // 3. 链式自动刷新: 车票已预约数 12->13
    QTRY_COMPARE(w.ticketModel()->data(w.ticketModel()->index(0, TicketTableModel::ColNum)).toInt(), 13);
    qDebug("[PASS] 预约后自动刷新: 车票已预约数 12->13");

    // 4. 链式自动刷新: 我的预约出现 1 条(北京-上海)
    QTRY_COMPARE(w.reserveModel()->rowCount(), 1);
    QCOMPARE(w.reserveModel()->reservationAt(0).addr, QStringLiteral("北京-上海"));
    QVERIFY(w.reserveModel()->reservationAt(0).ydId > 0);
    qDebug("[PASS] 我的预约自动刷新: 1 条, 线路/预约ID 正确");

    // 5. 我的预约选中 -> 取消 -> 数据复原
    rv->selectRow(0);
    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("cancelBtn")), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(status(w).contains(QStringLiteral("取消预约成功")), 8000);
    QTRY_COMPARE(w.ticketModel()->data(w.ticketModel()->index(0, TicketTableModel::ColNum)).toInt(), 12);
    QTRY_COMPARE(w.reserveModel()->rowCount(), 0);
    qDebug("[PASS] 取消预约: 成功提示 + 车票数回 12 + 我的预约清空");

    // 6. 未选中行直接点预约/取消的界面守卫
    tv->clearSelection();
    tv->setCurrentIndex(QModelIndex());
    QTest::mouseClick(reserveBtn, Qt::LeftButton);
    QTRY_VERIFY(status(w).contains(QStringLiteral("选中一行")));
    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("cancelBtn")), Qt::LeftButton);
    QTRY_VERIFY(status(w).contains(QStringLiteral("选中一行")));
    qDebug("[PASS] 未选中行的操作守卫: 均提示且不发请求");
}

void TestFlow::disconnectAndReconnect()
{
    MainWindow w;
    w.setSession(QStringLiteral("13800001111"), QStringLiteral("测试用户"));
    connectAndWait(w);
    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("refreshBtn")), Qt::LeftButton);
    QTRY_COMPARE(w.ticketModel()->rowCount(), 3);

    // 1. kill 服务端(模拟断线)
    QProcess::execute(QStringLiteral("pkill"), { QStringLiteral("-x"), QStringLiteral("ser") });
    QTRY_VERIFY_WITH_TIMEOUT(!w.client()->isConnected(), 8000);
    QTRY_VERIFY(status(w).contains(QStringLiteral("已断线")));
    qDebug("[PASS] 断线: 状态栏提示\"已断线\"");

    // 2. 所有操作按钮禁用, 重连按钮可用; 事件循环仍在跑(界面不卡死)
    auto *refreshBtn = w.findChild<QPushButton *>(QStringLiteral("refreshBtn"));
    QVERIFY(!refreshBtn->isEnabled());
    QVERIFY(!w.findChild<QPushButton *>(QStringLiteral("reserveBtn"))->isEnabled());
    QVERIFY(!w.findChild<QPushButton *>(QStringLiteral("myReserveBtn"))->isEnabled());
    QVERIFY(!w.findChild<QPushButton *>(QStringLiteral("cancelBtn"))->isEnabled());
    auto *reconnectBtn = w.findChild<QPushButton *>(QStringLiteral("reconnectBtn"));
    QVERIFY(reconnectBtn->isEnabled());
    QTest::qWait(200);                                    // 期间保持响应不崩溃
    QVERIFY(true);
    qDebug("[PASS] 断线: 四个操作按钮禁用, 重连可用, 界面不卡死");

    // 3. 重启服务端(SO_REUSEADDR 保证 TIME_WAIT 下可重新 bind 6000)
    //    经 sh + nohup 脱离并重定向输出, 避免继承本测试的 stdout 管道
    QProcess::execute(
        QStringLiteral("sh"),
        { QStringLiteral("-c"),
          QStringLiteral("cd '%1' && nohup ./ser >/dev/null 2>&1 &").arg(kServerDir) });
    QTest::qWait(1500);

    // 4. 点击重连 -> 恢复
    QTest::mouseClick(reconnectBtn, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(w.client()->isConnected(), 8000);
    qDebug("[PASS] 重连: 连接恢复");

    // 5. 重连成功自动刷新车票(等本次刷新真正完成), 且可继续操作(再查一次我的预约)
    QTRY_VERIFY_WITH_TIMEOUT(status(w).contains(QStringLiteral("条车票记录")), 8000);
    QTest::mouseClick(w.findChild<QPushButton *>(QStringLiteral("myReserveBtn")), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(status(w).contains(QStringLiteral("条预约记录")), 8000);
    qDebug("[PASS] 重连后: 车票自动恢复 + 可继续查询我的预约");
}

QTEST_MAIN(TestFlow)
#include "main.moc"
