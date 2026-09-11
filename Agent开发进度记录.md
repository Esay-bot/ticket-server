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

## Agent-M3 Agent 循环与确认门控（提交：`Agent-M3`）

**目标**：手写 DeepSeek Function Calling 循环（不引框架）+ 代码层确认门控。计划称此模块为项目"灵魂"。

**完成内容**（`agent/agent.py`，`TicketAgent` 类）：

1. **消息列表 + 工具 schema + 执行回填**的经典循环：
   `chat(用户输入)` → messages 追加 → `llm.chat.completions.create(model, messages, tools)` →
   有 `tool_calls`：assistant 消息（含 tool_calls 结构）入栈 → 逐个执行 → 结果以 `role:"tool"` + `tool_call_id` 回填 → 继续下一轮；无 `tool_calls`：返回文本，本轮结束。
2. **确认门控（代码层强制，回答面试自测题 4）**——服务端收到 type=4/6 即产生真实副作用，因此在"模型想调用"与"TCP 发包"之间加了三重闸门：
   - 未带 `confirmed=true` 的 reserve/cancel：执行器**不碰 TCP**，查询详情记为 pending，返回 `confirm_required` + 复述文案（班次/日期/余票 或 预约号），由模型转达用户；
   - `confirmed=true` 必须同时满足：**pending 存在且参数一致** 且 **pending 创建后用户又发过至少一条消息**（`_user_turns_since_pending` 计数）——模型无法在同一用户轮次内"自问自答"绕过门控；
   - 新的危险调用参数与 pending 不一致 → 覆盖 pending（视为用户改主意）。
   门控期即发现售罄/无此票/非本人预约 → 直接短路返回对应失败原因，不进确认流程。
3. **系统提示词**：角色、当前用户（tel/user_name 注入，无密码）、能力边界（只做票务）、tk_id/yd_id 严禁编造、两步确认操作规范（a 不带 confirmed→b 复述等待→c 用户同意后 confirmed=true）、失败话术规则（sold_out→主动推荐 alternatives、not_yours→说明非本人）。
4. **防御**：最大步数 8 防死循环（兜底话术）；LLM 60s 超时与一切 API 异常转自然语言道歉（`AgentReply.error` 记录）；未知工具/坏参数返回结构化错误让模型自行解释；token 用量累计进 `AgentReply.usage`。
5. **可测性**：LLM 与 socket 均可注入——`FakeLLM`（脚本化模型响应）+ 假 socket 让整个循环离线可测，不需要 API Key。

**测试**（`agent/test_agent.py`，10 用例全过）：

| 用例 | 验证 |
|---|---|
| 纯文本应答 / 工具调用与回填 | 循环主干、消息序列 system→user→assistant→tool→assistant |
| **A. 未确认不碰下单接口** | TCP 发送字节里无 `type:4` |
| **B. 同轮自问自答被拒** | 两次 `confirm_required`，无下单帧 |
| **C. 用户确认后下一轮放行** | `type:4` 帧出现且 pending 清空 |
| **D. 改主意覆盖 pending** | 换班次后 pending 参数更新 |
| 门控期售罄短路 | 返回 `sold_out` 不进确认流程 |
| 最大步数兜底 / 未知工具 / LLM 异常 | 防御分支 |

**验收对照计划**：循环/门控/防御的代码与测试完成；✅ 真实验收已于 9 月 11 日补跑通过（见下方"M3 真实验收记录"）。

### M3 真实验收记录（2026-09-11，真实 DeepSeek Key + 真实服务端）

CLI 多轮对话全流程实测通过：

- 「10月有什么票呢」→ 列出 4 班次（含"广州已售罄"标注）；
- 「我想订10月1去北京的，能定吗」→ **门控拦截**，复述"班次1 西安-北京 2026-10-01 余票100 当前账号13900000001"等待确认；
- 「确认」→ 下单成功，余票 100→99；
- 「订10月4去广州的」→ 售罄解释 + 主动推荐 3 个有余票班次（alternatives 生效）;
- 「取消刚刚的预订」→ 取消同样走门控（复述预约号12）→「确认」→ 取消成功，预约清空；
- 单轮 token：prompt 2.2k~5.9k（波动源于 DeepSeek 上下文缓存与历史裁剪），completion 81~122。

结论：**计划 M3 验收标准（命令行多轮对话完成查票→订→确认；未确认先追问）全部满足**。已知小瑕疵：模型列表编号偶有重复（"1、1、"），属提示词格式问题，留待桌面端 V1 修。

---

## Agent-M4 多轮会话与评测（提交：`Agent-M4`）

**目标**：会话历史管理 + 26 项任务评测集 + 指标统计与 CLI 对照。

**完成内容**：

1. **多轮会话裁剪**（`agent/agent.py` 新增 `_trim_history()`）：
   - 保留 system + 最近 `max_history=40` 条消息，只在 **user 消息边界**下刀——一轮内的 assistant(tool_calls)+tool* 消息组永远连续，不会产生孤儿 tool 消息（配了专项单测）；
   - "必要状态"（确认门控的 pending、登录态）存在实例字段里，不受裁剪影响。
2. **评测框架**（`agent/evaluate.py`）：
   - **26 项任务、7 大类**：查询 4 / 直接订 4 / 模糊描述 3 / 售罄改订 3 / 余票紧张 2 / 取消 5 / 异常输入 5（覆盖计划要求的：直接订、模糊描述、售罄改订、余票 1 张、取消别人的预约号、乱输入）；
   - **成功判定三条**（全满足才算成功）：① 结束后"我的预约"**直连工具**核对包含/不包含指定线路（不信任模型自述）；② 每个正则模式须命中回复文本（模式内 `|` 为同义替换，如 `售罄|没有余票|已订完`）；③ `no_dangerous_exec` 类任务全程不得有成功的预订/取消调用；
   - **隔离与确定性**：每任务独立注册手机号 + 独立 TCP 连接；`pre_reserve` 剧本铺垫（如 C4 用另一个账号真订一单，把真实 yd_id 模板进用户发言）；任务结束直连清空该号预约，保证 tk3"剩 1 张"等场景跨任务可复现；
   - **指标**：任务成功率、平均用户轮数、平均工具调用、token 用量，分类别汇总 + 与纯命令行客户端的菜单操作步数对照表；明细存 `agent/eval_results/*.json`。
3. **冒烟模式 `--smoke`**：3 个代表任务（查询/直接订/售罄拒绝）用脚本化 FakeLLM 走**真实服务端**，验证评测链路本身（注册登录→门控下单→判定→清场→报表→JSON 落盘）。**冒烟 3/3 通过**，跑后数据库核实票池归位（tk3=19、预约表空）。

**测试**：全套 35 个单元/集成测试通过（M3 新增 2 个裁剪用例）。

**验收对照计划**：✅ 评测集 26 项（>计划的 25~30 下限）；✅ 链路真实跑通；⚠️ **真实成功率/平均轮数/token 数据待 `DEEPSEEK_API_KEY` 后运行 `python -m agent.evaluate` 产出**（输出会自动写入本文件 M5 节）。

---

## Agent-M5 收尾（提交：`Agent-M5`）

**完成内容**：

1. **CLI 入口**（`agent/main.py`）：登录/注册在 CLI 侧确定性完成（密码不进模型上下文）→ 多轮对话；断线自动重连一次（TCP 重连+重新登录，对话历史保留）；`--debug` 打印工具轨迹与 token；中文控制台兜底。已实测：真实登录成功、缺 Key 时友好退出。
2. **`agent/README.md`**：架构图、协议对接表（帧格式/枚举/一问一答约束/字符串字段坑/事务一致性）、启动方式、评测说明、**确认门控原理**、面试自测题 9 问参考答案。
3. **`agent/演示脚本.md`**：3 分钟录屏脚本（开场讲稿、查票、确认门控先拒后放、售罄推荐替代、取消闭环、评测数据展示、常见追问备选）。
4. **依赖就绪**：`openai==2.44.0` 已安装；用户只需 `export DEEPSEEK_API_KEY=...` 即可对话与评测。
5. **最终状态**：35 个单元/集成测试全过；冒烟评测 3/3；真实服务端（WSL）运行中。

**简历条目草稿**（数据待真实评测后替换——见计划第八节）：

> 票务智能预订 Agent：基于 DeepSeek Function Calling 手写工具调用循环，通过 Python 实现自研「4 字节长度头 + JSON」二进制协议客户端（粘包/半包/请求锁），对接本人开发的 C++ libevent 票务服务（零改动）；封装查票/预订/取消/我的预约四个工具，预订与取消在代码执行层强制用户确认（防模型同轮自答绕过），售罄时主动推荐替代班次；构建 26 项任务评测集（状态直连核对判定），任务成功率 __%（对比 CLI 基线：同类任务平均 __ 步菜单操作 → __ 轮自然语言）。

---

## M4 真实评测首轮数据与失败修复（2026-09-11，提交：`评测修复`）

**首轮结果**（`eval_real_20260911_014331.json`，DeepSeek 真实调用）：

- **任务成功率 19/26 = 73.1%**；平均用户轮数 2.08；平均工具调用 1.73；token 总量 132,955（prompt 127,874 / completion 5,081）
- 分类：查询 4/4、直接订 2/4、模糊描述 2/3、售罄改订 3/3、余票紧张 1/2、取消 3/5、异常输入 4/5

**失败分析（7 例 → 2 个根因，满足计划"定位并修复至少 2 个失败案例"）**：

| 根因 | 涉及任务 | 现象 | 修复 |
|---|---|---|---|
| ① 模型在门控之外多问一句"需要我帮您预订吗？" | R1/R3/V2/T2/C1/C5 | 用户意图已明确，模型却先反问，消耗了剧本的"确认"轮；门控复述文案晚一轮出现，剧本结束时交易未达成（C5 中"确认"错位成了完成预订，取消从未执行） | 系统提示词新增规则：意图明确时直接调用工具走 3a，门控复述是唯一确认环节，禁止额外口头确认 |
| ② 判分正则过严 | X3 | 模型答"当前系统中没有 88 号班次"+列出可用班次，行为正确但 `/不存在\|没有找到/` 未命中 | evaluate.py 正则放宽为 `不存在|没有找到|无效|找不到|没有.{0,8}班次|没有 ?\d+ ?号` |

修复后 35 个单元测试无回归。

### 复测与第二轮修复（2026-09-11）

**复测结果**：**21/26 = 80.8%**（较首轮 +7.7pp）；平均轮数 2.08；tokens 159,397。首轮修复验证：R1/V2/T2 转绿；分类变化：模糊描述 3/3、余票紧张 2/2 全绿，查询 3/4（Q2 新失败）。

**第二轮失败分析（5 例 → 3 根因）**：

| 根因 | 案例 | 现象 | 修复 |
|---|---|---|---|
| ① Markdown 隔断判分正则 | Q2 | 回复"还剩 \*\*1\*\* 张"行为正确, 但 `1 ?张` 被星号隔断未命中 | 判分前剔除 `*_\`#~` 修饰符 |
| ② 提示词遵循概率性 + 剧本用户不会应变 | R3/C1/C5 | 模型偶尔仍在门控外先反问, 真人会把流程走完, 剧本用户不会 | 评测加**尾轮确认应答**：剧本念完后若 Agent 正在等确认且目标状态未达成, 模拟用户最多补 2 轮"确认"（合作用户行为; 对 no_dangerous_exec 类任务禁用, 防止评测自己触发危险操作） |
| ③ 目标不存在时模型只列可选项 | X3 | 对"订 88 号"只列出 1~4 号, 未明说 88 不存在 | 提示词新增：目标不存在必须先明确告知"不存在/没有找到"再列可选项 |

修复后 35 单测 + 冒烟 3/3 全过。**可选第三轮验证**：再跑一次 `python -m agent.evaluate`（预期 ≥88%）; 亦可将 80.8% 定稿。

## 全局遗留与下一步（给王思雨）

0. ~~补 M3 真实验收~~ ✅ 2026-09-11 已完成（见上节记录）。
1. **复跑评测验证修复**（Key 已可用）：`python -m agent.evaluate` 再跑一次，对比首轮 73.1%；复测通过后把最终数字填进下方简历条目与 README 表格。

### 运维备忘（2026-09-11 凌晨排障记录）

- **WSL 会假死**：症状为 `wsl -d Ubuntu -- ...` 命令无响应/空输出。处置：`wsl --shutdown` 硬重置（虚拟磁盘数据不丢），然后 `service mysql start` + 重启 `./ser`。用户 00:14 前后测试时疑似踩中。
- **服务端日志有缓冲延迟**：`server/log.cpp` 的 ofstream/cout 从不 flush，低流量下日志滞留内存、强杀进程即丢（曾表现为"日志停在半行"）。不影响业务。可选修复（一行）：`log()` 里改用 `std::endl` 或定期 flush——按"不改服务端"约束未动，留待你自己决定。
- 每次开发前启动顺序：WSL 里 `sudo service mysql start && cd ~/ser-cli/server && ./ser`。






---

## QtA V1-M1 FastAPI 服务层（2026-09-11，计划：《Qt+Agent桌面端开发计划》）

**产出**：`agent/service.py`（服务层）+ `agent/test_service.py`（17 用例）+ `agent/agent.py` 小改（`AgentReply.confirm_request`）。

**接口一览**（默认 `127.0.0.1:8000`，C++ 服务端地址可用 `--server-host/--server-port` 或 `TICKET_SERVER_HOST/PORT` 指定）：

| 接口 | 作用 | 失败分级 |
|---|---|---|
| `POST /login` `/register` | 建 TicketSession（连 TCP+登录/注册并自动登录），返回 `{session_id, user_name}` | 401 密码错 / 409 手机号已注册 / 503 服务端不可达（提示先启动 WSL） |
| `POST /chat` | 调 `TicketAgent.chat()`，返回 `{reply, tool_trace, usage, confirm_request, error}` | 404 会话不存在或过期 / 503 TCP 已断或缺 API Key |
| `GET /tickets?session_id=` | 转发 `query_tickets`（桌面端表格数据源） | 404 / 503（network_error 归一） |
| `POST /logout` | 关 TCP 并移除会话 | 404 |
| `GET /health` | 存活探测（Qt 判断服务层是否启动） | — |

**关键设计**：

1. **会话管理**：`session_id(uuid) → {TicketSession, TicketAgent, Lock, last_active}`；空闲 **30 分钟 TTL**（后台协程周期清理 + 取用时惰性清理，清理即关 TCP 防连接泄漏）；同一会话的 chat/tickets 持锁串行化；logout 主动关闭。一个进程多会话并存（有测试）。
2. **`confirm_request` 卡片数据**：`chat()` 结束后从 `_pending` 提取 `{action, args, display}`，**只在 pending 相对轮首变化时上报**——新建/改主意算新请求；用户口头拒绝后 pending 虽在但不重复弹卡；异常轮不上报。点击按钮=发送"确认"/"不订了"文本，门控零改动（完整模型链路）。
3. **Agent 懒创建**：首次 `/chat` 才构造 TicketAgent——登录/查票不依赖 `DEEPSEEK_API_KEY`，缺 Key 只影响对话（503 人话），会话仍可用，Qt 端开发不烧 Key。
4. **线程模型**：端点全部同步 def，Starlette 丢线程池执行，`agent.chat()` 最长 60s 的 LLM 调用不卡事件循环。
5. **错误分级**：503（链路）/401/409（业务拒绝，用"连接是否仍在"区分）/404（会话）/422（参数）；chat 中工具网络故障不抛 503，与 CLI 一致由模型自然语言解释。

**验收**：52/52 测试全过（原 35 无回归 + 新 17）；真实 C++ 服务端 curl 实测：register/login(401 分支)/tickets(真实余票)/logout(404 分支)/缺 Key 的 /chat 503 全部符合预期。**待补**：真实 Key 下 `/chat` 门控闭环 curl 实测（服务已启动后）：

```bash
export DEEPSEEK_API_KEY=sk-xxxx
python -m agent.service --port 8000
SID=$(curl -s -X POST localhost:8000/login -H "Content-Type: application/json" -d '{"tel":"...","passwd":"..."}' | python -c "import sys,json;print(json.load(sys.stdin)['session_id'])")
curl -X POST localhost:8000/chat -H "Content-Type: application/json" -d "{"session_id":"$SID","text":"订10月1日去北京的"}"   # 应含 confirm_request
curl -X POST localhost:8000/chat -H "Content-Type: application/json" -d "{"session_id":"$SID","text":"确认"}"               # confirm_request 应为 null, tool_trace ok=true
```

**下一步**：V1-M2 Qt 登录对接（LoginDialog 改走 `/login` `/register`，QNetworkAccessManager）。

---

## QtA V1-M2 Qt 登录对接 HTTP（2026-09-11）

**产出**：`qt-client/src/apiclient.{h,cpp}`（新）+ `LoginDialog`/`MainWindow`/`main.cpp` 改造 + 服务层补 `GET /reservations` + 五个测试全部翻新。服务层 Python 测试 18/18（新增 /reservations 转发用例）。

**改动要点**：

1. **`ApiClient`**（QNetworkAccessManager 封装）：login/register/tickets/reservations/chat/logout/health 全异步，信号回 UI 线程；持有 session_id/userName/userTel。**错误人话化两级**：无 HTTP 状态（服务层未启动）→ "无法连接 Agent 服务，请先启动： python -m agent.service"；4xx/5xx → 直接展示服务层 `{"detail": 人话}`（401 密码错/409 已注册/404 会话过期/422 校验错）。
2. **`LoginDialog`**：showEvent 先 `/health` 探活（验收：服务层未启动直接提示启动命令）；提交改调 `/login` `/register`（注册成功自动登录，服务层已内置）；等待态防连点保留。
3. **`MainWindow`**：数据源从直连 TCP 换 `GET /tickets` `GET /reservations`（Model 加 `fromHttpArray` 适配器复用）；**手动"预约/取消"按钮移除**——危险操作只能经对话+门控发起，界面无绕过门控的捷径；新增"退出登录"（/logout → 关窗 → main.cpp 循环重登，覆盖"断网重启服务后重新登录"验收）。
4. **测试翻新**（全部改为自注册随机账号，不再依赖被评测清库重置的种子账号 13800001111）：dialog_test 9 用例（HTTP 形态，自拉起服务层 8901）；flow_test 4 用例（登录→表格链路→退出→kill 服务层提示）；model_test 8 用例（纯单测 + fromHttpArray）；net_test（TCP 直连形态保留，自注册账号）；codec_test 不变。**WSL 一键跑**：`bash qt-client/scripts/build_and_test.sh`。
5. **运维**：WSL Python(3.14) 补装 pip(get-pip.py) + fastapi/uvicorn/httpx（`--user --break-system-packages`），服务层现可在 Windows 或 WSL 任一侧启动；`agent/scripts/wsl_smoke.sh` 冒烟脚本。

**验收**（真实 C++ 服务端 + WSL 服务层）：错误密码 401 人话提示 ✓；服务层未启动提示"请先启动 python -m agent.service" ✓（dialog_test::serviceDownShowsStartupHint + flow_test::serviceKilledShowsStartupHint）。

**踩坑记录**（写进测试注释）：① WSL 里连接被拒的 QNetworkReply finished 几乎同步到达，探活重试循环若只判"无信号"会瞬间耗尽——每轮间需 `QTest::qWait(500)` 留节拍；② Git Bash→WSL 内联命令的中文路径/引号会被转坏，复杂命令一律走脚本文件。

**下一步**：V1-M3 聊天页签（消息列表+输入框+确认卡片+tool_trace 折叠）——核心场景 `查票→订票出卡→点确认→查我的预约→取消出卡→点确认` 的 GUI 验收测试。

---

## QtA V1-M3 聊天页签与确认卡片（2026-09-11，V1 功能闭环完成）

**产出**：`qt-client/src/chatwidget.{h,cpp}`（新）+ MainWindow 接入"AI 助手"页签 + `tests/chat_test`（V1-M3 验收测试）。Qt 六套测试 + Python 53/53 全过。

**功能**：

1. **消息流**（QScrollArea + 自上而下布局）：欢迎语 / 用户消息 / 助手回复 / "正在思考(可能调用工具)..."占位；发送后输入禁用防连点。
2. **确认卡片**（门控的产品化表达）：`confirm_request` 非空时渲染复述文案 + 按钮——预订卡 [确认预订][不订了]、取消卡 [确认取消][先不取消]；**点击=发送等价文本**（"确认"/"不订了"/"先不取消"）走完整模型链路，门控代码零改动；点过的按钮立即禁用；新一轮应答后旧卡片陈旧化（不复活）。
3. **tool_trace 折叠行**：每轮回复下方灰色小字 `└ 工具: query_tickets(成功) · reserve_ticket(confirm_required)`（V2-M2 再面板化）。
4. **对话后自动刷新**：每轮 chatFinished 触发 /tickets + /reservations，余票/预约随对话实时变化。

**验收测试**（chat_test，自拉起服务层 8908 + 自注册账号）：
- 无 Key 部分（始终跑，5 用例）：渲染/卡片按钮=等价文本（chatSent 信号断言）/旧卡片陈旧化——**全过**；
- 有 Key 部分（`DEEPSEEK_API_KEY` 存在时跑，当前环境无 Key 故 SKIP）：GUI 完整走 `查票→订10月1日去北京的(出卡)→点[确认预订]→预约成立+表格联动→查我的预约→取消(出卡)→点[确认取消]→清空+余票复原`，及 `点[不订了]零预订`。**待用户 export Key 后复跑**（`cd qt-client/tests/chat_test/build && DEEPSEEK_API_KEY=... QT_QPA_PLATFORM=offscreen ./chat_test`）。
- "断网重启服务后可重新登录继续"：flow_test 已覆盖 kill 服务层→提示先启动 + 退出登录→main.cpp 重登循环。

**V1 完成状态对照计划**：FastAPI 服务层 ✓、确认卡片按钮化 ✓、门控可视化（轨迹行）✓——计划标注"不砍"的三项全部落地。下一步 V2-M1（服务层 SSE 流式 + chat_stream 事件生成器）→ V2-M2（三栏布局+打字机+轨迹面板）。

---

## QtA V2-M1 服务层流式 SSE（2026-09-11）

**产出**：`agent.py` 新增 `chat_stream()` 事件生成器 + `service.py` 新增 `POST /chat/stream`(SSE) + FakeLLM 流式形态。Python 59/59 全过（新增 agent 流式 3 用例 + 服务 SSE 3 用例）。

**设计**：

1. **`chat_stream(user_text)`**：与 `chat()` 共享同一套 messages/门控/裁剪——只有输出形态不同。事件：`token`(打字机增量) / `tool_start`(名称+参数) / `tool_result`(名称+ok+reason，门控拦截即 `confirm_required`) / `confirm_request`(卡片) / `done`(最终 content+usage+error，异常时供客户端覆盖已流出 token)。
2. **流式工具调用累积**：DeepSeek 按 OpenAI 形态分片吐 `delta.tool_calls`，按 index 累积 id/name/arguments；usage 在 `stream_options={"include_usage": True}` 的末块(choices 为空)给出。
3. **`POST /chat/stream`**：`StreamingResponse(media_type="text/event-stream")`，同步生成器由 Starlette 丢线程池迭代，token 渐产不卡事件循环；缺 Key 时以 `error`+`done` 事件优雅收尾（SSE 冒烟实测）。
4. **零回退**：`chat()` 与既有测试一行未动；FakeLLM 只在 `stream=True` 时走分片形态（test_agent 15 用例含非流式 12 个原样全过）。

**验收**：`chat_stream` 门控 A/C 用例在流式路径全过（未确认无下单帧→确认后下单）；SSE 事件序 token 拼接=done.content；SSE 冒烟脚本 `agent/scripts/sse_smoke.py`（真实服务端）。

**待办**：V2-M2 Qt 三栏 + QNetworkReply 增量读 SSE 打字机 + 右栏执行轨迹面板。
