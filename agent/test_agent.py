"""M3 Agent 循环测试(FakeLLM, 不需要真实 API Key)。

重点验证确认门控的代码层保证(不依赖模型自觉):
  A. 未带 confirmed 的 reserve/cancel 不碰 TCP(只允许出现查票帧);
  B. 同一用户轮次内"自问自答"带 confirmed=true 也无法通过门控;
  C. 用户确认后的下一轮, 参数一致的 confirmed=true 才真正发包;
  D. 参数不一致视为改主意, 覆盖 pending。

另覆盖: 纯文本应答、工具结果回填、最大步数兜底、未知工具、LLM 异常道歉。

运行: python -m unittest agent.test_agent -v
"""

from __future__ import annotations

from types import SimpleNamespace

import unittest

from agent.agent import TicketAgent
from agent.protocol import TicketClient
from agent.test_protocol import FakeSocket, frame_of
from agent.test_tools import SEED
from agent.tools import TicketSession


def OK_QUERY():
    return frame_of({"status": "OK", "num": len(SEED), "arr": SEED})


def OK_RESERVE():
    return frame_of({"status": "OK"})


class FakeLLM:
    """脚本化 LLM: 每次 create() 弹出一个预设"模型响应"。

    响应格式: ("text", "回复内容") 或 ("tools", [(工具名, 参数JSON), ...])
    """

    def __init__(self, script=None, raise_exc=None):
        self.script = list(script or [])
        self.raise_exc = raise_exc
        self.calls = []

    @property
    def chat(self):
        return self

    @property
    def completions(self):
        return self

    def create(self, **kw):
        self.calls.append(kw)
        if self.raise_exc:
            raise self.raise_exc
        kind, payload = self.script.pop(0)
        if kw.get("stream"):               # V2-M1: chat_stream() 的流式形态
            return self._fake_stream(kind, payload)
        if kind == "text":
            msg = SimpleNamespace(content=payload, tool_calls=None)
        else:
            tcs = self._tool_call_namespaces(payload)
            msg = SimpleNamespace(content=None, tool_calls=tcs)
        usage = SimpleNamespace(prompt_tokens=10, completion_tokens=5, total_tokens=15)
        return SimpleNamespace(choices=[SimpleNamespace(message=msg)], usage=usage)

    @staticmethod
    def _tool_call_namespaces(payload):
        return [SimpleNamespace(id=f"call_{i}",
                                function=SimpleNamespace(name=name, arguments=args))
                for i, (name, args) in enumerate(payload)]

    @staticmethod
    def _fake_stream(kind, payload):
        """按 OpenAI 流式分片形态吐 chunk: 文本按 5 字符切片;
        工具调用首块带 index/id/name, 参数按 8 字符分片续传; 末块只带 usage。"""
        chunks = []
        if kind == "text":
            for i in range(0, len(payload), 5):
                chunks.append(SimpleNamespace(choices=[SimpleNamespace(
                    delta=SimpleNamespace(content=payload[i:i + 5], tool_calls=None))]))
        else:
            for idx, (name, args) in enumerate(payload):
                chunks.append(SimpleNamespace(choices=[SimpleNamespace(
                    delta=SimpleNamespace(content=None, tool_calls=[SimpleNamespace(
                        index=idx, id=f"call_{idx}",
                        function=SimpleNamespace(name=name, arguments=""))]))]))
                for j in range(0, len(args), 8):
                    chunks.append(SimpleNamespace(choices=[SimpleNamespace(
                        delta=SimpleNamespace(content=None, tool_calls=[SimpleNamespace(
                            index=idx, id=None,
                            function=SimpleNamespace(name=None, arguments=args[j:j + 8]))]))]))
        chunks.append(SimpleNamespace(choices=[],
                                      usage=SimpleNamespace(prompt_tokens=10,
                                                            completion_tokens=5,
                                                            total_tokens=15)))
        return chunks


def make_agent(responses, script=None, raise_exc=None):
    """构造注入了 FakeLLM 与假 socket 的 TicketAgent。

    responses: 假 socket 依次返回的服务端响应帧
    script:    FakeLLM 依次返回的模型响应; 后续轮次可整体替换 agent.llm
    """
    client = TicketClient()
    fake = FakeSocket(responses)
    client.connect(sock=fake)
    s = TicketSession(client=client)
    s.tel, s.user_name = "13900000001", "eval_user"
    llm = FakeLLM(script, raise_exc)
    return TicketAgent(s, llm=llm), fake


class TestBasicLoop(unittest.TestCase):
    def test_plain_text_answer(self):
        agent, _ = make_agent([], [("text", "你好, 我可以帮你查票订票。")])
        r = agent.chat("你好")
        self.assertEqual(r.content, "你好, 我可以帮你查票订票。")
        self.assertIsNone(r.error)
        self.assertEqual(r.usage["total_tokens"], 15)

    def test_tool_call_and_backfill(self):
        agent, _ = make_agent(
            [OK_QUERY()],
            [("tools", [("query_tickets", "{}")]),
             ("text", "共 4 个班次。")])
        r = agent.chat("有哪些票")
        self.assertEqual(r.content, "共 4 个班次。")
        self.assertEqual(r.tool_trace,
                         [{"name": "query_tickets", "ok": True, "reason": None}])
        # 回填的 tool 消息与 assistant tool_calls 消息都进入历史
        roles = [m["role"] for m in agent.messages]
        self.assertEqual(roles, ["system", "user", "assistant", "tool", "assistant"])
        self.assertIn("西安-北京", agent.messages[3]["content"])


class TestConfirmGate(unittest.TestCase):
    def test_a_unconfirmed_never_touches_reserve_api(self):
        """A: 未确认的预订请求, TCP 上只允许出现查票帧(type:3)。"""
        agent, fake = make_agent(
            [OK_QUERY()],  # 门控生成复述文案需要查一次票
            [("tools", [("reserve_ticket", '{"tk_id": 1}')]),
             ("text", "为您查询到: 西安-北京 2026-10-01, 余票100张, 确认预订吗?")])
        r = agent.chat("帮我订1号")
        self.assertIn("确认预订吗", r.content)
        self.assertEqual(r.tool_trace[0]["reason"], "confirm_required")
        self.assertNotIn(b'"type":4', fake.sent)  # 关键: 没有下单帧
        self.assertIsNotNone(agent._pending)

    def test_b_same_turn_self_confirm_rejected(self):
        """B: 模型同一轮内直接带 confirmed=true, 门控必须拒绝。"""
        agent, fake = make_agent(
            [OK_QUERY(), OK_QUERY()],
            [("tools", [("reserve_ticket", '{"tk_id": 1}')]),
             ("tools", [("reserve_ticket", '{"tk_id": 1, "confirmed": true}')]),
             ("text", "请确认后我再下单。")])
        r = agent.chat("直接订1号别问了")
        self.assertNotIn(b'"type":4', fake.sent)
        reasons = [t["reason"] for t in r.tool_trace]
        self.assertEqual(reasons, ["confirm_required", "confirm_required"])
        # 第二次拒绝的提示语进入了模型可见的 tool 结果
        self.assertIn("尚未获得用户确认", agent.messages[-2]["content"])

    def test_c_confirmed_after_user_turn_executes(self):
        """C: 用户确认后的下一轮, 门控放行, TCP 出现下单帧。"""
        agent, fake = make_agent(
            [OK_QUERY(), OK_QUERY(), OK_RESERVE()],
            [("tools", [("reserve_ticket", '{"tk_id": 1}')]),
             ("text", "西安-北京 2026-10-01 余票 100 张, 确认吗?")])
        agent.chat("订1号")

        agent.llm = FakeLLM([("tools", [("reserve_ticket",
                                         '{"tk_id": 1, "confirmed": true}')]),
                             ("text", "预订成功!")])
        r = agent.chat("确认")
        self.assertTrue(r.tool_trace[0]["ok"])
        self.assertIn(b'"type":4', fake.sent)  # 此刻才真正下单
        self.assertIsNone(agent._pending)

    def test_d_mismatched_args_replace_pending(self):
        """D: 改主意(换班次)覆盖 pending, 旧确认不能用于新班次。"""
        agent, _ = make_agent(
            [OK_QUERY(), OK_QUERY()],
            [("tools", [("reserve_ticket", '{"tk_id": 1}')]),
             ("text", "确认订1号吗?")])
        agent.chat("订1号")

        agent.llm = FakeLLM([("tools", [("reserve_ticket", '{"tk_id": 2}')]),
                             ("text", "改成2号了, 确认吗?")])
        agent.chat("不, 改成2号")
        self.assertEqual(agent._pending["args"], {"tk_id": 2})

    def test_gate_soldout_short_circuit(self):
        """门控期就发现售罄: 直接返回 sold_out, 不进入确认流程。"""
        agent, fake = make_agent(
            [OK_QUERY()],
            [("tools", [("reserve_ticket", '{"tk_id": 4}')]),
             ("text", "该班次已售罄。")])
        r = agent.chat("订4号")
        self.assertEqual(r.tool_trace[0]["reason"], "sold_out")
        self.assertIsNone(agent._pending)
        self.assertNotIn(b'"type":4', fake.sent)


class TestChatStream(unittest.TestCase):
    """V2-M1: chat_stream() 与 chat() 共享 messages/门控, 只有输出形态不同。"""

    @staticmethod
    def events_of(agent, text):
        return list(agent.chat_stream(text))

    def test_tokens_concat_to_final_content(self):
        agent, _ = make_agent([], [("text", "共 4 个班次, 欢迎挑选。")])
        evs = self.events_of(agent, "有哪些票")
        tokens = [e["text"] for e in evs if e["type"] == "token"]
        done = evs[-1]
        self.assertEqual(done["type"], "done")
        self.assertIsNone(done["error"])
        self.assertEqual("".join(tokens), done["content"])
        self.assertEqual(done["content"], "共 4 个班次, 欢迎挑选。")
        self.assertEqual(done["usage"]["total_tokens"], 15)
        # messages 维护与 chat() 同构
        self.assertEqual([m["role"] for m in agent.messages],
                         ["system", "user", "assistant"])

    def test_gate_flow_streaming(self):
        """门控两步操作在流式路径同样全过: 未确认拦截 -> 确认后放行。"""
        agent, fake = make_agent(
            [OK_QUERY(), OK_QUERY(), OK_RESERVE()],
            [("tools", [("reserve_ticket", '{"tk_id": 1}')]),
             ("text", "西安-北京 2026-10-01, 确认预订吗?")])
        evs = self.events_of(agent, "订1号")
        kinds = [e["type"] for e in evs]
        self.assertIn("tool_start", kinds)
        tr = next(e for e in evs if e["type"] == "tool_result")
        self.assertEqual((tr["name"], tr["ok"], tr["reason"]),
                         ("reserve_ticket", False, "confirm_required"))
        cr = next(e for e in evs if e["type"] == "confirm_request")
        self.assertEqual(cr["action"], "reserve_ticket")
        self.assertIn("预订班次 1", cr["display"])
        self.assertNotIn(b'"type":4', fake.sent)      # 未确认不下单

        agent.llm = FakeLLM([("tools", [("reserve_ticket",
                                         '{"tk_id": 1, "confirmed": true}')]),
                             ("text", "预订成功!")])
        evs2 = self.events_of(agent, "确认")
        tr2 = next(e for e in evs2 if e["type"] == "tool_result")
        self.assertTrue(tr2["ok"])
        self.assertIn(b'"type":4', fake.sent)          # 此刻才下单
        self.assertNotIn("confirm_request", [e["type"] for e in evs2])
        # 工具轮的历史结构与非流式一致: assistant(tool_calls)+tool 回填
        self.assertEqual([m["role"] for m in agent.messages],
                         ["system", "user", "assistant", "tool", "assistant",
                          "user", "assistant", "tool", "assistant"])

    def test_stream_error_apologizes(self):
        agent, _ = make_agent([], raise_exc=RuntimeError("api down"))
        evs = self.events_of(agent, "查票")
        done = evs[-1]
        self.assertEqual(done["error"], "api down")
        self.assertIn("抱歉", done["content"])
        # 异常轮不上报确认卡片
        self.assertNotIn("confirm_request", [e["type"] for e in evs])


class TestHistoryTrim(unittest.TestCase):
    def test_trims_at_user_boundary_keeps_system(self):
        """多轮后裁剪: 保留 system, 总条数不超预算, 边界落在 user 上。"""
        agent, _ = make_agent(
            [], [("text", f"回复{i}") for i in range(5)])
        agent.max_history = 6
        for i in range(5):
            agent.chat(f"第{i}轮")
        roles = [m["role"] for m in agent.messages]
        self.assertEqual(roles[0], "system")
        self.assertLessEqual(len(roles), 6)
        self.assertEqual(roles[1], "user")          # 裁剪点必须是 user 边界
        self.assertEqual(roles, ["system", "user", "assistant",
                                 "user", "assistant"])

    def test_trim_never_orphans_tool_messages(self):
        """裁剪不得把 assistant(tool_calls) 与其 tool 结果拆散。"""
        agent, _ = make_agent(
            [OK_QUERY(), OK_QUERY()],
            [("tools", [("query_tickets", "{}")]), ("text", "清单如上。")])
        agent.max_history = 8
        agent.chat("查票")
        agent.llm = FakeLLM([("text", "好的。"), ("text", "好的。")])
        agent.chat("继续")
        agent.chat("继续")
        msgs = agent.messages
        for idx, m in enumerate(msgs):
            if m["role"] == "tool":
                self.assertEqual(msgs[idx - 1]["role"], "assistant")
                self.assertIn("tool_calls", msgs[idx - 1])


class TestDefenses(unittest.TestCase):
    def test_max_steps_backstop(self):
        agent, _ = make_agent(
            [OK_QUERY() for _ in range(8)],
            [("tools", [("query_tickets", "{}")])] * 8)
        r = agent.chat("查票")
        self.assertEqual(r.error, "max_steps_exceeded")
        self.assertIn("换个说法", r.content)

    def test_unknown_tool_reported_to_model(self):
        agent, _ = make_agent(
            [],
            [("tools", [("delete_all", "{}")]),
             ("text", "没有这个功能。")])
        r = agent.chat("删库")
        self.assertEqual(r.tool_trace[0]["reason"], "unknown_tool")

    def test_llm_exception_apologizes(self):
        agent, _ = make_agent([], raise_exc=RuntimeError("api down"))
        r = agent.chat("查票")
        self.assertIn("抱歉", r.content)
        self.assertEqual(r.error, "api down")


if __name__ == "__main__":
    unittest.main(verbosity=2)
