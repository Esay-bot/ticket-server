# 票务 Agent · Qt 桌面端开发计划（V1 ~ V3）

> **定位**：给已完成的 Python 票务 Agent（`agent/`，见 `Agent开发进度记录.md`）做桌面演示端。
> 真实演示不能用终端聊，要做能点、能看、能讲的功能闭环界面；同时中间加一层 **FastAPI 服务化**，
> 一份工程换两份收益：既得到界面，又练到 Agent 岗 JD 高频技能（HTTP 服务 / SSE 流式 / 会话管理）。
>
> **执行方式**：沿用票务 Agent 的节奏——按模块推进，每模块完成即 git 提交（前缀 `QtA-`），并更新 `Agent开发进度记录.md`。
>
> **与其他计划的关系**：
> - 依赖：`票务预订Agent开发计划.md`（已完成 M0~M5）；
> - 衔接：`UI美化Agent开发计划.md` 是**另一个独立项目**（视觉模型自动美化 QSS 的 Agent），排在本计划 V3 之后，恰好以本桌面端为"靶子程序"，不在这里展开。

---

## 一、目标与架构

**一句话**：Qt 桌面端聊天界面 + 确认门控按钮化 + Agent 执行过程可视化，让面试官"看见" Function Calling 循环在跑。

```
Qt 桌面客户端(本计划, qt-client/ 扩展)
 │  HTTP + SSE(流式)
 ▼
FastAPI 本地服务(本计划, agent/service.py)   ←── 服务化层, 顺带练岗位技能
 │  会话管理: session_id ⇄ (TicketSession + TicketAgent)
 │  复用 agent/ 现有全部代码(协议/工具/循环/门控)
 ▼
C++ libevent 服务端(零改动, WSL :6000) ⇄ MySQL
```

**关键决策与理由**：

| 决策 | 理由 |
|---|---|
| 中间加 FastAPI，而不是 Qt 用 QProcess 直挂 Python | ① Agent 岗需要"服务化"经历（SSE/会话管理）；② 界面与 Agent 解耦，以后换 Web 前端零成本；③ QProcess 裸管道只得到界面，学不到东西 |
| 确认按钮 = 发送"确认"文本，走完整模型链路 | 门控逻辑**零改动**；顺带持续验证模型对自然语言确认的理解（与评测一致）；比加专用 /confirm 端点绕过模型更真实 |
| 登录/注册走 FastAPI（而非 Qt 直连 TCP） | 会话的 TCP 连接与登录态在 Python 侧的 TicketSession 里，Qt 只会 HTTP，边界干净 |
| 流式分两步：V1 先非流式 JSON，V2 再上 SSE 打字机 | 先跑通功能闭环，再演示增强；流式改造只动服务层与渲染，不返工 |
| 票务表格数据走 GET /tickets | 复用已有 `TicketTableModel`，数据源从直连 TCP 换成 HTTP，一个适配层搞定 |

**现有资产盘点（直接复用，不重写）**：

- `agent/`：`TicketClient`（协议）、`TicketSession`（登录态）、`TicketAgent.chat()`（循环+门控，含 `tool_trace`/`usage`）、全部测试；
- `qt-client/src/`：`LoginDialog`（登录/注册表单）、`TicketTableModel` / `ReserveTableModel`（Model/View 表格）、`MainWindow` 骨架、`tests/`（五个测试）；
- 服务端 + MySQL + 评测集照旧。

---

## 二、模块划分与验收

### W0 前置补课（1 小时，不阻塞界面开发，随时插入）

- 拿到 `DEEPSEEK_API_KEY` 后跑 `python -m agent.evaluate`，把真实成功率/轮数/token 填进进度记录与 README（简历数字）。
- **验收**：`eval_results/` 产出真实评测 JSON；README 评测表不再"待跑"。

### V1 功能版（2~3 天）：服务层 + 聊天界面 + 确认按钮化

- **V1-M1 FastAPI 服务层**（`agent/service.py` + `agent/test_service.py`）：
  - `POST /login` / `POST /register` → 建 `TicketSession`（连 TCP、登录），返回 `session_id` + `user_name`；会话字典 + TTL 清理（空闲 30 分钟关连接）；
  - `POST /chat {session_id, text}` → 调 `TicketAgent.chat()`，返回 `{reply, tool_trace, usage, confirm_request}`；
  - `GET /tickets {session_id}` → 转发 `query_tickets`；`POST /logout`；服务端不可达时统一 503 + 人话错误；
  - **小改 agent.py**：`AgentReply` 增加 `confirm_request` 字段（从 `agent._pending` 的复述文案提取，无 pending 为 null）。
  - **验收**：curl/httpie 全接口跑通；pytest 覆盖 会话建立/聊天/门控字段/503 分支；一个进程多会话并存。
- **V1-M2 Qt 登录对接**：`LoginDialog` 的提交从 TcpClient 改调 `/login` `/register`（QNetworkAccessManager），失败原因展示；登录成功进入主窗口。
  - **验收**：错误密码有明确提示；服务层未启动时提示"请先启动 agent 服务"。
- **V1-M3 聊天视图与确认卡片**：`MainWindow` 增加聊天页签（消息列表 + 输入框 + 发送）；`confirm_request` 非空时渲染**确认卡片**（复述文案 + [确认预订] [取消] 按钮）；点击按钮等价于发送"确认"/"不订了"；`tool_trace` 以折叠文本显示（V2 再做面板化）。
  - **验收（本计划的核心场景）**：GUI 完整走 `查票 → 订票(出卡片) → 点确认 → 成功 → 查我的预约 → 取消(出卡片) → 点确认`；点"取消"按钮不产生预订；断网重启服务后可重新登录继续。

### V2 演示增强版（1~2 天）：流式打字机 + 三栏布局 + 执行轨迹

- **V2-M1 服务层流式**：DeepSeek `stream=True`；`agent.py` 增加 `chat_stream()` 事件生成器（token 增量 / 工具调用开始与结果 / 确认卡片 / 完成+usage），复用同一套 messages 与门控；`GET /chat/stream` 以 SSE（`text/event-stream`）输出；CLI 的 `chat()` 与既有测试不回退（FakeLLM 加流式形态）。
  - **验收**：curl -N 能看到逐 token 输出与工具事件；门控测试在流式路径同样全过。
- **V2-M2 Qt 三栏布局与打字机**：左栏车票表格（`TicketTableModel` 数据源接 `/tickets`，每次对话结束后自动刷新）；中栏聊天流（SSE 增量渲染，打字机效果）；右栏**执行轨迹面板**：实时滚动显示 `工具调用(名称/参数/结果/门控状态) → token 计数`——把 Function Calling 循环"演"给观众看。
  - **验收**：一次"订10月1日去北京的"对话中，右栏能看到 `query_tickets → reserve_ticket(门控拦截,未放行) → 确认后放行` 的完整轨迹；表格余票在预订成功后自动变化。
- **V2-M3 演示收口**：按 `agent/演示脚本.md` 重录桌面版 3 分钟演示（门控卡片 + 轨迹面板是主角）。
  - **验收**：录屏完成；不写代码的观众能看懂"Agent 每一步在干什么"。

### V3 轻量美化（1 天，可整体推后）

- 统一 QSS 主题（浅色、消息气泡、确认卡片高亮、表格斑马纹）、窗口图标与标题、关于页、高 DPI 适配；不引入新的结构改动。
- **验收**：界面截图能直接放简历附件/答辩 PPT，不需要解释。
- **明确不做**：自动化美化流程——那是 `UI美化Agent开发计划.md`（`ui-agent/`，视觉模型 + QSS 迭代）的范畴，以本桌面端为靶子，作为下一个独立项目启动。

---

## 三、时间参考

| 天 | 内容 |
|---|---|
| 第 1 天 | W0（评测补课）+ V1-M1（FastAPI 服务层 + 测试） |
| 第 2 天 | V1-M2 + V1-M3（Qt 登录/聊天/确认卡片，核心闭环） |
| 第 3 天 | V2-M1 + V2-M2（SSE 流式 + 三栏 + 轨迹面板） |
| 第 4 天 | V2-M3（录屏）+ V3（轻量美化）+ 进度记录收尾 |

**落后时砍单顺序**（从先砍到不砍）：

1. V3 整体推后（界面朴素不致命）；
2. 打字机流式退化为一次性返回（SSE 骨架保留，V2-M1 部分完成也可用）；
3. 表格自动联动退化为手动刷新按钮；
4. **不砍**：FastAPI 服务层、确认卡片按钮化、门控可视化轨迹——这三个是"Qt+Agent 对求职的真正价值"。

## 四、对求职的价值映射（做之前想清楚为什么）

| 模块 | 面试话术 |
|---|---|
| FastAPI 服务层 | "Agent 服务化：会话管理、SSE 流式、错误分级"——Agent 岗 JD 高频词 |
| 确认卡片 | "代码层门控在产品形态上的表达：模型只能请求，用户点击才放行" |
| 执行轨迹面板 | "把 Function Calling 循环可视化，演示时观众能看到模型每一步调了什么工具、门控何时拦截" |
| 三端同构 | "同一套自研协议服务了三种前端：CLI、Qt、自然语言 Agent" |

## 五、面试自测题（V2 结束答）

1. SSE 和 WebSocket 的取舍？为什么流式输出选 SSE？（单向、HTTP 生态、自动重连语义简单）
2. 多用户会话怎么管理的？连接泄漏怎么防？（session 字典 + TTL；logout 主动关 TCP）
3. 确认卡片点击后发生了什么？为什么这样设计而不直接调工具？（按钮→用户消息"确认"→模型→confirmed=true→门控放行；保证走的仍是完整真实链路）
4. Qt 端解析 SSE 用什么？和主线程怎么交互？（QNetworkAccessManager 增量读 + 信号槽回到 UI 线程）
5. 流式改造动了哪些层？为什么 CLI 和测试没回退？（事件生成器与 chat() 共享 messages/门控，只是输出形态不同）
