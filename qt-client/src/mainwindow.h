#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class TcpClient;

/*
 * 主窗口: 持有全局唯一的 TcpClient(与服务端的串行连接)
 * M3: 登录流程入口; M4/M5 将加入车票表格与预约操作
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 弹出模态登录对话框; 登录成功返回 true 并记录用户信息
    bool login();

    TcpClient *client() const { return m_client; }
    QString userTel() const { return m_userTel; }
    QString userName() const { return m_userName; }

private:
    TcpClient *m_client = nullptr;
    QString m_userName;
    QString m_userTel;
};

#endif // MAINWINDOW_H
