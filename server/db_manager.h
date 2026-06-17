#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include <jsoncpp/json/json.h>

using namespace std;

class DBManager {
public:
    DBManager(const string& db_ips = "127.0.0.1", 
              const string& db_username = "root",
              const string& db_passwd = "211925",
              const string& db_dbname = "Project_DB",
              unsigned int port = 3306)
        : db_ips_(db_ips), db_username_(db_username), 
          db_passwd_(db_passwd), db_dbname_(db_dbname), 
          port_(port), mysql_con_(nullptr) {}

    ~DBManager() {
        if (mysql_con_) {
            mysql_close(mysql_con_);
            mysql_con_ = nullptr;
        }
    }

    // 禁止拷贝和移动
    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;
    DBManager(DBManager&&) = delete;
    DBManager& operator=(DBManager&&) = delete;

    // 数据库连接
    bool connect();

    // 事务管理
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // 用户相关操作
    bool userRegister(const string& tel, const string& passwd, const string& name);
    bool userLogin(const string& tel, const string& passwd, string& name);

    // 票务相关操作
    bool showTickets(Json::Value& resval);
    bool reserveTicket(int tk_id, const string& tel);
    bool getMyReservedTickets(const string& tel, Json::Value& reserve);
    bool cancelReservedTicket(int yd_id, const string& tel);

private:
    // 安全执行SQL（防止SQL注入基础版，生产环境建议用预处理）
    bool executeSQL(const string& sql);
    // 获取查询结果集
    MYSQL_RES* getQueryResult(const string& sql);
    // 释放结果集
    void freeResult(MYSQL_RES* res);

private:
    string db_ips_;
    string db_username_;
    string db_passwd_;
    string db_dbname_;
    unsigned int port_;
    MYSQL* mysql_con_;
};

#endif // DB_MANAGER_H