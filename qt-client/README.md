# 票务预约系统 · Qt 图形客户端

替换 `client/` 旧命令行客户端的 Qt 5 图形客户端（登录 / 查票 / 预约 / 取消 / 断线重连）。

## 架构

```
┌────────────────────────── Qt 客户端(本目录) ──────────────────────────┐
│                                                                      │
│  界面层(qt widgets, 全部主线程)                                       │
│  ┌────────────┐  ┌──────────────────────────────────────────────┐   │
│  │ LoginDialog │  │ MainWindow                                   │   │
│  │ 登录/注册    │  │ ┌────────────────────────────────────────┐  │   │
│  └──────┬──────┘  │ │ TicketTableModel ── QTableView(车票列表) │  │   │
│         │  共用    │ │ ReserveTableModel ─ QTableView(我的预约) │  │   │
│         ▼  一条    │ └────────────────────────────────────────┘  │   │
│  ┌──────────────┐  │ 按钮: 刷新/预约/我的预约/取消/重连 + 状态栏   │   │
│  │  TcpClient   │◄─└──────────────────────────────────────────────┘   │
│  │ (QTcpSocket   │   串行请求锁: 同一时刻只允许一个未完成请求          │
│  │  组合封装)    │   (服务端一问一答且响应不回显 type, 并发无法配对)   │
│  └──────┬───────┘                                                  │
│         │ readyRead → readAll → feed()                              │
│  ┌──────▼───────────┐                                              │
│  │ ProtocolCodec    │ 4 字节大端长度头 + JSON, 缓冲拆包(半包/粘包)    │
│  └──────┬───────────┘                                              │
└─────────┼────────────────────────────────────────────────────────────┘
          │ TCP 127.0.0.1:6000
┌─────────▼────────────────────────────────────────────────────────────┐
│ 服务端(../server): libevent 单线程 IO + 4 线程线程池 + MySQL(Project_DB) │
└───────────────────────────────────────────────────────────────────────┘
```

设计要点：

- **信号驱动，无手写收发线程**：`QTcpSocket` 本身异步（`readyRead`/`connected`/`disconnected`），UI 与网络同在主线程。
- **串行请求锁**：`TcpClient::send()` 后置 `busy_`，收到响应/超时/出错解锁；期间 `send()` 返回 false。
- **响应关联**：界面层用 `m_pendingReq` 记录当前请求 type，`jsonReceived` 到达时按它分发。
- **5 秒响应超时**：`QTimer` 单发实现（等价旧客户端 `SO_RCVTIMEO`）。
- **Model/View**：`QAbstractTableModel` 子类存结构体数组，整表替换走 `beginResetModel/endResetModel`。

## 目录结构

```
qt-client/
├── ticket-client.pro        # qmake 工程
├── src/
│   ├── main.cpp             # 入口: 加载 QSS → 登录 → 主窗口
│   ├── appconfig.h          # 服务端地址(单点定义)
│   ├── protocolcodec.{h,cpp}# M1 协议编解码(核心)
│   ├── tcpclient.{h,cpp}    # M2 网络层封装(核心)
│   ├── logindialog.{h,cpp}  # M3 登录/注册对话框
│   ├── tickettablemodel.{h,cpp} # M4 车票表模型(核心)
│   ├── reservetablemodel.{h,cpp}# M5 我的预约表模型
│   ├── jsonutil.h           # 防御性 JSON 取值(服务端数值字段是字符串)
│   └── mainwindow.{h,cpp}   # 主窗口: 页签/操作区/断线重连状态机
├── res/style.qss            # M6 统一样式(qrc 资源打包)
└── tests/                   # 四套自动化测试(见下)
```

## 编译与启动（WSL / Linux）

前置（一次性）：

```bash
sudo apt install -y g++ qtbase5-dev libevent-dev libjsoncpp-dev \
                    default-libmysqlclient-dev mysql-server fonts-noto-cjk
mysql -u root -p < server/sql/init.sql     # 建库 Project_DB + 种子数据
# 服务端连接参数: 127.0.0.1:3306 root/211925(见 server/ser.cpp)
./build.sh                                  # 编译 server/ser 与 client/cli
./server/ser &                              # 启动服务端(监听 127.0.0.1:6000)
```

编译运行 Qt 客户端：

```bash
cd qt-client
mkdir -p build && cd build
qmake ../ticket-client.pro && make -j$(nproc)
./ticket-client        # WSLg 直接弹窗; 远程 Linux 需配置 X11 转发
```

Windows 注意：本机两套 Qt(Anaconda 5.15.2 / Qt 5.12.4)均为 MSVC ABI 且无配套编译器，
推荐直接在 WSL 里编译运行（WSLg 显示窗口）。

## 应用层协议

帧格式：`4 字节大端(网络序)长度头 + JSON 文本`，长度 = JSON 字节数，合法范围 (0, 4096]。
客户端发送用紧凑 JSON；服务端返回 jsoncpp `toStyledString`（带缩进），双方只做 JSON 解析，互不影响。
服务端**一问一答**，响应不回显请求 type，故客户端必须串行发送。

| 操作 | type | 请求 | 成功响应 |
|---|---|---|---|
| 登录 | 1 | `{type,user_tel,user_passwd}` | `{status:"OK",user_name}` |
| 注册 | 2 | `{type,user_tel,user_name,user_passwd}` | `{status:"OK"}` |
| 查票 | 3 | `{type}` | `{status:"OK",num,arr:[{tk_id,addr,max,num,use_date}]}` |
| 预约 | 4 | `{type,tel,index}`(index=tk_id) | `{status:"OK"}` |
| 我的预约 | 5 | `{type,tel}` | `{status:"OK",num,arr:[{yd_id,addr,use_date}]}` |
| 取消 | 6 | `{type,tel,index}`(index=yd_id) | `{status:"OK"}` |

失败一律 `{status:"ERR"}`（无原因字段，客户端只能按操作类型给通用提示）。
注意：服务端把 MySQL 行直接序列化为 JSON **字符串**（如 `"max":"100"`），
客户端统一走 `JsonUtil::asInt/asStr` 兼容解析。

## 测试

全部对真实服务端运行（先启动 MySQL + `./server/ser`）：

```bash
cd qt-client/tests/<name> && mkdir -p build && cd build
qmake ../<name>.pro && make -j && ./<name>          # dialog/model/flow 加 QT_QPA_PLATFORM=offscreen
```

| 套件 | 覆盖 |
|---|---|
| codec_test | 半包/粘包/非法长度/坏 JSON 等 20 项断言（无需服务端） |
| net_test | 真实登录往返、串行锁、错误密码、连接被拒 |
| dialog_test | 登录成功/错误密码/重复注册/空值/密码不一致/等待态防连点 |
| model_test | 模型角色与 reset 信号、fromJson 防御、真实查票入表 |
| flow_test | 完整业务闭环 + kill 服务端断线/重连恢复（会重启服务端进程） |

## 已知限制

- 服务端明文密码、SQL 字符串拼接（仅本地开发，`db_manager.cpp` 有注释）。
- 无心跳保活；空闲连接异常断开只能等下次收发时发现。
- 响应不含失败原因，客户端提示只能按操作类型笼统给出。
- 服务端单实例（监听单端口、无会话 token，tel 即身份）。
- 查票无分页，车票量大时整表刷新。

## 3 分钟演示脚本（录屏用）

1. **登录**（30s）：启动 `./ticket-client` → 输错密码一次（提示）→ 正确登录进入主界面，车票自动加载。
2. **查票/预约**（60s）：点"刷新" → 选中"北京-上海"行 → 点"预约选中车票" → 状态栏"预约成功 · 共 1 条预约记录"，车票表已预约数 +1。
3. **我的预约/取消**（45s）：切到"我的预约"页签 → 选中 → "取消选中预约" → 数量复原、列表清空。
4. **断线重连**（45s）：终端 `pkill -x ser` → 界面提示"已断线"、按钮全灰仅"重连"可点 → `./server/ser &` 重启 → 点"重连" → 车票自动恢复。
