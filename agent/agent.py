"""M3 Agent 循环: 手写 DeepSeek Function Calling 工具调用循环(不引框架)。

链路(面试自测题1的答案即此文件的执行路径):
  用户输入 -> messages 追加 -> LLM(chat.completions.create, 携带 tools schema)
    -> 无 tool_calls: 返回内容文本(一轮结束)
    -> 有 tool_calls: 逐个交给执行器 -> 结果以 role:"tool" 回填 messages
       -> 继续下一轮 LLM 调用, 直到模型给出文本或达到最大步数

确认门控(代码层强制, 不依赖模型自觉 —— 面试自测题4):
  服务端一旦收到 type=4/6 就产生真实扣减/删除副作用, 因此在"模型想调用"
  与"TCP 真正发包"之间加一道由本文件控制的闸门:
    1) 模型调用 reserve_ticket/cancel_reservation 而未带 confirmed=true:
       执行器不碰 TCP, 查询该动作详情后记为 pending, 返回
       {"reason": "confirm_required", "confirm_display": 复述文案};
       模型把复述文案转达给用户并等待答复。
    2) confirmed=true 只有在 【pending 存在且参数一致】且【pending 创建后
       用户又发过至少一条消息】 时才放行 —— 模型无法在同一用户轮次内
       "自问自答"绕过确认。
    3) 新的危险调用参数与 pending 不一致: 视为用户改主意, 覆盖 pending。
  因此无论模型怎么"想", 没有用户的实际参与, type=4/6 永远发不出去。

防御: 最大步数 8 防死循环; LLM 调用 60s 超时; API 异常转自然语言道歉
而非崩溃; 未知工具名返回错误结果让模型自行解释。

依赖: pip install openai;  环境变量 DEEPSEEK_API_KEY(不进代码/仓库)。
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field

from agent.tools import (
    TOOL_REGISTRY,
    TicketSession,
    cancel_reservation,
    my_reservations,
    query_tickets,
    reserve_ticket,
)

DEEPSEEK_BASE_URL = "https://api.deepseek.com"
DEFAULT_MODEL = "deepseek-chat"
DEFAULT_MAX_STEPS = 8
DEFAULT_MAX_HISTORY = 40   # M4: 多轮会话最多保留的消息条数(system 之外)
LLM_TIMEOUT_SECONDS = 60

# 暴露给 LLM 的工具 JSON Schema(Function Calling)
TOOLS_SCHEMA = [
    {
        "type": "function",
        "function": {
            "name": "query_tickets",
            "description": "查询当前全部车票: 班次编号、线路、日期、总票数、已订数、余票。回答任何关于「有哪些票/余票」的问题前先调用它。",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "reserve_ticket",
            "description": "为当前登录用户预订一个班次。第一步不带 confirmed 参数调用(返回复述文案); 用户明确确认后, 携带 confirmed=true 再次调用才真正下单。同一时间只处理一个班次。",
            "parameters": {
                "type": "object",
                "properties": {
                    "tk_id": {"type": "integer", "description": "班次编号, 必须来自 query_tickets 的结果, 禁止编造"},
                    "confirmed": {"type": "boolean", "description": "用户已明确确认后才为 true"},
                },
                "required": ["tk_id"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "my_reservations",
            "description": "查询当前用户的全部预约记录(预约号、班次、日期)。取消前、以及回答「我订了什么」时调用。",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "cancel_reservation",
            "description": "取消当前用户的一条预约。第一步不带 confirmed 参数调用(返回复述文案); 用户明确确认后, 携带 confirmed=true 再次调用才真正取消。只能取消本人预约。",
            "parameters": {
                "type": "object",
                "properties": {
                    "yd_id": {"type": "integer", "description": "预约编号, 必须来自 my_reservations 的结果"},
                    "confirmed": {"type": "boolean", "description": "用户已明确确认后才为 true"},
                },
                "required": ["yd_id"],
            },
        },
    },
]

SYSTEM_PROMPT_TEMPLATE = """你是一个票务预订助手, 通过工具调用操作真实的票务预订系统(C++ 服务端)。

当前登录用户: 手机号 {tel}, 用户名 {user_name}。工具会自动携带该用户身份, 无需再向用户索要账号密码。

【必须遵守的规则】
1. 只做与票务相关的事: 查票、订票、查我的预约、取消预约。用户要求其他事情时礼貌拒绝。
2. 班次编号 tk_id 只能来自 query_tickets 的结果, 预约编号 yd_id 只能来自 my_reservations 的结果, 严禁编造或猜测。
3. 订票/取消是两步操作:
   a. 第一次调用 reserve_ticket/cancel_reservation 不带 confirmed, 系统会返回 confirm_display 复述文案, 你必须把它原样转达给用户(包含班次/日期/预约号), 然后停下等待用户答复;
   b. 用户明确同意(如"确认/订吧/是的/可以")后, 携带 confirmed=true 重新调用同一工具完成操作;
   c. 用户拒绝或改变主意, 就不要执行, 视情况重新查票或结束。
4. 工具返回 ok=false 时, 用自然语言向用户解释失败原因, 不要暴露 reason 码本身:
   - sold_out: 说明已售罄, 并主动从 alternatives 里推荐有余票的替代班次(附日期和余票);
   - not_yours: 说明该预约号不存在或不属于本账号;
   - not_logged_in: 说明需要先登录;
   - 其他: 如实说明失败, 建议用户稍后再试。
5. 回答简洁, 使用中文。列表用一行一条的格式展示: 编号、线路、日期、余票。
"""


@dataclass
class AgentReply:
    """一轮 chat() 的结果。tool_trace 供评测与调试。"""
    content: str
    tool_trace: list = field(default_factory=list)   # [{name, args, ok, reason}]
    usage: dict = field(default_factory=dict)        # {"prompt_tokens", "completion_tokens", "total_tokens"}
    error: str | None = None                         # 非空表示本轮异常终止


class TicketAgent:
    """单用户多轮会话 Agent。

    生命周期与 TicketSession 一致: 同一 TCP 连接、同一登录态;
    messages 由本对象持有, 跨轮保留(裁剪策略见 M4)。
    """

    def __init__(self, session: TicketSession, llm=None,
                 model: str = DEFAULT_MODEL, max_steps: int = DEFAULT_MAX_STEPS,
                 max_history: int = DEFAULT_MAX_HISTORY):
        if llm is None:
            llm = self._make_llm()
        self.llm = llm
        self.model = model
        self.max_steps = max_steps
        self.max_history = max_history
        self.session = session
        self.messages: list[dict] = [{
            "role": "system",
            "content": SYSTEM_PROMPT_TEMPLATE.format(
                tel=session.tel, user_name=session.user_name or "未知"),
        }]
        # 确认门控状态
        self._pending: dict | None = None             # {"tool", "args", "display"}
        self._user_turns_since_pending = 0

    @staticmethod
    def _make_llm():
        api_key = os.environ.get("DEEPSEEK_API_KEY")
        if not api_key:
            raise RuntimeError(
                "未设置环境变量 DEEPSEEK_API_KEY, 无法调用 DeepSeek; "
                "测试注入可用 TicketAgent(session, llm=FakeLLM(...))")
        from openai import OpenAI
        return OpenAI(api_key=api_key, base_url=DEEPSEEK_BASE_URL,
                      timeout=LLM_TIMEOUT_SECONDS)

    # ---- 对外主入口 -------------------------------------------------------

    def chat(self, user_text: str) -> AgentReply:
        """处理一条用户消息, 返回 Agent 应答(可能内部完成多次工具调用)。"""
        self.messages.append({"role": "user", "content": user_text})
        if self._pending is not None:
            self._user_turns_since_pending += 1

        reply = AgentReply(content="")
        try:
            reply = self._run_loop()
        except TimeoutError:
            reply = AgentReply(content="抱歉, 模型响应超时了, 请稍后再试一次。",
                               error="llm_timeout")
        except Exception as e:  # openai.APIError 等一切异常: 道歉而非崩溃
            reply = AgentReply(content=f"抱歉, 调用模型出错: {e}", error=str(e))
        self.messages.append({"role": "assistant", "content": reply.content})
        self._trim_history()
        return reply

    def _trim_history(self) -> None:
        """M4 会话裁剪: 保留 system + 最近 max_history 条消息。

        只在 user 消息边界上裁剪 —— 一轮对话的 assistant(tool_calls)+tool*
        消息组永远连续, 从 user 处下刀不会产生孤儿 tool 消息;
        确认门控的 pending 状态在消息之外, 不受裁剪影响("必要状态")。
        """
        msgs = self.messages
        if len(msgs) <= self.max_history:
            return
        # 从早到晚找第一个 user 边界, 使裁剪后长度进入预算(保留 system 占 1 条)
        for i in range(1, len(msgs)):
            if msgs[i]["role"] == "user" and len(msgs) - i + 1 <= self.max_history:
                self.messages = [msgs[0]] + msgs[i:]
                return

    # ---- 主循环 -----------------------------------------------------------

    def _run_loop(self) -> AgentReply:
        reply = AgentReply(content="")
        for _ in range(self.max_steps):
            resp = self.llm.chat.completions.create(
                model=self.model,
                messages=self.messages,
                tools=TOOLS_SCHEMA,
            )
            self._accumulate_usage(reply, resp)
            msg = resp.choices[0].message
            tool_calls = getattr(msg, "tool_calls", None)

            if not tool_calls:
                reply.content = (msg.content or "").strip()
                return reply

            # 模型要求调用工具: 原样入栈(含 tool_calls 结构), 逐个执行并回填
            self.messages.append({
                "role": "assistant",
                "content": msg.content or "",
                "tool_calls": [
                    {"id": tc.id, "type": "function",
                     "function": {"name": tc.function.name,
                                  "arguments": tc.function.arguments}}
                    for tc in tool_calls
                ],
            })
            for tc in tool_calls:
                result, trace = self._dispatch(tc.function.name, tc.function.arguments)
                reply.tool_trace.append(trace)
                self.messages.append({
                    "role": "tool",
                    "tool_call_id": tc.id,
                    "content": json.dumps(result, ensure_ascii=False),
                })

        # 步数用尽仍未给出文本: 兜底话术, 防死循环伤害用户体验
        reply.content = "抱歉, 这个请求的处理步骤过多, 请换个说法或稍后再试。"
        reply.error = "max_steps_exceeded"
        return reply

    # ---- 工具执行与确认门控 -------------------------------------------------

    def _dispatch(self, name: str, arguments_json: str) -> dict:
        """执行一个工具调用。返回 (给模型看的结果dict, 调试trace)。"""
        try:
            args = json.loads(arguments_json or "{}")
        except json.JSONDecodeError:
            args = None
        entry = TOOL_REGISTRY.get(name)
        if entry is None:
            result = {"ok": False, "action": name, "reason": "unknown_tool",
                      "message": f"工具 {name} 不存在"}
        elif args is None:
            result = {"ok": False, "action": name, "reason": "bad_arguments",
                      "message": "工具参数不是合法 JSON"}
        else:
            func, dangerous = entry
            result = (self._gated_execute(name, func, args)
                      if dangerous else func(self.session, **args))
        trace = {"name": name, "ok": result.get("ok", False),
                 "reason": result.get("reason")}
        return result, trace

    def _gated_execute(self, name: str, func, args: dict) -> dict:
        """危险操作(reserve/cancel)的唯一出口: 确认门控。"""
        core = {k: v for k, v in args.items() if k != "confirmed"}
        wants_exec = args.get("confirmed") is True

        if wants_exec and self._pending is not None \
                and self._pending["tool"] == name \
                and self._pending["args"] == core:
            if self._user_turns_since_pending >= 1:
                self._pending = None            # 通过门控, 消费待确认动作
                return func(self.session, **args)
            # 用户还没说过话就带着 confirmed=true 来: 拒绝, 重新要求确认
            return self._require_confirm(name, core,
                hint="检测到尚未获得用户确认, 必须先复述并等待用户答复。")

        # 未确认(或参数与 pending 不一致=用户改了主意): 建立/覆盖 pending
        return self._require_confirm(name, core)

    def _require_confirm(self, name: str, core_args: dict, hint: str = "") -> dict:
        """记录待确认动作并生成复述文案。查询失败时直接返回失败原因。"""
        display = self._build_display(name, core_args)
        if not display.get("ok"):
            self._pending = None
            return display

        self._pending = {"tool": name, "args": core_args, "display": display}
        self._user_turns_since_pending = 0
        message = ("请先向用户复述以下内容并获得明确确认, "
                   f"确认后携带 confirmed=true 重新调用 {name}: "
                   f"{display['confirm_display']}")
        if hint:
            message = hint + " " + message
        return {"ok": False, "action": name, "reason": "confirm_required",
                "message": message, "confirm_display": display["confirm_display"]}

    def _build_display(self, name: str, core_args: dict) -> dict:
        """为待确认动作生成人类可读的复述文案(顺带校验目标是否存在)。"""
        if name == "reserve_ticket":
            listing = query_tickets(self.session)
            if not listing["ok"]:
                return listing
            tk = next((t for t in listing["tickets"]
                       if t["tk_id"] == core_args.get("tk_id")), None)
            if tk is None:
                return {"ok": False, "action": name, "reason": "ticket_not_found",
                        "message": f"不存在编号为 {core_args.get('tk_id')} 的班次"}
            if tk["remaining"] <= 0:
                return {"ok": False, "action": name, "reason": "sold_out",
                        "message": f"{tk['addr']} {tk['use_date']} 已售罄",
                        "alternatives": [t for t in listing["tickets"] if t["remaining"] > 0]}
            return {"ok": True,
                    "confirm_display": (f"预订班次 {tk['tk_id']}: {tk['addr']}, "
                                        f"日期 {tk['use_date']}, 余票 {tk['remaining']} 张, "
                                        f"当前账号 {self.session.tel}")}
        if name == "cancel_reservation":
            listing = my_reservations(self.session)
            if not listing["ok"]:
                return listing
            yd = next((r for r in listing["reservations"]
                       if r["yd_id"] == core_args.get("yd_id")), None)
            if yd is None:
                return {"ok": False, "action": name, "reason": "not_yours",
                        "message": f"预约号 {core_args.get('yd_id')} 不存在或不属于当前账号"}
            return {"ok": True,
                    "confirm_display": (f"取消预约 {yd['yd_id']}: {yd['addr']}, "
                                        f"日期 {yd['use_date']}")}
        return {"ok": False, "action": name, "reason": "unknown_tool",
                "message": f"未知危险操作 {name}"}

    # ---- 统计 -------------------------------------------------------------

    @staticmethod
    def _accumulate_usage(reply: AgentReply, resp) -> None:
        usage = getattr(resp, "usage", None)
        if usage is None:
            return
        for k in ("prompt_tokens", "completion_tokens", "total_tokens"):
            reply.usage[k] = reply.usage.get(k, 0) + (getattr(usage, k, 0) or 0)
