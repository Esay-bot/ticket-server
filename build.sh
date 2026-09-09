#!/usr/bin/env bash
# ============================================================
# 服务端 / 旧命令行客户端 一键编译脚本 (Linux / WSL)
# 依赖: g++ libevent-dev libjsoncpp-dev default-libmysqlclient-dev
#   Ubuntu 安装: sudo apt install -y g++ libevent-dev libjsoncpp-dev \
#                          default-libmysqlclient-dev mysql-server
# ============================================================
set -e
cd "$(dirname "$0")"

# 注: 代码使用了 std::make_unique, 需 C++14 (计划文档里的 -std=c++11 参考命令实际编不过)
echo "[1/2] 编译服务端 server/ser ..."
g++ -std=c++14 -Wall -g server/*.cpp -levent -ljsoncpp -lmysqlclient -o server/ser

echo "[2/2] 编译旧命令行客户端 client/cli ..."
g++ -std=c++14 -Wall -g client/client.cpp -ljsoncpp -o client/cli

echo "编译完成: ./server/ser  ./client/cli"
echo "首次使用请先初始化数据库: mysql -u root -p < server/sql/init.sql"
