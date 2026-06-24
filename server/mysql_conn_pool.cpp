#include "mysql_conn_pool.h"
#include "db_manager.h"
#include "log.h"
#include <chrono>

MysqlConnPool& MysqlConnPool::getInstance()
{
    static MysqlConnPool ins;
    return ins;
}

MysqlConnPool::MysqlConnPool()
    : maxConn_(10), waitTimeout_(3), usingCnt_(0)
{}

MysqlConnPool::~MysqlConnPool()
{
    destroyPool();
}

DBManager* MysqlConnPool::createSingleConn()
{
    DBManager* db = new DBManager(dbIp_, dbUser_, dbPwd_, dbName_, dbPort_);
    // 获取原生MYSQL句柄，开启自动重连
    MYSQL* mysql = db->getMysqlHandle();
    char autoReconnect = 1;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &autoReconnect);

    if (!db->connect())
    {
        LOG_ERROR("create mysql connection failed");
        delete db;
        return nullptr;
    }
    return db;
}

bool MysqlConnPool::checkConnAlive(DBManager* db)
{
    if (!db)
        return false;
    // 使用MySQL原生mysql_ping做心跳探活，轻量无SQL开销
    MYSQL* mysql = db->getMysqlHandle();
    if (mysql_ping(mysql) != 0)
    {
        LOG_WARN("mysql connection lost, will recreate connection");
        return false;
    }
    return true;
}

void MysqlConnPool::init(const std::string& ip, const std::string& user, const std::string& pwd,
                         const std::string& db, int port, int maxConn, int waitTimeout)
{
    std::lock_guard<std::mutex> lock(mtx_);
    dbIp_ = ip;
    dbUser_ = user;
    dbPwd_ = pwd;
    dbName_ = db;
    dbPort_ = port;
    maxConn_ = maxConn;
    waitTimeout_ = waitTimeout;

    LOG_INFO("mysql connection pool init start, max conn=" + std::to_string(maxConn_));
    for (int i = 0; i < maxConn_; ++i)
    {
        DBManager* dbConn = createSingleConn();
        if (dbConn)
        {
            idleQueue_.push(dbConn);
        }
    }
    LOG_INFO("mysql pool init done, idle conn num=" + std::to_string(idleQueue_.size()));
}

DBManager* MysqlConnPool::getConn()
{
    std::unique_lock<std::mutex> lock(mtx_);
    // 等待空闲连接，超时时间waitTimeout_秒
    bool hasIdle = cond_.wait_for(lock, std::chrono::seconds(waitTimeout_), [this](){
        return !idleQueue_.empty();
    });
    if (!hasIdle)
    {
        LOG_WARN("get mysql conn timeout, no idle connection");
        return nullptr;
    }

    DBManager* db = idleQueue_.front();
    idleQueue_.pop();
    usingCnt_++;

    // 心跳校验连接有效性，失效则销毁重建
    if (!checkConnAlive(db))
    {
        delete db;
        db = createSingleConn();
        if (!db)
        {
            usingCnt_--;
            cond_.notify_one();
            return nullptr;
        }
    }
    LOG_DEBUG("get mysql conn, current using=" + std::to_string(usingCnt_.load()));
    return db;
}

void MysqlConnPool::releaseConn(DBManager* db)
{
    if (!db)
        return;
    std::lock_guard<std::mutex> lock(mtx_);
    idleQueue_.push(db);
    usingCnt_--;
    cond_.notify_one();
    LOG_DEBUG("release mysql conn back to pool");
}

void MysqlConnPool::destroyPool()
{
    std::lock_guard<std::mutex> lock(mtx_);
    while (!idleQueue_.empty())
    {
        DBManager* db = idleQueue_.front();
        idleQueue_.pop();
        delete db;
    }
    LOG_INFO("mysql connection pool destroyed, all conn released");
}