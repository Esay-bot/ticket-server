#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <event.h>
#include "connection.h"
#include "mysql_conn_pool.h"

// 前置声明枚举，不用引入ser.h,解决Login未识别
enum OP_TYPE
{
    Login = 1,
    Register,
    View,
    Reserve,
    MyReserve,
    Cancel,
    Exit
};

// 任务结构体：保存当前连接+请求报文
struct Task
{
    TcpConnection *conn; // 保存当前客户端连接，后面用来回包
    std::string jsonReq; // 完整未处理过的json报文
};
class ThreadPool
{
public:
    static ThreadPool &getInstance(); // 线程池，全局只有一个
    void init(int threadNum, struct event_base *base, int pipeFd);
    // main函数只调用一次，初始化线程数量，事件循环，管道
    void addTask(const Task &task); // IO线程调用，把任务放入阻塞队列
    void stop();                    // 程序退出时调用，回收所有线程资源
private:
    ThreadPool(); // 私有构造，禁止外部new，保证单例
    // 禁止拷贝，移动，防止多实例
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;
    void workerLoop(); // 每个工作线程无限循环执行
private:
    std::vector<std::thread> workers; // 存储所有工作线程
    std::queue<Task> taskQueue;       // 阻塞任务队列
    std::mutex mtx;                   // 保护队列互斥锁
    std::condition_variable cond;     // 线程休眠/唤醒条件变量
    std::atomic<bool> running;        // 原子标记：线程池是否运行
    struct event_base *evBase;        // 保存libevent_base
    int pipeWriteFd;                  // 保存写端，子线程用来发唤醒信号
};
void pipeReadCallback(int fd, short ev, void *arg); // 管道读事件回调：IO线程收到任务完成信号，执行响应发送

#endif