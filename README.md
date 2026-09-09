# ser-cli · 票务预约系统

libevent 服务端 + 双客户端（旧命令行 / 新 Qt 图形客户端），开发与验收记录见各目录 README。

| 目录 | 说明 |
|---|---|
| `server/` | libevent 单线程 IO + 4 线程线程池服务端，监听 `127.0.0.1:6000`，MySQL `Project_DB` |
| `client/` | 旧命令行客户端（同步阻塞，已被 Qt 客户端替代，保留作协议对照） |
| `qt-client/` | **Qt 5 图形客户端（本次交付）**，详见 [qt-client/README.md](qt-client/README.md) |
| `Qt客户端开发计划.md` | 分阶段开发计划（M0～M6）与验收标准 |

## 快速启动（WSL/Linux）

```bash
# 一次性: 安装依赖并初始化数据库(见 qt-client/README.md 前置部分)
mysql -u root -p < server/sql/init.sql
./build.sh          # 编译服务端与旧客户端(注: C++14, 源码用了 make_unique)
./server/ser &      # 启动服务端
cd qt-client && mkdir -p build && cd build && qmake .. && make -j && ./ticket-client
```

服务端数据库连接：`127.0.0.1:3306`，root/211925（`server/ser.cpp`）。
