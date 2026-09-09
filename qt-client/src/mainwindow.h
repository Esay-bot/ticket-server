#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

/*
 * 主窗口: M0 阶段仅为空窗口骨架
 * 后续模块将逐步加入: 车票表格(M4)、预约操作区(M5)、断线状态栏(M5)
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
};

#endif // MAINWINDOW_H
