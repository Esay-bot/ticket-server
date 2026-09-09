#include "mainwindow.h"
#include "tcpclient.h"
#include "logindialog.h"

#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_client(new TcpClient(this))
{
    setWindowTitle(QStringLiteral("票务预约系统"));
    resize(900, 600);

    connect(m_client, &TcpClient::connStateChanged, this, [this](bool up) {
        statusBar()->showMessage(up ? QStringLiteral("已连接")
                                    : QStringLiteral("已断线"));
    });

    statusBar()->showMessage(QStringLiteral("未连接"));
}

MainWindow::~MainWindow()
{
}

bool MainWindow::login()
{
    LoginDialog dlg(m_client, this);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    m_userName = dlg.userName();
    m_userTel = dlg.userTel();
    setWindowTitle(QStringLiteral("票务预约系统 - %1").arg(m_userName));
    statusBar()->showMessage(QStringLiteral("已连接 - %1 (%2)").arg(m_userName, m_userTel));
    return true;
}
