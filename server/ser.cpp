#include "ser.h"
#include <memory>
#include <cstring>
#include <cerrno>
#include "log.h"
#include "mysql_conn_pool.h"

// 新回调：IO线程只打包任务丢入线程池
void businessDispatch(TcpConnection *conn, const std::string &jsonStr)
{
    Task task;
    task.conn = conn;
    task.jsonReq = jsonStr;
    ThreadPool::getInstance().addTask(task);
}
bool socket_listen::socket_init()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd)
    {
        LOG_ERROR("create socket failed");
        return false;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(m_port);
    saddr.sin_addr.s_addr = inet_addr(m_ips.c_str());

    int res = bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (-1 == res)
    {
        LOG_ERROR("bind socket failed");
        close(sockfd);
        return false;
    }
    res = listen(sockfd, LIS_MAX);
    if (-1 == res)
    {
        LOG_ERROR("listen socket failed");
        return false;
    }

    return true;
}

int socket_listen::accept_client()
{
    int c = accept(sockfd, NULL, NULL);
    return c;
}

void SOCK_LIS_CALLBACK(int sockfd, short ev, void *arg)
{
    socket_listen *p = (socket_listen *)arg;
    if (!(ev & EV_READ))
        return;

    int cfd = p->accept_client();
    if (cfd < 0)
        return;
    LOG_INFO("new client connect fd=" + std::to_string(cfd));

    auto newConn = std::make_unique<TcpConnection>(cfd, p->Get_base(), businessDispatch);
    newConn->enableRead();
    ConnManager::getInstance().addConn(cfd, move(newConn));
}

int main()
{
    // 日志初始化，线上用LOG_INFO，调试改为LOG_DEBUG
    Log::getInstance()->init(LOG_INFO, "server.log", 10 * 1024 * 1024);
    LOG_INFO("ticket server starting...");
    // 初始化数据库连接池，10个最大连接
    MysqlConnPool::getInstance().init("127.0.0.1", "root", "211925", "Project_DB", 3306, 10);
    LOG_INFO("mysql connection pool init success");

    socket_listen sock_ser;
    if (!sock_ser.socket_init())
    {
        LOG_ERROR("socket init failed, exit");
        exit(1);
    }

    struct event_base *base = event_init();
    if (base == NULL)
    {
        // 替换cout为日志
        LOG_ERROR("event_base create failed, base is null");
        exit(1);
    }

    // 创建匿名管道，用于跨线程唤醒IO线程
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        LOG_ERROR("create pipe failed, errno:" + std::string(strerror(errno)));
        exit(1);
    }
    int pipeReadFd = pipefd[0];
    int pipeWriteFd = pipefd[1];

    // 单例线程池初始化，开启4个工作线程
    ThreadPool &pool = ThreadPool::getInstance();
    pool.init(4, base, pipeWriteFd);

    // 注册管道读事件
    struct event *pipeEv = event_new(base, pipeReadFd, EV_READ | EV_PERSIST, pipeReadCallback, nullptr);
    event_add(pipeEv, nullptr);

    sock_ser.Set_base(base);
    // 修正：最后一个参数传递 &sock_ser 而非 &sock_ev
    struct event *sock_ev = event_new(base, sock_ser.Get_sockfd(), EV_READ | EV_PERSIST, SOCK_LIS_CALLBACK, &sock_ser);
    event_add(sock_ev, nullptr);

    LOG_INFO("server event loop start, listen port 6000");
    // 启动事件循环
    event_base_dispatch(base);
    LOG_INFO("event loop exit, start release resource");

    // 程序退出，安全释放所有资源
    pool.stop();
    event_free(pipeEv);
    event_free(sock_ev);
    event_base_free(base);
    close(pipeReadFd);
    close(pipeWriteFd);

    // 释放连接池
    MysqlConnPool::getInstance().destroyPool();
    LOG_INFO("all resource released, server exit");
    return 0;
}