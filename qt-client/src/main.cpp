#include "mainwindow.h"
#include <QApplication>
#include <QFile>

// 从 qrc 资源加载统一样式表; 样式文件: res/style.qss
static void loadStyleSheet(QApplication &app)
{
    QFile f(QStringLiteral(":/style.qss"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(f.readAll()));
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    loadStyleSheet(a);

    MainWindow w;
    if (!w.login())     // 未登录/取消则直接退出
        return 0;
    w.show();
    return a.exec();
}
