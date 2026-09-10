"""M2 工具层测试。

单元部分: 用假 socket 脚本化服务端响应, 验证结构化返回与失败归因。
集成部分: 连真实服务端跑通 注册->登录->查票->预订->我的预约->取消 全流程,
         并验证 售罄/无此票/非本人预约号/未登录 的失败原因(计划 M2 验收项)。

运行: python -m unittest agent.test_tools -v   (服务端未启动时集成用例自动跳过)
"""

from __future__ import annotations

import time
import unittest

from agent.protocol import TicketClient
from agent.test_protocol import FakeSocket, frame_of
from agent.tools import (
    REASON_NOT_LOGGED_IN,
    REASON_NOT_YOURS,
    REASON_SOLD_OUT,
    REASON_TICKET_NOT_FOUND,
    TicketSession,
    cancel_reservation,
    my_reservations,
    query_tickets,
    reserve_ticket,
)

# 与 server/sql/init.sql 种子数据一致
SEED = [
    {"tk_id": "1", "addr": "西安-北京", "max": "100", "num": "0", "use_date": "2026-10-01"},
    {"tk_id": "2", "addr": "西安-上海", "max": "50", "num": "0", "use_date": "2026-10-02"},
    {"tk_id": "3", "addr": "西安-成都", "max": "20", "num": "19", "use_date": "2026-10-03"},
    {"tk_id": "4", "addr": "西安-广州", "max": "80", "num": "80", "use_date": "2026-10-04"},
]


def seeded_client(extra_chunks: list[bytes] | None = None) -> TicketClient:
    """预置响应队列的客户端: 先一次查票响应, 再接调用方给的额外响应。"""
    chunks = [frame_of({"status": "OK", "num": len(SEED), "arr": SEED})]
    chunks += extra_chunks or []
    c = TicketClient()
    c.connect(sock=FakeSocket(chunks))
    return c


class TestQueryTickets(unittest.TestCase):
    def test_normalizes_string_fields_and_remaining(self):
        """服务端字符串字段 -> int, 且补算 remaining。"""
        s = TicketSession(client=seeded_client())
        out = query_tickets(s)
        self.assertTrue(out["ok"])
        t3 = out["tickets"][2]
        self.assertEqual((t3["tk_id"], t3["total"], t3["used"], t3["remaining"]),
                         (3, 20, 19, 1))

    def test_not_logged_in_is_irs_for_query(self):
        # 查票不需要登录态, 但会话未登录时仍应正常返回(服务端查票不校验 tel)
        s = TicketSession(client=seeded_client())
        self.assertTrue(query_tickets(s)["ok"])

    def test_network_error_maps_to_reason(self):
        c = TicketClient()  # 未连接 -> TransportError
        s = TicketSession(client=c)
        out = query_tickets(s)
        self.assertFalse(out["ok"])
        self.assertEqual(out["reason"], "network_error")


class TestReserve(unittest.TestCase):
    def test_sold_out_with_alternatives(self):
        """售罄班次: 明确 reason + 给出替代班次列表。"""
        s = TicketSession(client=seeded_client())  # 只预置查票, 不应走到下单
        s.tel, s.user_name = "138", "u"
        out = reserve_ticket(s, 4)
        self.assertFalse(out["ok"])
        self.assertEqual(out["reason"], REASON_SOLD_OUT)
        self.assertIn("已售罄", out["message"])
        self.assertEqual([t["tk_id"] for t in out["alternatives"]], [1, 2, 3])

    def test_ticket_not_found(self):
        s = TicketSession(client=seeded_client())
        s.tel = "138"
        out = reserve_ticket(s, 999)
        self.assertEqual(out["reason"], REASON_TICKET_NOT_FOUND)

    def test_success_shape(self):
        ok_resp = frame_of({"status": "OK"})
        s = TicketSession(client=seeded_client([ok_resp]))
        s.tel = "138"
        out = reserve_ticket(s, 1)
        self.assertTrue(out["ok"])
        self.assertEqual(out["remaining_after"], 99)
        sent = s.client._sock.sent  # 假 socket 记录的原始帧
        self.assertIn(b'"type":4,"tel":"138","index":1', sent)

    def test_requires_login(self):
        s = TicketSession(client=seeded_client())
        out = reserve_ticket(s, 1)
        self.assertEqual(out["reason"], REASON_NOT_LOGGED_IN)


class TestCancel(unittest.TestCase):
    def test_not_yours(self):
        """取消不属于当前账号的预约号 -> not_yours 而非笼统 ERR。"""
        mine = frame_of({"status": "OK", "num": 1, "arr": [
            {"yd_id": "7", "addr": "西安-北京", "use_date": "2026-10-01"}]})
        s = TicketSession(client=seeded_client([mine]))
        s.tel = "138"
        out = cancel_reservation(s, 999)  # 别人的/不存在的预约号
        self.assertEqual(out["reason"], REASON_NOT_YOURS)
        self.assertIn("不属于当前账号", out["message"])


class TestRealServer(unittest.TestCase):
    """真实服务端: M2 验收"四工具顺序调用全通 + 售罄明确失败原因"。"""

    @classmethod
    def setUpClass(cls):
        import socket as _s
        try:
            probe = _s.create_connection(("127.0.0.1", 6000), timeout=1)
            probe.close()
        except OSError:
            raise unittest.SkipTest("127.0.0.1:6000 无服务端, 跳过集成测试")
        cls.session = TicketSession(client=TicketClient("127.0.0.1", 6000))
        cls.session.client.connect()
        cls.tel = f"139{int(time.time()) % 100000000:08d}"  # 每次运行唯一
        if not cls.session.register(cls.tel, "eval_user", "123456"):
            raise unittest.SkipTest("注册失败(手机号重复?), 跳过")
        assert cls.session.login(cls.tel, "123456")

    @classmethod
    def tearDownClass(cls):
        cls.session.client.close()

    def test_1_full_flow(self):
        s = self.session
        q = query_tickets(s)
        self.assertTrue(q["ok"])
        tk1 = next(t for t in q["tickets"] if t["addr"] == "西安-北京")

        r = reserve_ticket(s, tk1["tk_id"], confirmed=True)
        self.assertTrue(r["ok"], r)

        m = my_reservations(s)
        self.assertTrue(m["ok"])
        mine = next(x for x in m["reservations"] if x["addr"] == "西安-北京")

        c = cancel_reservation(s, mine["yd_id"], confirmed=True)
        self.assertTrue(c["ok"], c)

        m2 = my_reservations(s)
        self.assertFalse(any(x["yd_id"] == mine["yd_id"]
                             for x in m2["reservations"]))

    def test_2_sold_out_reason(self):
        s = self.session
        q = query_tickets(s)
        sold = next(t for t in q["tickets"] if t["remaining"] == 0)
        out = reserve_ticket(s, sold["tk_id"], confirmed=True)
        self.assertFalse(out["ok"])
        self.assertEqual(out["reason"], REASON_SOLD_OUT)
        self.assertTrue(out["alternatives"])

    def test_3_cancel_foreign_yd_id(self):
        out = cancel_reservation(self.session, 999999, confirmed=True)
        self.assertFalse(out["ok"])
        self.assertEqual(out["reason"], REASON_NOT_YOURS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
