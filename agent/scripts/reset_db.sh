#!/usr/bin/env bash
# 重置票务数据库到初始种子状态(Agent 评测前/后使用)
# 用法(在 WSL Ubuntu 内执行): bash agent/scripts/reset_db.sh
#   可用环境变量覆盖: DB_USER(默认 root)  DB_PASS(默认 211925, 与 server/ser.cpp 一致)
#
# 效果: DROP 并重建 Project_DB, 车票种子数据恢复为:
#   tk1 西安-北京 100/0   tk2 西安-上海 50/0
#   tk3 西安-成都 20/19(剩1张)   tk4 西安-广州 80/80(售罄)
# 注意: 会清空所有用户与预约记录。

set -e
DB_USER="${DB_USER:-root}"
DB_PASS="${DB_PASS:-211925}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INIT_SQL="$SCRIPT_DIR/../../server/sql/init.sql"

echo "[reset_db] 服务端需停连或容忍短暂断连; 开始重建 Project_DB ..."
mysql -u"$DB_USER" -p"$DB_PASS" -e "DROP DATABASE IF EXISTS Project_DB;"
mysql -u"$DB_USER" -p"$DB_PASS" < "$INIT_SQL"
mysql -u"$DB_USER" -p"$DB_PASS" Project_DB -e "SELECT tk_id, addr, num, use_date FROM ticket_info;"
echo "[reset_db] 完成。服务端使用连接池, 若此前已启动建议重启 ./ser 使连接指向新库。"
