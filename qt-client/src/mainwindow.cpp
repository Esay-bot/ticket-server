#include "mainwindow.h"
#include "tcpclient.h"
#include "logindialog.h"
#include "tickettablemodel.h"
#include "reservetablemodel.h"
#include "appconfig.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_client(new TcpClient(this)),
      m_ticketModel(new TicketTableModel(this)),
      m_reserveModel(new ReserveTableModel(this))
{
    buildUi();

    connect(m_client, &TcpClient::jsonReceived, this, &MainWindow::onJsonReceived);
    connect(m_client, &TcpClient::connStateChanged, this, &MainWindow::onConnStateChanged);
    connect(m_client, &TcpClient::errorOccurred, this, &MainWindow::onConnError);
}

MainWindow::~MainWindow()
{
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("票务预约系统"));
    resize(900, 600);

    m_refreshBtn    = new QPushButton(QStringLiteral("刷新"), this);
    m_refreshBtn->setObjectName(QStringLiteral("refreshBtn"));
    m_reserveBtn    = new QPushButton(QStringLiteral("预约选中车票"), this);
    m_reserveBtn->setObjectName(QStringLiteral("reserveBtn"));
    m_myReserveBtn  = new QPushButton(QStringLiteral("我的预约"), this);
    m_myReserveBtn->setObjectName(QStringLiteral("myReserveBtn"));
    m_cancelBtn     = new QPushButton(QStringLiteral("取消选中预约"), this);
    m_cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
    m_reconnectBtn  = new QPushButton(QStringLiteral("重连"), this);
    m_reconnectBtn->setObjectName(QStringLiteral("reconnectBtn"));

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_refreshBtn);
    btnRow->addWidget(m_reserveBtn);
    btnRow->addWidget(m_myReserveBtn);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_reconnectBtn);

    // 车票表
    m_ticketView = makeView(QStringLiteral("ticketView"), m_ticketModel);
    // 我的预约表
    m_reserveView = makeView(QStringLiteral("reserveView"), m_reserveModel);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(m_ticketView, QStringLiteral("车票列表"));
    tabs->addTab(m_reserveView, QStringLiteral("我的预约"));

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->addLayout(btnRow);
    lay->addWidget(tabs);
    setCentralWidget(central);

    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshTickets);
    connect(m_reserveBtn, &QPushButton::clicked, this, &MainWindow::reserveSelected);
    connect(m_myReserveBtn, &QPushButton::clicked, this, &MainWindow::refreshMyReserve);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancelSelected);
    connect(m_reconnectBtn, &QPushButton::clicked, this, &MainWindow::reconnect);

    statusBar()->showMessage(QStringLiteral("未连接"));
    updateUiState();
}

QTableView *MainWindow::makeView(const QString &name, QAbstractItemModel *model)
{
    auto *view = new QTableView(this);
    view->setObjectName(name);
    view->setModel(model);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);   // 整行选中 -> 行号即数据下标
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(true);
    view->verticalHeader()->hide();
    view->horizontalHeader()->setStretchLastSection(true);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return view;
}

bool MainWindow::login()
{
    LoginDialog dlg(m_client, this);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    setSession(dlg.userTel(), dlg.userName());
    refreshTickets();                    // 登录成功即拉取车票列表
    return true;
}

void MainWindow::setSession(const QString &tel, const QString &name)
{
    m_userTel = tel;
    m_userName = name;
    setWindowTitle(QStringLiteral("票务预约系统 - %1").arg(m_userName));
}

// ---- 请求发送(统一守卫: 串行未完成/连接态/发送结果) ----

bool MainWindow::sendRequest(int type, const QJsonObject &req)
{
    if (m_pendingReq != 0)
        return false;                    // 串行协议: 未完成请求期间不再发
    if (!m_client->isConnected()) {
        statusBar()->showMessage(QStringLiteral("未连接服务器，请点击\"重连\""), 3000);
        return false;
    }
    if (!m_client->send(req)) {
        statusBar()->showMessage(QStringLiteral("发送失败，请重试"), 3000);
        return false;
    }
    m_pendingReq = type;
    updateUiState();
    return true;
}

void MainWindow::refreshTickets()
{
    QJsonObject req;
    req.insert(QStringLiteral("type"), 3);
    if (sendRequest(3, req))
        statusBar()->showMessage(QStringLiteral("正在查询车票..."));
}

void MainWindow::refreshMyReserve()
{
    QJsonObject req;
    req.insert(QStringLiteral("type"), 5);
    req.insert(QStringLiteral("tel"), m_userTel);
    if (sendRequest(5, req))
        statusBar()->showMessage(QStringLiteral("正在查询我的预约..."));
}

void MainWindow::reserveSelected()
{
    const QModelIndex idx = m_ticketView->currentIndex();
    if (!idx.isValid()) {
        statusBar()->showMessage(QStringLiteral("请先在车票列表选中一行"), 3000);
        return;
    }
    const Ticket t = m_ticketModel->ticketAt(idx.row());

    QJsonObject req;
    req.insert(QStringLiteral("type"), 4);
    req.insert(QStringLiteral("tel"), m_userTel);
    req.insert(QStringLiteral("index"), t.tkId);       // index = 票的 tk_id
    if (sendRequest(4, req))
        statusBar()->showMessage(QStringLiteral("正在提交预约(%1)...").arg(t.addr));
}

void MainWindow::cancelSelected()
{
    const QModelIndex idx = m_reserveView->currentIndex();
    if (!idx.isValid()) {
        statusBar()->showMessage(QStringLiteral("请先在\"我的预约\"选中一行"), 3000);
        return;
    }
    const Reservation r = m_reserveModel->reservationAt(idx.row());

    QJsonObject req;
    req.insert(QStringLiteral("type"), 6);
    req.insert(QStringLiteral("tel"), m_userTel);
    req.insert(QStringLiteral("index"), r.ydId);       // index = 预约的 yd_id
    if (sendRequest(6, req))
        statusBar()->showMessage(QStringLiteral("正在取消预约(%1)...").arg(r.addr));
}

void MainWindow::reconnect()
{
    if (m_client->isConnected())
        return;
    statusBar()->showMessage(QStringLiteral("正在重连服务器..."));
    m_client->connectToHost(AppConfig::kServerHost, AppConfig::kServerPort);
}

// ---- 响应处理 ----

void MainWindow::onJsonReceived(const QJsonObject &obj)
{
    const int type = m_pendingReq;
    m_pendingReq = 0;

    switch (type) {
    case 3: handleViewResp(obj); break;
    case 4: handleReserveResp(obj); break;
    case 5: handleMyReserveResp(obj); break;
    case 6: handleCancelResp(obj); break;
    default: break;                      // 非本窗口请求(登录对话框期间), 忽略
    }
    updateUiState();
}

void MainWindow::handleViewResp(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("status")).toString() != QStringLiteral("OK")) {
        statusBar()->showMessage(QStringLiteral("查询车票失败"), 3000);
        return;
    }
    m_ticketModel->setTickets(TicketTableModel::fromJson(obj));
    statusBar()->showMessage(notePrefix() + QStringLiteral("共 %1 条车票记录")
                                 .arg(m_ticketModel->rowCount()));

    // 预约/取消成功后的链式刷新第二步: 车票表更新完再拉我的预约(串行协议逐个发)
    if (m_chainMyReserve) {
        m_chainMyReserve = false;
        refreshMyReserve();
    }
}

void MainWindow::handleReserveResp(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("status")).toString() == QStringLiteral("OK")) {
        statusBar()->showMessage(QStringLiteral("预约成功"), 4000);
        m_actionNote = QStringLiteral("预约成功");
        m_chainMyReserve = true;
        refreshTickets();                // 自动刷新: 先车票(已预约数), 再我的预约
    } else {
        statusBar()->showMessage(QStringLiteral("预约失败（车票可能已约满）"), 4000);
    }
}

void MainWindow::handleMyReserveResp(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("status")).toString() != QStringLiteral("OK")) {
        statusBar()->showMessage(QStringLiteral("查询我的预约失败"), 3000);
        return;
    }
    m_reserveModel->setReservations(ReserveTableModel::fromJson(obj));
    // 链式刷新的最后一步: 操作结果提示在此定格(如"预约成功 · 共 1 条预约记录")
    statusBar()->showMessage(notePrefix() + QStringLiteral("共 %1 条预约记录")
                                 .arg(m_reserveModel->rowCount()));
    m_actionNote.clear();
}

void MainWindow::handleCancelResp(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("status")).toString() == QStringLiteral("OK")) {
        statusBar()->showMessage(QStringLiteral("取消预约成功"), 4000);
        m_actionNote = QStringLiteral("取消预约成功");
        m_chainMyReserve = true;
        refreshTickets();
    } else {
        statusBar()->showMessage(QStringLiteral("取消失败（预约记录可能已不存在）"), 4000);
    }
}

// ---- 连接状态 ----

void MainWindow::onConnStateChanged(bool up)
{
    if (!up) {
        // 断线: 未完成请求作废, 全部操作按钮禁用(见 updateUiState), 界面保持响应
        m_pendingReq = 0;
        m_chainMyReserve = false;
        m_actionNote.clear();
        statusBar()->showMessage(QStringLiteral("已断线：请点击\"重连\""));
    } else {
        statusBar()->showMessage(QStringLiteral("已连接 - %1 (%2)").arg(m_userName, m_userTel));
        if (!m_userTel.isEmpty())
            refreshTickets();            // 重连成功自动恢复车票列表
    }
    updateUiState();
}

void MainWindow::onConnError(const QString &reason)
{
    if (m_pendingReq != 0) {
        m_pendingReq = 0;                // 超时/协议错: 请求作废, 解锁界面
        statusBar()->showMessage(QStringLiteral("请求失败：%1").arg(reason), 5000);
    }
    updateUiState();
}

void MainWindow::updateUiState()
{
    // 操作按钮 = 无未完成请求 && 已连接; 重连按钮 = 未连接时可用
    const bool idle = (m_pendingReq == 0);
    const bool up = m_client->isConnected();
    m_refreshBtn->setEnabled(idle && up);
    m_reserveBtn->setEnabled(idle && up);
    m_myReserveBtn->setEnabled(idle && up);
    m_cancelBtn->setEnabled(idle && up);
    m_reconnectBtn->setEnabled(idle && !up);
}

QString MainWindow::notePrefix() const
{
    return m_actionNote.isEmpty() ? QString() : m_actionNote + QStringLiteral(" · ");
}
