#include "ser.h"
#include <memory>

//新回调：IO线程只打包任务丢入线程池
void businessDispatch(TcpConnection* conn, const std::string& jsonStr)
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
        return false;

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(m_port);
    saddr.sin_addr.s_addr = inet_addr(m_ips.c_str());

    int res = bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (-1 == res)
    {
        cout << "bind err" << endl;
        close(sockfd);
        return false;
    }
    res = listen(sockfd, LIS_MAX);
    if (-1 == res)
        return false;

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
    if (!(ev & EV_READ)) return;

    int cfd = p->accept_client();
    if (cfd < 0) return;
    cout << "new client fd = " << cfd << endl;

    auto newConn = std::make_unique<TcpConnection>(cfd, p->Get_base(), businessDispatch);
    newConn->enableRead();
    ConnManager::getInstance().addConn(cfd, move(newConn));
}

int main()
{
    socket_listen sock_ser;
    if (!sock_ser.socket_init())
    {
        cout << "socket init err" << endl;
        exit(1);
    }

    struct event_base *base = event_init();
    if (base == NULL)
    {
        cout << "base null" << endl;
        exit(1);
    }

    // 创建匿名管道，用于跨线程唤醒IO线程
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("pipe create failed");
        exit(1);
    }
    int pipeReadFd = pipefd[0];
    int pipeWriteFd = pipefd[1];

    // 单例线程池初始化，开启4个工作线程
    ThreadPool& pool = ThreadPool::getInstance();
    pool.init(4, base, pipeWriteFd);

    // 注册管道读事件
    struct event* pipeEv = event_new(base, pipeReadFd, EV_READ | EV_PERSIST, pipeReadCallback, nullptr);
    event_add(pipeEv, nullptr);

    sock_ser.Set_base(base);
    struct event *sock_ev = event_new(base, sock_ser.Get_sockfd(), EV_READ | EV_PERSIST, SOCK_LIS_CALLBACK, &sock_ev);
    event_add(sock_ev, nullptr);

    // 启动事件循环
    event_base_dispatch(base);

    // 程序退出，安全释放所有资源
    pool.stop();
    event_free(pipeEv);
    event_free(sock_ev);
    event_base_free(base);
    close(pipeReadFd);
    close(pipeWriteFd);

    return 0;
}