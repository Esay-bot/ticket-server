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

    // "退出登录"关窗后重新走登录流程(会话在服务层已被 /logout 销毁,
    // 重登拿到新 session_id) —— 覆盖断网重启服务后重新登录的验收场景
    while (true) {
        MainWindow w;
        if (!w.login())                 // 未登录/取消则直接退出
            return 0;
        w.show();
        a.exec();
        if (!w.reloginRequested())
            return 0;
    }
}
