#include "threadpool.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include "log.h"
#include "mysql_conn_pool.h"

// 单例实现
ThreadPool &ThreadPool::getInstance()
{
    static ThreadPool instance;
    return instance;
}

// 私有构造初始化成员
ThreadPool::ThreadPool()
    : running(false), evBase(nullptr), pipeWriteFd(-1) // 未运行管道无效
{
}

void ThreadPool::init(int threadNum, struct event_base *base, int pipeFd) // main调用一次
{
    if (running) // 通过判断线程池是否运行 防止重复初始化
        return;
    evBase = base;
    pipeWriteFd = pipeFd;
    running = true;
    LOG_INFO("thread pool init, worker thread num=" + std::to_string(threadNum));
    for (int i = 0; i < threadNum; i++) // 创建指定数量工作线程
    {
        // 循环创建线程，绑定workerLoop线程主函数
        workers.emplace_back(std::thread(&ThreadPool::workerLoop, this));
    }
}
void ThreadPool::stop() // 安全停止线程池(程序退出调用)
{
    LOG_INFO("thread pool stopping...");
    running = false;
    cond.notify_all(); // 唤醒所有阻塞休眠的线程
    for (auto &t : workers)
    {
        if (t.joinable())
            t.join(); // 主线程等待子线程执行完毕，避免僵尸线程
    }
    workers.clear();
    LOG_INFO("thread pool stopped");
}

void ThreadPool::addTask(const Task &task)
{
    std::lock_guard<std::mutex> lock(mtx);
    taskQueue.push(task);
    cond.notify_one();
    LOG_DEBUG("add new business task to thread pool");
}

void ThreadPool::workerLoop()
{
    while (running)
    {
        std::unique_lock<std::mutex> lock(mtx);
        // 有任务或者线程池停止时才唤醒
        cond.wait(lock, [this]()
                  { return !running || !taskQueue.empty(); });

        if (!running)
            break;

        Task task = taskQueue.front();
        taskQueue.pop();
        lock.unlock();

        // 子线程执行业务、数据库耗时操作
        Json::Reader rd;
        Json::Value val;
        Json::Value resp;
        resp["status"] = "ERR"; // 默认错误状态，确保字段完整
        bool parseOk = rd.parse(task.jsonReq, val);

        if (!parseOk)
        {
            LOG_ERROR("fd=" + std::to_string(task.conn->getFd()) + " json request parse failed");
        }
        else
        {
            DBManager *db = MysqlConnPool::getInstance().getConn();
            if (!db)
            {
                LOG_ERROR("get mysql conn pool failed, fd=" + std::to_string(task.conn->getFd()));
            }
            else
            {
                int op = val["type"].asInt();
                switch (op)
                {
                case Login:
                {
                    std::string tel = val["user_tel"].asString();
                    std::string pwd = val["user_passwd"].asString();
                    std::string name;
                    if (db->userLogin(tel, pwd, name))
                    {
                        resp["status"] = "OK";
                        resp["user_name"] = name;
                        LOG_INFO("fd=" + std::to_string(task.conn->getFd()) + " user login success, tel=" + tel);
                    }
                    else
                    {
                        LOG_WARN("fd=" + std::to_string(task.conn->getFd()) + " user login failed, tel=" + tel);
                    }
                    break;
                }
                case Register:
                {
                    std::string tel = val["user_tel"].asString();
                    std::string pwd = val["user_passwd"].asString();
                    std::string name = val["user_name"].asString();
                    if (db->userRegister(tel, pwd, name))
                    {
                        resp["status"] = "OK";
                        LOG_INFO("fd=" + std::to_string(task.conn->getFd()) + " user register success, tel=" + tel);
                    }
                    else
                    {
                        LOG_WARN("fd=" + std::to_string(task.conn->getFd()) + " user register failed, tel=" + tel);
                    }
                    break;
                }
                case View:
                {
                    db->showTickets(resp);
                    LOG_INFO("fd=" + std::to_string(task.conn->getFd()) + " query all ticket list");
                    break;
                }
                case Reserve:
                {
                    int tkId = val["index"].asInt();
                    std::string tel = val["tel"].asString();
                    if (db->reserveTicket(tkId, tel))
                    {
                        resp["status"] = "OK";
                        LOG_INFO("fd=" + std::to_string(task.conn->getFd()) + " reserve ticket success, tkId=" + std::to_string(tkId) + ", tel=" + tel);
                    }
                    else
                    {
                        LOG_WARN("fd=" + std::to_string(task.conn->getFd()) + " reserve ticket failed, tkId=" + std::to_string(tkId) + ", tel=" + tel);
                    }
                    break;
                }
                case MyReserve:
                {
                    std::string tel = val["tel"].asString();
                    db->getMyReservedTickets(tel, resp);
                    LOG_INFO("fd=" + std::to_string(task.conn->getFd()) + " query self reserve ticket, tel=" + tel);
                    break;
                }
                case Cancel:
                {
                    int ydId = val["index"].asInt();
                    std::string tel = val["tel"].asString();
                    if (db->cancelReservedTicket(ydId, tel))
                    {
                        resp["status"] = "OK";
                        LOG_INFO("fd=" + std::to_string(task.conn->getFd()) + " cancel reserve success, ydId=" + std::to_string(ydId) + ", tel=" + tel);
                    }
                    else
                    {
                        LOG_WARN("fd=" + std::to_string(task.conn->getFd()) + " cancel reserve failed, ydId=" + std::to_string(ydId) + ", tel=" + tel);
                    }
                    break;
                }
                default:
                {
                    LOG_WARN("fd=" + std::to_string(task.conn->getFd()) + " unknown op type=" + std::to_string(op));
                }
                break;
                }
                // 归还连接到池
                MysqlConnPool::getInstance().releaseConn(db);
            }
        }

        // 管道分段写入：fd -> json长度 -> json字符串
        std::string sendStr = resp.toStyledString();
        int clientFd = task.conn->getFd();
        int jsonLen = static_cast<int>(sendStr.size());

        write(pipeWriteFd, &clientFd, sizeof(int));
        write(pipeWriteFd, &jsonLen, sizeof(int));
        write(pipeWriteFd, sendStr.c_str(), jsonLen);
    }
}

// IO线程管道读回调：分段读取数据，避免管道粘包
void pipeReadCallback(int fd, short ev, void *arg)
{
    (void)ev;
    (void)arg;

    int clientFd = 0;
    int jsonLen = 0;
    // 先读取客户端fd
    ssize_t n1 = read(fd, &clientFd, sizeof(int));
    if (n1 <= 0)
        return;
    // 再读取json报文长度
    ssize_t n2 = read(fd, &jsonLen, sizeof(int));
    if (n2 <= 0 || jsonLen <= 0)
        return;

    char *jsonBuf = new char[jsonLen + 1];
    ssize_t n3 = read(fd, jsonBuf, jsonLen);
    if (n3 <= 0)
    {
        delete[] jsonBuf;
        return;
    }
    jsonBuf[jsonLen] = '\0';

    // 从连接管理器找到对应连接发送响应
    TcpConnection *conn = ConnManager::getInstance().getConn(clientFd);
    if (conn != nullptr)
    {
        conn->sendResponse(std::string(jsonBuf, jsonLen));
    }

    delete[] jsonBuf;
}