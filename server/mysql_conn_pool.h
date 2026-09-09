#ifndef MYSQL_CONN_POOL_H
#define MYSQL_CONN_POOL_H

#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>
#include "log.h"
#include "db_manager.h"

class MysqlConnPool
{
public:
    // 单例获取，对齐项目局部静态单例风格
    static MysqlConnPool& getInstance();
    // 初始化连接池参数，预创建连接
    void init(const std::string& ip, const std::string& user, const std::string& pwd,
              const std::string& db, int port, int maxConn = 10, int waitTimeout = 3);
    // 获取一个可用DBManager连接，超时阻塞
    DBManager* getConn();
    // 使用完毕归还连接
    void releaseConn(DBManager* db);
    // 程序退出销毁所有连接
    void destroyPool();

private:
    MysqlConnPool();
    ~MysqlConnPool();
    MysqlConnPool(const MysqlConnPool&) = delete;
    MysqlConnPool& operator=(const MysqlConnPool&) = delete;

    // 创建单个数据库连接
    DBManager* createSingleConn();
    // 使用mysql_ping原生心跳检测连接是否有效，失效重建
    bool checkConnAlive(DBManager* db);

    std::queue<DBManager*> idleQueue_;
    std::mutex mtx_;
    std::condition_variable cond_;

    std::string dbIp_;
    std::string dbUser_;
    std::string dbPwd_;
    std::string dbName_;
    int dbPort_;
    int maxConn_;
    int waitTimeout_;
    std::atomic<int> usingCnt_;
};

#endif