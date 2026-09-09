#include "mainwindow.h"
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("票务预约系统"));
    resize(900, 600);
    statusBar()->showMessage(QStringLiteral("未连接"));
}

MainWindow::~MainWindow()
{
}
