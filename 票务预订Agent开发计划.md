# 票务智能预订 Agent 开发计划（方案 A · 带做模式）

> **给新对话的使用方法**：在 `D:\桌面\my\ser-cli` 目录下打开 AI 编程助手，发送：
> 「请完整阅读 票务预订Agent开发计划.md，从 M0 开始，按"讲解 → 我动手 → 反馈 → 验收"的节奏带我做完，**不要一次生成全部代码**，核心代码由我自己写，你负责讲解、给骨架和验收。」

---

## 一、背景与目标

- **我是谁**：王思雨，大四，C++/Linux 后端基础，自己写过本项目的 C++ 服务端（libevent + 线程池 + MySQL），有一段 Qt 实习。
- **要做什么**：在**不改动 C++ 服务端一行代码**的前提下，用 Python 写一个智能预订 Agent——用户自然语言下单，Agent 通过工具调用完成 登录 → 查票 → 确认 → 预订 / 取消 的闭环。
- **核心卖点**：工具层是**用 Python 重新实现「4 字节大端长度头 + JSON」协议的 TCP 客户端，对接我自己写的 C++ 服务**。一句话讲清差异化："我的 Agent 的工具，是通过自定义二进制协议调用我自己开发的高并发 C++ 服务。"
- **工期**：3～4 天（每天 4～6 小时）。
- **硬性要求**：完成后必须能独立讲解每条执行路径；评测数据真实跑出来。

## 二、教学规则（AI 助手必须遵守）

1. 每个模块按 **讲解 → 我动手 → 反馈 → 验收** 推进；讲解包含目标、知识点、设计取舍。
2. **核心代码（协议编解码、工具层、Agent 循环）由我亲手写**，AI 只给类/函数骨架与提示，不给可直接复制的完整实现。
3. 卡住时先给思路和调试方法，我尝试 2 次仍不行再给答案。
4. 每个模块验收全过才进下一模块。
5. 每天结束复述：当天模块的输入、输出、状态变化、失败分支。

## 三、Windows 上跑 Linux 服务：用 WSL2（M0 解决，之后不再纠结）

C++ 服务端用的 libevent / `unistd.h` / `pipe()` 是 POSIX 接口，Windows 原生编译不了。**不要移植服务端**（改 Windows 就失去了这个项目的意义），用 WSL2 在 Windows 里跑一个真 Linux：

```bash
# PowerShell（管理员）安装，装完重启一次
wsl --install -d Ubuntu
```

进入 Ubuntu 后初始化环境（M0 的活，逐条执行）：

```bash
sudo apt update
sudo apt install -y g++ make libevent-dev libjsoncpp-dev libmysqlclient-dev mysql-server python3-pip
# 启动 MySQL 并建库
sudo service mysql start
sudo mysql -e "CREATE DATABASE Project_DB;"
sudo mysql Project_DB < init.sql          # init.sql 见第五节
# 把项目从 Windows 盘拷进 WSL（原生文件系统快很多）
cp -r /mnt/d/桌面/my/ser-cli ~/ser-cli && cd ~/ser-cli/server
g++ -std=c++11 *.cpp -levent -ljsoncpp -lmysqlclient -o ser
./ser &        # 监听 127.0.0.1:6000
```

要点：
- WSL2 里监听的 `127.0.0.1:6000`，**Windows 侧直接访问 `127.0.0.1:6000` 就能连**（默认端口转发）——Python Agent、命令行客户端、以后的 Qt 客户端都能直接用。
- MySQL 连接参数改 `ser.cpp` 里的初始化调用为自己设的账号密码（本地开发库）。
- 备选：如果之前学校服务器/虚拟机/云主机里环境都是好的，也可以直接用那台机器，只把 Agent 的目标 host 改掉即可；但 WSL2 最省事、离线可用。
- 每次开发前的启动顺序：`sudo service mysql start` → `cd ~/ser-cli/server && ./ser`。

## 四、现有服务端事实（以代码为准，不要臆测）

- 代码：`~/ser-cli/server`（服务端）、`~/ser-cli/client`（旧命令行客户端，可作对照）。
- 监听 `127.0.0.1:6000`；libevent 单线程 IO + 4 线程线程池。
- **协议**：`4 字节大端（网络序）长度头 + JSON 文本`（jsoncpp `toStyledString()` 生成，长度按字节数）。
- **约束**：响应**不回显**请求 type，服务端是严格一问一答 → **客户端同一时刻只能有一个未完成请求**，收到响应或超时后才能发下一个。
- 操作枚举（`type` 字段）：`Login=1, Register=2, View=3, Reserve=4, MyReserve=5, Cancel=6`。

| 操作 | 请求 | 成功响应 | 失败响应 |
|---|---|---|---|
| 登录 | `{type:1, user_tel, user_passwd}` | `{status:"OK", user_name}` | `{status:"ERR"}` |
| 注册 | `{type:2, user_tel, user_name, user_passwd}` | `{status:"OK"}` | `{status:"ERR"}` |
| 查票 | `{type:3}` | `{status:"OK", num, arr:[{tk_id,addr,max,num,use_date}]}` | `{status:"ERR"}` |
| 预订 | `{type:4, tel, index}`（index=tk_id） | `{status:"OK"}` | `{status:"ERR"}` |
| 我的预约 | `{type:5, tel}` | `{status:"OK", num, arr:[{yd_id,addr,use_date}]}` | `{status:"ERR"}` |
| 取消 | `{type:6, tel, index}`（index=yd_id） | `{status:"OK"}` | `{status:"ERR"}` |

## 五、数据库初始化 `init.sql`（按服务端代码推断，M0 时核对）

```sql
USE Project_DB;
CREATE TABLE user_info (
  id INT PRIMARY KEY AUTO_INCREMENT,
  tel VARCHAR(20), username VARCHAR(50), passwd VARCHAR(50), status INT DEFAULT 1
);
CREATE TABLE ticket_info (
  tk_id INT PRIMARY KEY AUTO_INCREMENT,
  addr VARCHAR(100), max INT, num INT DEFAULT 0, use_date VARCHAR(20)
);
CREATE TABLE reserve_ticket (
  yd_id INT PRIMARY KEY AUTO_INCREMENT,
  tk_id INT, tel VARCHAR(20), time DATETIME
);
INSERT INTO ticket_info (addr, max, num, use_date) VALUES
  ('西安-北京', 100, 0, '2026-10-01'),
  ('西安-上海', 50,  0, '2026-10-02'),
  ('西安-成都', 20,  19, '2026-10-03'),   -- 故意只剩 1 张，用于测"余票紧张"
  ('西安-广州', 80,  80, '2026-10-04');   -- 故意售罄，用于测"售罄追问"
```

## 六、技术选型（已定）

- Python 3.10+（WSL 自带）；LLM 用 **DeepSeek**（OpenAI 兼容接口，`openai` SDK，`base_url=https://api.deepseek.com`），支持 Function Calling。
- **先手写 Agent 循环（消息列表 + 工具 schema + 执行回填），不引任何框架**；LangGraph 留作做完后的可选扩展。
- TCP 用标准库 `socket`，超时 5 秒；API Key 放环境变量 `DEEPSEEK_API_KEY`，不进代码。
- 新目录：`~/ser-cli/agent/`（协议层、工具层、循环、评测各一个文件）。

## 七、模块划分与验收

### M0 环境（半天）
- WSL2 + MySQL + 服务端按第三节跑通；`init.sql` 导入；用旧命令行客户端完成一次 登录→查票→预订→取消。
- **验收**：命令行闭环成功；`ss -lnt | grep 6000` 有监听。

### M1 Python 协议客户端（核心，我亲写）
- `TicketClient` 类：`connect()`、`request(dict) -> dict`、`close()`。
- 编码：`struct.pack('>I', len(body)) + body`；解码：接收缓冲累积，先凑齐 4 字节长度，再凑齐正文——**处理粘包与半包**，非法长度（0 / > 4096）报错。
- 请求锁：同一时刻只允许一个在途请求（对照第四节约束）。
- **验收**：单元测试覆盖 半包分两次到达、两包粘连、非法长度；真实调用 View 成功。

### M2 工具封装
- 把四个业务包成带 docstring 的函数：`query_tickets()`、`reserve_ticket(tk_id)`、`my_reservations()`、`cancel_reservation(yd_id)`；登录态（tel、user_name）由 Agent 会话持有。
- 每个工具返回**结构化结果**（含失败原因：售罄 / 未登录 / 无此票），供模型判断下一步。
- **验收**：脚本顺序调用四个工具全通；售罄票返回明确失败原因。

### M3 Agent 循环（核心，我亲写）
- DeepSeek Function Calling：系统提示词（角色、确认规则、边界）、四个工具的 JSON Schema、循环：模型响应 → tool_calls → 执行 → 结果回填 → 继续，最大步数 8、超时与异常分支。
- **确认门控**：预订 / 取消必须先向用户复述（班次、日期）并得到"确认"才执行；余票不足或售罄时主动提供替代班次；任何工具失败要有自然语言解释。
- **验收**：命令行多轮对话完成 "查周五去北京的票" → "订倒数第二个" → 确认 → 成功；未确认直接说"订"时 Agent 会先追问。

### M4 会话与评测
- 多轮会话（保留最近 N 轮 + 必要状态）；评测集 **25～30 个任务**（直接订 / 模糊描述 / 售罄改订 / 余票 1 张 / 取消别人的预约号 / 乱输入），记录任务成功率、平均轮数、token 用量，与"纯命令行操作"做对照表。
- **验收**：跑出真实数据表，至少定位并修复 2 个失败案例。

### M5 收尾（半天）
- 简单 CLI 或 FastAPI 接口（二选一，别贪）；`agent/README.md`（架构图、协议对接说明、启动方式、评测结果）；3 分钟演示录屏。
- 对照第八节核对简历条目；完成第九节自测题。

## 八、简历预写条目（完成后填真实数据）

> 票务智能预订 Agent：基于 DeepSeek Function Calling 手写工具调用循环，通过 Python 实现自研「4 字节长度头 + JSON」二进制协议客户端，对接本人开发的 C++ libevent 票务服务；封装查票 / 预订 / 取消 / 我的预约四个工具，预订与取消强制用户确认，售罄时主动推荐替代班次；构建 30 项任务评测集，任务成功率 __%（对比基线 __%）。

**先把项目做出来再替换简历里的电商项目**；评测数字必须真实跑出来的。

## 九、面试自测题（M5 结束全答）

1. Function Calling 的完整链路：模型返回什么、谁执行、结果怎么回填？
2. 你的工具层为什么要自己实现协议？粘包半包在 Python 端怎么处理的？
3. 为什么"同一时刻只允许一个在途请求"？服务端哪个设计导致了这个约束？
4. 预订前的用户确认是在哪一层实现的？为什么不能靠模型自觉？
5. 余票扣减的一致性由谁保证？（答：C++ 服务端事务，模型/工具层不碰）——这个答案为什么重要？
6. 最大步数、超时、异常各防什么？死循环怎么兜底？
7. 你的评测集怎么设计的？"任务成功"的判定标准是什么？
8. 这个项目和 RAG、和固定工作流的区别是什么？
9. 如果把 DeepSeek 换成别的模型，你要改哪些地方？

## 十、时间参考

| 天 | 内容 |
|---|---|
| 第 1 天 | M0（环境）+ M1（协议层，时间花够） |
| 第 2 天 | M2 + M3（Agent 循环是灵魂） |
| 第 3 天 | M4（评测出数据） |
| 第 4 天 | M5（收尾 + 自测 + 更新简历） |

落后时优先砍：FastAPI 界面（保 CLI）、替代班次推荐（保基本追问）；**不砍 M1 协议层、M3 确认门控、M4 评测**——这三块是简历条目的技术含金量所在。
