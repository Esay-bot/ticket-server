# 票务智能预订 Agent 包
# 结构(与开发计划对应):
#   protocol.py  M1 协议层: 4字节大端长度头 + JSON 的 TCP 客户端
#   tools.py     M2 工具层: 查票/预订/我的预约/取消 四个工具
#   agent.py     M3 Agent 循环: DeepSeek Function Calling + 确认门控
#   evaluate.py  M4 评测: 25+ 任务评测集与指标统计
#   main.py      M5 CLI 入口
