#include "mainwindow.h"
#include "tcpclient.h"
#include "logindialog.h"
#include "tickettablemodel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_client(new TcpClient(this)),
      m_model(new TicketTableModel(this))
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

    m_refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    m_refreshBtn->setObjectName(QStringLiteral("refreshBtn"));

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_refreshBtn);
    btnRow->addStretch();

    m_view = new QTableView(this);
    m_view->setObjectName(QStringLiteral("ticketView"));
    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);   // 整行选中, 便于"选中->预约"
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);    // 只读展示
    m_view->setAlternatingRowColors(true);
    m_view->verticalHeader()->hide();
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->addLayout(btnRow);
    lay->addWidget(m_view);
    setCentralWidget(central);

    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshTickets);

    statusBar()->showMessage(QStringLiteral("未连接"));
    updateUiState();
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

void MainWindow::refreshTickets()
{
    if (m_pendingReq != 0)
        return;                          // 串行协议: 上一请求未完成, 忽略
    if (!m_client->isConnected()) {
        statusBar()->showMessage(QStringLiteral("未连接服务器，无法查询"), 3000);
        return;
    }

    QJsonObject req;
    req.insert(QStringLiteral("type"), 3);
    if (!m_client->send(req)) {
        statusBar()->showMessage(QStringLiteral("发送查票请求失败"), 3000);
        return;
    }
    m_pendingReq = 3;
    statusBar()->showMessage(QStringLiteral("正在查询车票..."));
    updateUiState();
}

void MainWindow::onJsonReceived(const QJsonObject &obj)
{
    switch (m_pendingReq) {
    case 3:
        handleViewResp(obj);
        break;
    default:
        break;                           // 非主窗口请求(如登录对话框)的响应, 忽略
    }
}

void MainWindow::handleViewResp(const QJsonObject &obj)
{
    m_pendingReq = 0;

    if (obj.value(QStringLiteral("status")).toString() == QStringLiteral("OK")) {
        m_model->setTickets(TicketTableModel::fromJson(obj));
        statusBar()->showMessage(
            QStringLiteral("共 %1 条车票记录").arg(m_model->rowCount()));
    } else {
        statusBar()->showMessage(QStringLiteral("查询车票失败"), 3000);
    }
    updateUiState();
}

void MainWindow::onConnStateChanged(bool up)
{
    if (!up && m_pendingReq != 0) {
        m_pendingReq = 0;                // 断线使未完成请求作废
    }
    statusBar()->showMessage(up ? QStringLiteral("已连接 - %1 (%2)").arg(m_userName, m_userTel)
                                : QStringLiteral("已断线"));
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
    // 控件可用性 = 无未完成请求 && 连接正常(M5 会把预约/取消按钮并入此逻辑)
    const bool idle = (m_pendingReq == 0);
    const bool up = m_client->isConnected();
    m_refreshBtn->setEnabled(idle && up);
}
