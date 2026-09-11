# 票务预约系统 · Qt 图形客户端

Agent 桌面演示端（登录 / 车票与预约表格 / **AI 助手对话**，见《Qt+Agent桌面端开发计划》）。
V1-M2 起**不再直连 C++ 服务端 TCP**：登录、表格数据全部经 FastAPI 服务层（`agent/service.py`）走 HTTP，
会话的 TCP 连接与登录态住在 Python 侧 —— 界面与 Agent 解耦，以后换 Web 前端零成本。

## 架构（V1-M2 起）

```
┌────────────────────── Qt 客户端(本目录) ───────────────────────┐
│  LoginDialog(登录/注册)  MainWindow(页签: 车票/我的预约)        │
│        │                        │                             │
│        ▼        共用             ▼                             │
│  ┌──────────────────────────────────────────┐                 │
│  │ ApiClient (QNetworkAccessManager 封装)    │                 │
│  │  /login /register /tickets /reservations  │                 │
│  │  /chat(V1-M3) /logout /health             │                 │
│  │  错误人话化: 服务层未启动 -> 提示启动命令;   │                 │
│  │  4xx/5xx -> 直接展示服务层 {"detail":...}  │                 │
│  └──────────────┬───────────────────────────┘                 │
└─────────────────┼─────────────────────────────────────────────┘
                  │ HTTP + JSON (默认 http://127.0.0.1:8000)
┌─────────────────▼─────────────────────────────────────────────┐
│ FastAPI 服务层(agent/service.py): 会话字典+TTL, 错误分级        │
└─────────────────┬─────────────────────────────────────────────┘
                  │ TCP(自研 4 字节长度头+JSON 协议, 由 Python 侧对接)
┌─────────────────▼─────────────────────────────────────────────┐
│ C++ libevent 服务端(../server, 零改动) ⇄ MySQL                  │
└───────────────────────────────────────────────────────────────┘
```

设计要点：

- **手动"预约/取消"按钮已移除**：危险操作只能经对话发起（Agent 确认门控），界面上不存在绕过门控的捷径。
- **全部异步**：`QNetworkAccessManager` 发请求，信号回 UI 线程刷新表格/状态栏，界面永不阻塞。
- **会话**：`session_id` 由 `ApiClient` 持有；"退出登录"调 `/logout`（服务层关 TCP）后关窗，
  `main.cpp` 循环重新弹登录 —— 覆盖"断网重启服务后重新登录继续"场景。
- **Model/View**：`QAbstractTableModel` 子类存结构体数组，整表替换走 `beginResetModel/endResetModel`；
  数据源适配 `fromHttpArray`（服务层 JSON 形态）。
- 旧直连 TCP 形态（`TcpClient`/`ProtocolCodec`）保留在源码与 codec/net 测试中，作为协议层实现与对照。

## 目录结构

```
qt-client/
├── ticket-client.pro        # qmake 工程
├── src/
│   ├── main.cpp             # 入口: 加载 QSS → 登录(退出可重登循环)
│   ├── appconfig.h          # 服务层/服务端地址(单点定义)
│   ├── apiclient.{h,cpp}    # V1-M2 服务层 HTTP 客户端(核心)
│   ├── logindialog.{h,cpp}  # 登录/注册对话框(走 /login /register, 含探活提示)
│   ├── tickettablemodel.{h,cpp} # 车票表模型(fromJson 直连形态 + fromHttpArray)
│   ├── reservetablemodel.{h,cpp}# 我的预约表模型(同上)
│   ├── jsonutil.h           # 防御性 JSON 取值(兼容字符串/数字数值)
│   ├── mainwindow.{h,cpp}   # 主窗口: 页签/刷新/退出登录
│   ├── protocolcodec.{h,cpp}# M1 协议编解码(旧直连形态, 测试用)
│   └── tcpclient.{h,cpp}    # M2 网络层封装(旧直连形态, 测试用)
├── res/style.qss            # 统一样式(qrc 资源打包)
├── scripts/build_and_test.sh# 一键构建并运行全部测试(WSL)
└── tests/                   # 五套自动化测试(见下)
```

## 编译与启动（WSL / Linux）

前置（一次性）：

```bash
sudo apt install -y g++ qtbase5-dev libevent-dev libjsoncpp-dev \
                    default-libmysqlclient-dev mysql-server fonts-noto-cjk
mysql -u root -p < server/sql/init.sql     # 建库 Project_DB + 种子数据
./build.sh                                  # 编译 server/ser 与 client/cli
pip3 install --user --break-system-packages fastapi uvicorn httpx   # WSL 侧服务层依赖
```

每次开发前启动（顺序）：

```bash
# 1) WSL 里起后端
sudo service mysql start && cd ~/ser-cli/server && ./ser      # :6000

# 2) WSL 里起 Agent 服务层(或 Windows 侧: python -m agent.service)
python3 -m agent.service --port 8000                           # :8000

# 3) 编译运行 Qt 客户端
cd qt-client && mkdir -p build && cd build
qmake ../ticket-client.pro && make -j$(nproc)
./ticket-client        # WSLg 直接弹窗; 服务层没起会在登录框直接提示启动命令
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

```bash
# 一键构建并运行全部(WSL, 需 C++ 服务端 :6000 已启动; HTTP 测试自行拉起服务层):
bash scripts/build_and_test.sh
# 或单跑: cd tests/<name>/build && qmake ../<name>.pro && make && ./<name>
```

| 套件 | 覆盖 |
|---|---|
| codec_test | 半包/粘包/非法长度/坏 JSON 等 20 项断言（无需服务端, 旧直连形态） |
| net_test | 真实注册→登录往返、串行锁、错误密码、连接被拒（自注册随机账号） |
| model_test | 模型角色与 reset 信号、fromJson/fromHttpArray 防御（无需服务端） |
| dialog_test | HTTP 登录成功/错误密码 401/重复注册 409/空值/密码不一致/等待态防连点/**服务层未启动提示**（自拉起服务层+自注册账号） |
| flow_test | 登录→/tickets→/reservations 表格链路、退出登录会话销毁、kill 服务层→"请先启动 agent 服务"提示 |
| chat_test | **V1-M3 验收**：回复/轨迹行/确认卡片渲染、卡片按钮=等价文本、旧卡片陈旧化（无 Key）；`DEEPSEEK_API_KEY` 存在时加跑真实全场景（查票→订票出卡→点确认→预约成立→查预约→取消出卡→点确认→清空，及"不订了"零预订） |

## 已知限制

- 服务端明文密码、SQL 字符串拼接（仅本地开发，`db_manager.cpp` 有注释）。
- 无心跳保活；服务层会话空闲 30 分钟被清理（TTL），之后需重新登录。
- 查票无分页，车票量大时整表刷新。

## 3 分钟演示脚本（录屏用, V1 功能闭环版）

前置：起 C++ 服务端 + 服务层（`DEEPSEEK_API_KEY` 已 export），运行 `./ticket-client`。

1. **登录**（30s）：先不起服务层启动一次 → 登录框直接提示"请先启动 python -m agent.service"
   → 起服务层重开 → 输错密码一次（401 人话提示）→ 正确登录进入"AI 助手"页签。
2. **查票**（30s）：输入"有哪些票" → 回复班次清单，消息下方灰色小字显示
   `└ 工具: query_tickets(成功)` —— Function Calling 可见；"车票列表"页签同步刷新。
3. **订票 + 确认门控（主角）**（60s）：输入"订10月1日去北京的" → 出现**确认卡片**
   （复述：班次/日期/余票/账号 + [确认预订] [不订了]）→ 讲解"模型只能请求，用户点击才放行"
   → 点[确认预订] → 预订成功，车票表"已预约"+1、"我的预约"出现 1 条。
4. **查预约 + 取消闭环**（45s）：输入"我订了哪些预约" → 列出 → "取消我的预约" → 出取消卡片
   → 点[确认取消] → 预约清空、余票复原。
5. **拒绝路径**（15s）：再订一班 → 点[不订了] → 无任何预订产生（门控不放行的证据）。
