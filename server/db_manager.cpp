#include "db_manager.h"
#include <cstdio>
#include <cstdlib>
#include "log.h"

bool DBManager::connect() {
    if (mysql_con_) {
        mysql_close(mysql_con_);
        mysql_con_ = nullptr;
    }

    mysql_con_ = mysql_init(nullptr);
    if (!mysql_con_) {
        LOG_ERROR("mysql_init failed: " + std::string(mysql_error(mysql_con_)));
        return false;
    }

    mysql_con_ = mysql_real_connect(mysql_con_, 
                                   db_ips_.c_str(),
                                   db_username_.c_str(),
                                   db_passwd_.c_str(),
                                   db_dbname_.c_str(),
                                   port_,
                                   nullptr,
                                   0);
    if (!mysql_con_) {
        LOG_ERROR("mysql_real_connect failed: " + std::string(mysql_error(mysql_con_)));
        mysql_close(mysql_con_);
        mysql_con_ = nullptr;
        return false;
    }

    // 设置字符集
    mysql_set_character_set(mysql_con_, "utf8");
    return true;
}

bool DBManager::beginTransaction() {
    return executeSQL("BEGIN");
}

bool DBManager::commitTransaction() {
    return executeSQL("COMMIT");
}

bool DBManager::rollbackTransaction() {
    return executeSQL("ROLLBACK");
}

bool DBManager::userRegister(const string& tel, const string& passwd, const string& name) {
    char sql[1024] = {0};
    // 基础防注入（生产环境用预处理语句）
    snprintf(sql, sizeof(sql), 
             "INSERT INTO user_info VALUES(0,'%s','%s','%s',1)",
             tel.c_str(), name.c_str(), passwd.c_str());
    return executeSQL(sql);
}

bool DBManager::userLogin(const string& tel, const string& passwd, string& name) {
    char sql[512] = {0};
    snprintf(sql, sizeof(sql), 
             "SELECT username,passwd FROM user_info WHERE tel='%s'",
             tel.c_str());
    
    MYSQL_RES* res = getQueryResult(sql);
    if (!res) return false;

    int row_num = mysql_num_rows(res);
    if (row_num != 1) {
        freeResult(res);
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0] || !row[1]) {
        freeResult(res);
        return false;
    }

    if (string(row[1]) != passwd) {
        freeResult(res);
        return false;
    }

    name = row[0];
    freeResult(res);
    return true;
}

bool DBManager::showTickets(Json::Value& resval) {
    const string sql = "SELECT tk_id,addr,max,num,use_date FROM ticket_info";
    MYSQL_RES* res = getQueryResult(sql);
    if (!res) return false;

    int row_num = mysql_num_rows(res);
    resval["status"] = "OK";
    resval["num"] = row_num;

    if (row_num == 0) {
        freeResult(res);
        return true;
    }

    Json::Value arr;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        Json::Value tmp;
        tmp["tk_id"] = row[0] ? row[0] : "";
        tmp["addr"] = row[1] ? row[1] : "";
        tmp["max"] = row[2] ? row[2] : "";
        tmp["num"] = row[3] ? row[3] : "";
        tmp["use_date"] = row[4] ? row[4] : "";
        arr.append(tmp);
    }

    resval["arr"] = arr;
    freeResult(res);
    return true;
}

bool DBManager::reserveTicket(int tk_id, const string& tel) {
    if (!beginTransaction()) return false;

    // 查询票务信息
    char sql1[512] = {0};
    snprintf(sql1, sizeof(sql1), 
             "SELECT max,num FROM ticket_info WHERE tk_id=%d",
             tk_id);
    
    MYSQL_RES* res = getQueryResult(sql1);
    if (!res) {
        rollbackTransaction();
        return false;
    }

    int row_num = mysql_num_rows(res);
    if (row_num != 1) {
        freeResult(res);
        rollbackTransaction();
        LOG_ERROR("ticket record not unique: tk_id=" + std::to_string(tk_id));
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0] || !row[1]) {
        freeResult(res);
        rollbackTransaction();
        return false;
    }

    int tk_max = atoi(row[0]);
    int tk_num = atoi(row[1]);
    freeResult(res);

    if (tk_max <= tk_num) {
        rollbackTransaction();
        LOG_WARN("no available tickets: tk_id=" + std::to_string(tk_id));
        return false;
    }

    // 更新票务数量
    char sql2[512] = {0};
    snprintf(sql2, sizeof(sql2), 
             "UPDATE ticket_info SET num=%d WHERE tk_id=%d",
             tk_num + 1, tk_id);
    
    if (!executeSQL(sql2)) {
        rollbackTransaction();
        return false;
    }

    // 插入预约记录
    char sql3[512] = {0};
    snprintf(sql3, sizeof(sql3), 
             "INSERT INTO reserve_ticket VALUES(0,%d,'%s',NOW())",
             tk_id, tel.c_str());
    
    if (!executeSQL(sql3)) {
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

bool DBManager::getMyReservedTickets(const string& tel, Json::Value& reserve) {
    char sql[512] = {0};
    snprintf(sql, sizeof(sql), 
             "SELECT yd_id,addr,use_date FROM ticket_info,reserve_ticket "
             "WHERE reserve_ticket.tel='%s' AND ticket_info.tk_id=reserve_ticket.tk_id",
             tel.c_str());
    
    MYSQL_RES* res = getQueryResult(sql);
    if (!res) return false;

    int row_num = mysql_num_rows(res);
    reserve["status"] = "OK";
    reserve["num"] = row_num;

    if (row_num == 0) {
        freeResult(res);
        return true;
    }

    Json::Value arr;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        Json::Value tmp;
        tmp["yd_id"] = row[0] ? row[0] : "";
        tmp["addr"] = row[1] ? row[1] : "";
        tmp["use_date"] = row[2] ? row[2] : "";
        arr.append(tmp);
    }

    reserve["arr"] = arr;
    freeResult(res);
    return true;
}

bool DBManager::cancelReservedTicket(int yd_id, const string& tel) {
    if (!beginTransaction()) return false;

    // 查询tk_id
    char sql1[512] = {0};
    snprintf(sql1, sizeof(sql1), 
             "SELECT tk_id FROM reserve_ticket WHERE yd_id=%d AND tel='%s'",
             yd_id, tel.c_str());
    
    MYSQL_RES* res = getQueryResult(sql1);
    if (!res) {
        rollbackTransaction();
        return false;
    }

    int row_num = mysql_num_rows(res);
    if (row_num != 1) {
        freeResult(res);
        rollbackTransaction();
        LOG_WARN("reserve record not found: yd_id=" + std::to_string(yd_id) + ", tel=" + tel);
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    int tk_id = row ? atoi(row[0]) : -1;
    freeResult(res);

    if (tk_id < 0) {
        rollbackTransaction();
        return false;
    }

    // 删除预约记录
    char sql2[512] = {0};
    snprintf(sql2, sizeof(sql2), 
             "DELETE FROM reserve_ticket WHERE yd_id=%d AND tel='%s'",
             yd_id, tel.c_str());
    
    if (!executeSQL(sql2)) {
        rollbackTransaction();
        return false;
    }

    // 更新票务数量
    char sql3[512] = {0};
    snprintf(sql3, sizeof(sql3), 
             "UPDATE ticket_info SET num=num-1 WHERE tk_id=%d",
             tk_id);
    
    if (!executeSQL(sql3)) {
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

bool DBManager::executeSQL(const string& sql) {
    if (!mysql_con_) {
        LOG_ERROR("get query result failed: " + std::string(mysql_error(mysql_con_)));
        return false;
    }

    if (mysql_query(mysql_con_, sql.c_str()) != 0) {
        LOG_ERROR("get query result failed: " + std::string(mysql_error(mysql_con_)));
        return false;
    }

    return true;
}

MYSQL_RES* DBManager::getQueryResult(const string& sql) {
    if (!executeSQL(sql)) return nullptr;

    MYSQL_RES* res = mysql_store_result(mysql_con_);
    if (!res) {
        LOG_ERROR("get query result failed: " + std::string(mysql_error(mysql_con_)));
    }

    return res;
}

void DBManager::freeResult(MYSQL_RES* res) {
    if (res) {
        mysql_free_result(res);
    }
}