#include "mainwindow.h"
#include "apiclient.h"
#include "chatwidget.h"
#include "logindialog.h"
#include "tickettablemodel.h"
#include "reservetablemodel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(const QUrl &apiBaseUrl, QWidget *parent)
    : QMainWindow(parent),
      m_api(apiBaseUrl.isValid() && !apiBaseUrl.isEmpty()
                ? new ApiClient(apiBaseUrl, this)
                : new ApiClient(this)),
      m_chat(new ChatWidget(m_api, this)),
      m_ticketModel(new TicketTableModel(this)),
      m_reserveModel(new ReserveTableModel(this))
{
    buildUi();

    connect(m_api, &ApiClient::ticketsFinished, this, &MainWindow::onTicketsFinished);
    connect(m_api, &ApiClient::reservationsFinished, this, &MainWindow::onReservationsFinished);
    // 每轮对话结束后自动刷新两张表格: 余票/预约随对话实时变化(演示要点)
    connect(m_api, &ApiClient::chatFinished, this, [this](bool ok) {
        if (ok && m_api->hasSession()) {
            refreshTickets();
            refreshMyReserve();
        }
    });
}

MainWindow::~MainWindow()
{
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("票务预约系统"));
    resize(900, 600);

    m_refreshBtn    = new QPushButton(QStringLiteral("刷新车票"), this);
    m_refreshBtn->setObjectName(QStringLiteral("refreshBtn"));
    m_myReserveBtn  = new QPushButton(QStringLiteral("刷新我的预约"), this);
    m_myReserveBtn->setObjectName(QStringLiteral("myReserveBtn"));
    m_logoutBtn     = new QPushButton(QStringLiteral("退出登录"), this);
    m_logoutBtn->setObjectName(QStringLiteral("logoutBtn"));

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_refreshBtn);
    btnRow->addWidget(m_myReserveBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_logoutBtn);

    // 车票表
    m_ticketView = makeView(QStringLiteral("ticketView"), m_ticketModel);
    // 我的预约表
    m_reserveView = makeView(QStringLiteral("reserveView"), m_reserveModel);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(m_chat, QStringLiteral("AI 助手"));
    tabs->addTab(m_ticketView, QStringLiteral("车票列表"));
    tabs->addTab(m_reserveView, QStringLiteral("我的预约"));

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->addLayout(btnRow);
    lay->addWidget(tabs);
    setCentralWidget(central);

    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshTickets);
    connect(m_myReserveBtn, &QPushButton::clicked, this, &MainWindow::refreshMyReserve);
    connect(m_logoutBtn, &QPushButton::clicked, this, &MainWindow::logoutAndRelogin);

    statusBar()->showMessage(QStringLiteral("未登录"));
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
    LoginDialog dlg(m_api, this);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    setSession(m_api->sessionId(), m_api->userTel(), m_api->userName());
    statusBar()->showMessage(QStringLiteral("已登录 - %1 (%2)")
                                 .arg(m_api->userName(), m_api->userTel()));
    refreshTickets();                    // 登录成功即拉取车票列表
    return true;
}

void MainWindow::setSession(const QString &sessionId, const QString &tel,
                            const QString &name)
{
    m_api->adoptSession(sessionId, tel, name);
    setWindowTitle(QStringLiteral("票务预约系统 - %1").arg(name));
}

// ---- 数据刷新 --------------------------------------------------------------

void MainWindow::refreshTickets()
{
    if (!m_api->hasSession())
        return;
    m_api->fetchTickets();
    statusBar()->showMessage(QStringLiteral("正在查询车票..."), 3000);
}

void MainWindow::refreshMyReserve()
{
    if (!m_api->hasSession())
        return;
    m_api->fetchReservations();
    statusBar()->showMessage(QStringLiteral("正在查询我的预约..."), 3000);
}

void MainWindow::onTicketsFinished(bool ok, const QJsonArray &tickets,
                                   const QString &message)
{
    if (!ok) {
        statusBar()->showMessage(message, 6000);
        return;
    }
    m_ticketModel->setTickets(TicketTableModel::fromHttpArray(tickets));
    statusBar()->showMessage(QStringLiteral("共 %1 条车票记录")
                                 .arg(m_ticketModel->rowCount()));
}

void MainWindow::onReservationsFinished(bool ok, const QJsonArray &reservations,
                                        const QString &message)
{
    if (!ok) {
        statusBar()->showMessage(message, 6000);
        return;
    }
    m_reserveModel->setReservations(ReserveTableModel::fromHttpArray(reservations));
    statusBar()->showMessage(QStringLiteral("共 %1 条预约记录")
                                 .arg(m_reserveModel->rowCount()));
}

// ---- 退出/重登 -------------------------------------------------------------

void MainWindow::logoutAndRelogin()
{
    if (m_api->hasSession())
        m_api->logout();                 // 服务层关 TCP 并移除会话
    m_reloginRequested = true;
    close();                             // main.cpp 收到关窗后重新走登录
}
