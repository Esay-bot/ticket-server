# 票务预订 Agent 开发进度记录

> 配套计划：`票务预订Agent开发计划.md`。本文档按模块记录每次完成的内容、验收结果与对应 git 提交。
> 提交前缀使用 `Agent-Mx`，与此前 Qt 客户端的 `Mx` 提交区分。

---

## Agent-M0 环境与工程骨架（提交：`Agent-M0`）

**目标**：WSL2 里跑通 C++ 服务端 + MySQL，Windows 侧可直连；搭好 agent/ 工程骨架。

**完成内容**：

1. **协议事实核对**（以服务端源码为准，修正了计划文档中的两处推断）：
   - 帧格式确认：`4 字节网络序(大端)长度 + JSON`，见 `server/connection.cpp` 的 `sendResponse()`/`handleRead()`；
   - 枚举确认：`Login=1, Register, View, Reserve, MyReserve, Cancel`（`server/threadpool.h`），另有 `Exit=7` 但服务端无处理分支（落入 default）；
   - **修正 1**：响应中 `tk_id / max / num / yd_id` 等是**字符串**而非数字——`db_manager.cpp` 把 `MYSQL_ROW` 的 `char*` 直接赋给 `Json::Value`。工具层必须做 `int()` 转换；
   - **修正 2**：计划里 `-std=c++11` 编译不过（`ser.cpp:69` 用了 `std::make_unique`，C++14 起才有），实际用 `g++ -std=c++14 *.cpp -levent -ljsoncpp -lmysqlclient -o ser`。未改服务端任何代码。
2. **种子数据**：`server/sql/init.sql` 更新为 4 条评测场景数据（西安-北京 100/0、西安-上海 50/0、西安-成都 20/19 剩 1 张、西安-广州 80/80 售罄）。
3. **WSL 环境**：项目拷入 `~/ser-cli`；MySQL 8.4 启动，root 密码与服务端 `ser.cpp:80` 一致（`root/211925`）；`Project_DB` 建库导入。
4. **服务端**：C++14 编译通过，`./ser` 监听 `127.0.0.1:6000`（`ss -lnt` 确认）。
5. **Windows 连通性**：Windows 侧 Python 直连 `127.0.0.1:6000` 发 `type:3` 查票请求，收到 572 字节 JSON 响应（WSL2 默认 localhost 端口转发可用）。
6. **M0 验收（计划要求：旧命令行客户端闭环）**：注册测试账号 `13900000001/agenttest` → 登录 → 查票 → 预订 tk3 → 我的预约（yd_id=1）→ 取消；取消后数据库核实 `tk3.num=19`、`reserve_ticket` 清空，状态归位。
7. **工程骨架**：`agent/`（`__init__.py`、`requirements.txt`、`scripts/reset_db.sh` 评测用重库脚本）；`.gitignore` 增加 Python 条目。

**遗留**：`DEEPSEEK_API_KEY` 本机未设置，M3/M4 的真实 LLM 调用与评测数据待用户提供 Key 后跑出。

---

## Agent-M1 协议客户端（提交：`Agent-M1`）

**目标**：Python 重新实现「4 字节大端长度头 + JSON」协议，`TicketClient` 类 + 粘包/半包处理 + 请求锁。

**完成内容**（`agent/protocol.py`，约 200 行）：

1. **编码** `encode_frame()`：`struct.pack('>I', len(body)) + body`，正文 `ensure_ascii=False` 按 utf-8 字节计长（与 C++ 端 `sendStr.size()` 语义一致）；请求超 4096 字节提前拦截。
2. **解码** `try_extract_frame()`：纯函数，缓冲不足返回 `(None, buf)` 继续收；长度 0 或 >4096 抛 `ProtocolError`；调用方收到该异常立即断连（字节流已失步，不可恢复）。
3. **`TicketClient`**：`connect()/request()/close()`，支持 with 语法与测试用 socket 注入；`_recv_one_frame()` 循环凑帧，粘包多余字节留在 `_recv_buf` 给下次 `request()` 优先消费；`socket.timeout`(5s)/连接断开 → `TransportError` 并自动 close；响应非 JSON 对象 → `ProtocolError`。
4. **请求锁**：`threading.Lock` 覆盖「发送→接收→解析」整个临界区——服务端严格一问一答且响应不回显请求 type（`threadpool.cpp` 无请求标识字段），并发交错会把响应配错请求。
5. **请求校验**：`type` 必须 ∈ {1..6}（对照 `threadpool.h` OP_TYPE），低级错误发出去之前拦截。

**单元测试**（`agent/test_protocol.py`，12 个用例全过）：

| 用例 | 覆盖点 |
|---|---|
| 帧布局 / 中文按字节计长 | 编码正确性，与独立构造的帧交叉验证 |
| 半包碎块（1+3+5+剩余字节） | 凑帧循环 |
| 两帧粘连一次到达 | 粘包拆分 + 第二次 request 不再 recv |
| 帧尾残留下一帧半截头 | 跨请求缓冲复用 |
| 长度 0 / 4097 | ProtocolError + 连接关闭 |
| 非 JSON 正文 | ProtocolError + 连接关闭 |
| 在途请求阻塞第二个请求 | 请求锁串行化（Event 控制响应时机，确定性断言） |
| **真实 View 调用** | 连 WSL 服务端 6000，status=OK、arr 字段齐全（服务端未启动自动 skip） |

**验收对照计划**：✅ 半包分两次到达、✅ 两包粘连、✅ 非法长度报错、✅ 真实调用 View 成功。

---

## Agent-M2 工具封装（提交：`Agent-M2`）

**目标**：四个业务工具 + 结构化失败原因；登录态由会话持有，密码不进模型上下文。

**完成内容**（`agent/tools.py`）：

1. **`TicketSession`**：一条 TCP 连接 + 登录态（tel/user_name）。`login()/register()` 是 CLI 在进入 LLM 循环前的确定性步骤，密码永不出现在模型消息里；四个工具只做登录后业务。
2. **四个工具**（docstring 即能力说明）：
   - `query_tickets()` → `tickets:[{tk_id,addr,use_date,total,used,remaining}]`，服务端字符串字段统一转 int 并补算余票；
   - `reserve_ticket(tk_id, confirmed)`：先查后做——前置判断售罄/无此票，成功返回 `remaining_after`；
   - `my_reservations()` → `reservations:[{yd_id,addr,use_date}]`；
   - `cancel_reservation(yd_id, confirmed)`：预查本人预约，把"别人的预约号"归因为 `not_yours`。
3. **失败原因码固定**：`sold_out / ticket_not_found / not_yours / not_logged_in / server_rejected / network_error`，售罄时附 `alternatives`（有余票班次列表）供模型推荐替代。`confirmed` 参数为 M3 确认门控预留位。
4. **服务端事实固化进注释**：余票扣减一致性由 C++ 事务保证（工具层不碰）；服务端不防重复预订；取消校验 yd_id+tel 双条件。

**测试**（`agent/test_tools.py`，11 用例 + 真实服务端集成）：

- 单元（假 socket 脚本化响应）：字符串→int 归一、售罄+替代列表、无此票、非本人预约号、未登录拦截、网络错误归因、成功帧字段；
- 集成（真实服务端）：注册→登录→查票→订西安-北京→我的预约→取消→确认取消后列表为空；售罄班次（tk4）返回 `sold_out`；取消不存在预约号返回 `not_yours`。

**验收对照计划**：✅ 四工具顺序调用全通；✅ 售罄票返回明确失败原因。

---


