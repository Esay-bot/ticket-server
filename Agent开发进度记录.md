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
