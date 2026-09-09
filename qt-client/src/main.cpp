#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    if (!w.login())     // 未登录/取消则直接退出
        return 0;
    w.show();
    return a.exec();
}
