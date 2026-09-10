# 票务智能预订 Agent

> 一句话讲清差异化：**这个 Agent 的工具，是通过自研「4 字节大端长度头 + JSON」二进制协议，直接调用我自己开发的 C++ libevent 高并发票务服务**——不改服务端一行代码。

自然语言下单闭环：**登录 → 查票 → 确认 → 预订 / 取消**。LLM 用 DeepSeek（OpenAI 兼容接口，Function Calling），Agent 循环手写，不引任何框架。

## 架构

```
 用户(自然语言, CLI)
     │  python -m agent.main
     ▼
 TicketAgent ── agent/agent.py ──────────────────────────────
 │  消息列表 + tools schema + 执行回填的手写循环               │
 │  确认门控(代码层): 预订/取消必须 用户确认 后才放行            │──▶ DeepSeek API
 │  防御: 最大步数8 / 超时60s / 异常转自然语言道歉               │    (Function Calling)
 ▼
 四个工具 ────── agent/tools.py
 │  query_tickets / reserve_ticket / my_reservations / cancel_reservation
 │  结构化失败原因: sold_out / not_yours / not_logged_in / ...
 ▼
 TicketClient ── agent/protocol.py
 │  4字节大端长度头 + utf-8 JSON; 粘包/半包; 请求锁; 5s超时
 ▼  TCP 127.0.0.1:6000 (WSL2 localhost 端口转发)
 C++ libevent 服务端(本项目 server/, 零改动)
   单线程 IO + 4 线程业务池 + MySQL 连接池 + 事务扣票
```

## 协议对接说明（以服务端源码为准）

| 项 | 内容 | 出处 |
|---|---|---|
| 帧格式 | `4 字节网络序长度 + JSON 文本`（长度按 utf-8 字节数） | `server/connection.cpp` |
| 枚举 | `Login=1 Register=2 View=3 Reserve=4 MyReserve=5 Cancel=6` | `server/threadpool.h` |
| 响应约束 | **严格一问一答且不回显请求 type** → 客户端同一时刻只允许一个在途请求（`protocol.py` 用请求锁保证） | `server/threadpool.cpp` |
| 长度合法性 | 0 或 >4096 视为失步，客户端断连 | 对照 `client/client.cpp` |
| 字段类型坑 | 响应中 `tk_id/max/num/yd_id` 是**字符串**（MySQL 行值直接赋给 `Json::Value`），工具层统一 `int()` | `server/db_manager.cpp` |
| 一致性 | 余票扣减在 C++ **事务**里完成，Agent/工具层不碰库存 | `db_manager.cpp reserveTicket` |
| 不防重复 | 服务端允许同一手机号重复订同一班次（每次新 yd_id） | 同上 |

## 启动方式

```bash
# 1) WSL 里起后端(每次开发前):
wsl -d Ubuntu
sudo service mysql start
cd ~/ser-cli/server && ./ser        # 监听 127.0.0.1:6000
#   (首次: g++ -std=c++14 *.cpp -levent -ljsoncpp -lmysqlclient -o ser
#    建库: mysql -uroot -p211925 < sql/init.sql, 或 bash agent/scripts/reset_db.sh)

# 2) Windows 侧(本项目目录):
pip install -r agent/requirements.txt
export DEEPSEEK_API_KEY=sk-xxxx     # Windows CMD: set DEEPSEEK_API_KEY=...
python -m agent.main --debug        # 登录后自然语言对话
```

## 测试与评测

```bash
python -m unittest discover -s . -p "test_*.py"   # 35 个单元/集成测试(无需 API Key)
python -m agent.evaluate --smoke                  # 评测链路冒烟(真实服务端, 无需 Key)
python -m agent.evaluate                          # 26 项任务真实评测(需 Key)
python -m agent.evaluate --category 售罄改订      # 只跑一类
```

评测集 26 项 / 7 类（查询、直接订、模糊描述、售罄改订、余票紧张、取消、异常输入），成功判定 =
**最终预约状态直连核对** + **关键话术命中** + **危险操作零误执行**；任务间自动清场保持票池确定（tk3 恒剩 1 张、tk4 恒售罄）。

| 评测 | 结果 | 说明 |
|---|---|---|
| 冒烟(3 任务, FakeLLM) | **3/3 = 100%** | 验证链路: 注册→门控下单→判定→清场 |
| 真实首轮(26 任务, DeepSeek) | **19/26 = 73.1%** | 平均 2.08 轮/任务, 平均 1.73 次工具调用, 132,955 tokens |
| 真实复测(修复后) | 待跑 | 修复: ①提示词禁止门控外口头反问 ②X3 判分正则放宽, 明细在 `eval_results/` |

## 确认门控（本项目技术亮点）

预订/取消有真实副作用，因此闸门放在**代码执行层**而非提示词"拜托"模型：

1. 模型调用 `reserve/cancel` 未带 `confirmed=true` → 执行器**不碰 TCP**，记 pending 并返回复述文案；
2. `confirmed=true` 放行需同时满足：**pending 参数一致** 且 **pending 建立后用户又发过消息**（模型无法同轮"自问自答"）；
3. 参数不一致 → 覆盖 pending（用户改主意）；门控期发现售罄/非本人预约 → 直接短路返回原因。

## 目录

```
agent/
├── protocol.py      M1 协议层: TicketClient(粘包/半包/请求锁/超时)
├── test_protocol.py M1 测试: 12 用例(含真实 View 调用)
├── tools.py         M2 工具层: 四工具 + 结构化失败原因 + TicketSession
├── test_tools.py    M2 测试: 11 用例(含真实全流程)
├── agent.py         M3 Agent 循环 + 确认门控 + 防御
├── test_agent.py    M3/M4 测试: 12 用例(FakeLLM, 门控 A/B/C/D)
├── evaluate.py      M4 评测: 26 任务 / 判定 / 指标 / CLI 对照
├── main.py          M5 CLI 入口
├── scripts/reset_db.sh   评测前重置数据库
└── eval_results/         评测明细 JSON
```

## 面试自测题参考答案（速查）

1. **Function Calling 链路**：模型返回 `tool_calls`（名称+JSON 参数）→ **我自己的代码**执行工具（不是模型）→ 结果以 `role:"tool"`+`tool_call_id` 回填 messages → 再次调模型直到给出文本。见 `agent.py _run_loop`。
2. **为何自实现协议**：服务端是自定义二进制协议，任何 SDK 都不认识；粘包半包在 `try_extract_frame` + `_recv_one_frame` 用"累积缓冲、先凑头再凑正文"处理，与 C++ 端 `handleRead` 循环拆包同构。
3. **单在途请求**：服务端一问一答且响应不回显请求 type，无请求标识 → 并发交错必配错响应；`TicketClient` 用互斥锁把"发送→接收→解析"整体串行化。
4. **确认在哪层**：工具执行器（代码层），因为提示词对模型只是"建议"，模型可能跳过；代码层闸门模型绕不过（见上节）。
5. **余票一致性**：C++ 服务端事务保证（查询-判断-更新-插入原子提交）；这个答案重要在说明**Agent 不重新发明一致性**，职责边界清晰。
6. **防御**：最大步数 8 防工具死循环；LLM 60s 超时防挂起；一切异常转道歉话术防崩溃；门控防误操作。
7. **评测设计**：26 任务 7 类，判定=最终状态直连核对（不信模型自述）+关键话术+危险操作零执行；每任务独立账号并自动清场。
8. **与 RAG/固定流程的区别**：RAG 是"检索增强生成知识"，本项目是"工具调用执行动作"；固定工作流写死路径，Agent 循环由模型按上下文决定下一步（受门控约束）。
9. **换模型**：只改 `base_url/api_key/model` 三处——循环用的是 OpenAI 兼容标准协议；若新模型不支持 parallel tool calls 或参数风格差异，仅需调 `_run_loop` 的解析分支。
