"""V1-M1 服务层测试(FastAPI TestClient, 不需要真实 API Key 与 C++ 服务端)。

覆盖(计划 V1-M1 验收项):
  - 登录/注册建会话; 错误密码 401, 重复注册 409, 失败后 TCP 不残留;
  - chat 返回 reply/tool_trace/usage/confirm_request:
      两步操作间 confirm_request 正确出现(未确认无下单帧)与消失(确认后有下单帧);
      用户口头拒绝后不重复弹卡; 会话不存在 404; TCP 断开 503;
  - GET /tickets 转发(含 network_error -> 503); POST /logout 关 TCP 并移除会话;
  - 一个进程多会话并存互不干扰;
  - TTL 空闲清理: 过期会话被移除且 TCP 被关闭(防连接泄漏)。

替身注入方式: create_app(connect=..., agent_factory=...) ——
  connect 每次登录取一个预置 FakeSocket 的 TicketClient;
  agent_factory 给每个会话挂 SwapLLM(两次 POST /chat 之间可整体换剧本)。

运行: python -m unittest agent.test_service -v
"""

from __future__ import annotations

import time
import unittest
from types import SimpleNamespace

from fastapi.testclient import TestClient

from agent.agent import TicketAgent
from agent.protocol import TicketClient, TransportError
from agent.service import create_app
from agent.test_agent import FakeLLM
from agent.test_protocol import FakeSocket, frame_of
from agent.test_tools import SEED


def OK_LOGIN(name="service_tester"):
    return frame_of({"status": "OK", "user_name": name})


def OK_QUERY():
    return frame_of({"status": "OK", "num": len(SEED), "arr": SEED})


def OK_RESERVE():
    return frame_of({"status": "OK"})


class SwapLLM:
    """http 调用之间可整体换剧本的 LLM 桩(每次 POST /chat 前 load 下一轮)。"""

    def __init__(self):
        self.inner = FakeLLM([])

    def load(self, script):
        self.inner = FakeLLM(script)

    @property
    def chat(self):        # llm.chat.completions.create 代理到当前剧本
        return self.inner


def make_service(socket_batches: list[list[bytes]], **app_kwargs):
    """socket_batches: 每次 login/register 依次消费的一组响应帧。

    返回 (TestClient, recs): recs[i] 记录第 i 个会话的 client、FakeSocket
    (TicketClient.close() 后 _sock 置 None, 需另存 socket 引用)与 SwapLLM
    (与该会话的 Agent 绑定; Agent 懒创建于首次 /chat, 剧本可提前 load)。
    """
    recs = []

    def connect() -> TicketClient:
        sock = FakeSocket(socket_batches.pop(0))
        client = TicketClient()
        client.connect(sock=sock)
        recs.append(SimpleNamespace(client=client, sock=sock, swap=SwapLLM()))
        return client

    def agent_factory(session) -> TicketAgent:
        rec = next(r for r in recs if r.client is session.client)
        return TicketAgent(session, llm=rec.swap)

    return TestClient(create_app(connect=connect, agent_factory=agent_factory,
                                 **app_kwargs)), recs


def _raise_transport() -> TicketClient:
    raise TransportError("连接 127.0.0.1:6000 失败: 拒绝连接")


class TestLoginRegister(unittest.TestCase):
    def _login(self, tc, tel="13900000001", passwd="x"):
        r = tc.post("/login", json={"tel": tel, "passwd": passwd})
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()

    def test_login_returns_session_and_user_name(self):
        tc, _ = make_service([[OK_LOGIN("王思雨")]])
        with tc:
            body = self._login(tc)
        self.assertIn("session_id", body)
        self.assertEqual(body["user_name"], "王思雨")

    def test_login_wrong_password_401_and_no_tcp_leak(self):
        tc, recs = make_service([[frame_of({"status": "ERR"})]])
        with tc:
            r = tc.post("/login", json={"tel": "13900000001", "passwd": "bad"})
        self.assertEqual(r.status_code, 401)
        self.assertIn("手机号或密码", r.json()["detail"])
        self.assertTrue(recs[0].sock.closed)   # 失败连接即关, 不残留

    def test_register_conflict_409(self):
        tc, recs = make_service([[frame_of({"status": "ERR"})]])
        with tc:
            r = tc.post("/register", json={"tel": "13900000001",
                                           "user_name": "甲", "passwd": "x"})
        self.assertEqual(r.status_code, 409)
        self.assertIn("已被注册", r.json()["detail"])
        self.assertTrue(recs[0].sock.closed)

    def test_register_ok_auto_login(self):
        tc, _ = make_service([[frame_of({"status": "OK"}), OK_LOGIN("新用户")]])
        with tc:
            r = tc.post("/register", json={"tel": "13900000002",
                                           "user_name": "新用户", "passwd": "p"})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json()["user_name"], "新用户")

    def test_server_unreachable_503_with_human_message(self):
        with TestClient(create_app(connect=_raise_transport)) as tc:
            r = tc.post("/login", json={"tel": "13900000001", "passwd": "x"})
        self.assertEqual(r.status_code, 503)
        self.assertIn("WSL", r.json()["detail"])   # 人话: 告诉用户去哪启动服务端

    def test_health(self):
        tc, _ = make_service([[OK_LOGIN()]])
        with tc:
            self.assertEqual(tc.get("/health").json()["status"], "ok")


class TestChatAndGate(unittest.TestCase):
    def _login(self, tc, tel="13900000001"):
        r = tc.post("/login", json={"tel": tel, "passwd": "x"})
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()["session_id"]

    def _chat(self, tc, sid, text):
        r = tc.post("/chat", json={"session_id": sid, "text": text})
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()

    def test_gate_flow_confirm_request_appears_then_clears(self):
        """核心场景: 订票出卡片(无下单帧) -> 点确认等价的"确认" -> 下单成功卡消失。"""
        # 帧: 登录 | 第1轮 chat: 门控查票 | 第2轮 chat: 确认执行(先查后订)
        tc, recs = make_service(
            [[OK_LOGIN(), OK_QUERY(), OK_QUERY(), OK_RESERVE()]])
        with tc:
            sid = self._login(tc)

            recs[0].swap.load([("tools", [("reserve_ticket", '{"tk_id": 1}')]),
                          ("text", "西安-北京 2026-10-01 余票100张, 确认预订吗?")])
            r1 = self._chat(tc, sid, "订1号")
            self.assertEqual(r1["reply"], "西安-北京 2026-10-01 余票100张, 确认预订吗?")
            self.assertEqual(r1["tool_trace"][0]["reason"], "confirm_required")
            cr = r1["confirm_request"]
            self.assertEqual(cr["action"], "reserve_ticket")
            self.assertEqual(cr["args"], {"tk_id": 1})
            self.assertIn("西安-北京", cr["display"])
            self.assertIn("预订班次 1", cr["display"])
            self.assertNotIn(b'"type":4', recs[0].sock.sent)  # 未确认不下单

            recs[0].swap.load([("tools", [("reserve_ticket",
                                       '{"tk_id": 1, "confirmed": true}')]),
                          ("text", "预订成功!")])
            r2 = self._chat(tc, sid, "确认")
            self.assertTrue(r2["tool_trace"][0]["ok"])
            self.assertIsNone(r2["confirm_request"])
            self.assertIn(b'"type":4', recs[0].sock.sent)     # 此刻才下单

    def test_verbal_decline_does_not_reshow_card(self):
        """用户口头拒绝: pending 仍在(门控状态不丢), 但后续轮不重复弹卡。"""
        tc, recs = make_service([[OK_LOGIN(), OK_QUERY()]])
        with tc:
            sid = self._login(tc)
            recs[0].swap.load([("tools", [("reserve_ticket", '{"tk_id": 1}')]),
                           ("text", "确认订1号吗?")])
            self.assertIsNotNone(self._chat(tc, sid, "订1号")["confirm_request"])
            recs[0].swap.load([("text", "好的, 已为您取消本次预订意向。")])
            r2 = self._chat(tc, sid, "算了不订了")
            self.assertIsNone(r2["confirm_request"])


    def test_chat_without_llm_config_503_session_still_usable(self):
        """缺 DEEPSEEK_API_KEY: /chat 返回 503 人话提示, 但登录/查票不受影响。"""
        def connect() -> TicketClient:
            client = TicketClient()
            client.connect(sock=FakeSocket([OK_LOGIN(), OK_QUERY(), OK_QUERY()]))
            return client

        def no_key_factory(session):
            raise RuntimeError("未设置环境变量 DEEPSEEK_API_KEY, 无法调用 DeepSeek")

        with TestClient(create_app(connect=connect,
                                   agent_factory=no_key_factory)) as tc:
            sid = self._login(tc)
            self.assertEqual(
                tc.get("/tickets", params={"session_id": sid}).status_code, 200)
            r = tc.post("/chat", json={"session_id": sid, "text": "查票"})
            self.assertEqual(r.status_code, 503)
            self.assertIn("DEEPSEEK_API_KEY", r.json()["detail"])
            # Agent 初始化失败不拖垮会话: 查票仍可用
            self.assertEqual(
                tc.get("/tickets", params={"session_id": sid}).status_code, 200)

    def test_chat_unknown_session_404(self):
        with TestClient(create_app(connect=_raise_transport)) as tc:
            r = tc.post("/chat", json={"session_id": "nope", "text": "你好"})
        self.assertEqual(r.status_code, 404)
        self.assertIn("重新登录", r.json()["detail"])

    def test_chat_empty_text_422(self):
        tc, _ = make_service([[OK_LOGIN()]])
        with tc:
            sid = self._login(tc)
            r = tc.post("/chat", json={"session_id": sid, "text": ""})
        self.assertEqual(r.status_code, 422)

    def test_chat_disconnected_tcp_503(self):
        tc, recs = make_service([[OK_LOGIN()]])
        with tc:
            sid = self._login(tc)
            recs[0].client.close()          # 模拟链路中断(request 出错会自动 close)
            r = tc.post("/chat", json={"session_id": sid, "text": "查票"})
            self.assertEqual(r.status_code, 503)
            self.assertIn("重新登录", r.json()["detail"])

    def test_two_sessions_coexist_in_one_process(self):
        tc, recs = make_service([[OK_LOGIN("甲")], [OK_LOGIN("乙")]])
        with tc:
            sid1 = self._login(tc, "13900000001")
            sid2 = self._login(tc, "13900000002")
            self.assertNotEqual(sid1, sid2)
            recs[0].swap.load([("text", "这是甲的回复")])
            recs[1].swap.load([("text", "这是乙的回复")])
            r1 = self._chat(tc, sid1, "你好")
            r2 = self._chat(tc, sid2, "你好")
            self.assertEqual((r1["reply"], r2["reply"]),
                             ("这是甲的回复", "这是乙的回复"))
            self.assertEqual(len(recs), 2)     # 各自独立 TCP 连接
            # 乙的会话里没有甲的消息
            self.assertNotIn("甲", "".join(m.get("content", "")
                                           for m in recs[1].swap.inner.calls[0]["messages"]))


class TestTicketsAndLogout(unittest.TestCase):
    def _login(self, tc):
        r = tc.post("/login", json={"tel": "13900000001", "passwd": "x"})
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()["session_id"]

    def test_tickets_forwarding(self):
        tc, _ = make_service([[OK_LOGIN(), OK_QUERY()]])
        with tc:
            sid = self._login(tc)
            r = tc.get("/tickets", params={"session_id": sid})
        self.assertEqual(r.status_code, 200)
        body = r.json()
        self.assertTrue(body["ok"])
        t3 = body["tickets"][2]                    # SEED 3 号: 20-19=1
        self.assertEqual((t3["tk_id"], t3["remaining"]), (3, 1))

    def test_tickets_network_error_maps_503(self):
        tc, recs = make_service([[OK_LOGIN()]])
        with tc:
            sid = self._login(tc)
            recs[0].client.close()
            r = tc.get("/tickets", params={"session_id": sid})
        self.assertEqual(r.status_code, 503)

    def test_logout_closes_tcp_and_session(self):
        tc, recs = make_service([[OK_LOGIN()]])
        with tc:
            sid = self._login(tc)
            r = tc.post("/logout", json={"session_id": sid})
            self.assertEqual(r.status_code, 200)
            self.assertTrue(r.json()["ok"])
            self.assertTrue(recs[0].sock.closed)     # TCP 已关
            self.assertEqual(tc.get("/tickets", params={"session_id": sid}).status_code,
                             404)                        # 会话已移除


class TestSessionTtl(unittest.TestCase):
    def test_idle_session_swept_and_tcp_closed(self):
        # TTL 50ms + 惰性清理限频 50ms: 睡 100ms 后下一次取用应触发清理
        tc, recs = make_service([[OK_LOGIN()]],
                                      session_ttl=0.05, sweep_interval=0.05)
        with tc:
            r = tc.post("/login", json={"tel": "13900000001", "passwd": "x"})
            sid = r.json()["session_id"]
            time.sleep(0.1)
            r = tc.post("/chat", json={"session_id": sid, "text": "还在吗"})
            self.assertEqual(r.status_code, 404)
            self.assertTrue(recs[0].sock.closed)     # 清理时关闭了 TCP


if __name__ == "__main__":
    unittest.main(verbosity=2)
