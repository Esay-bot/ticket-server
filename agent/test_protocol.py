"""M1 协议层单元测试(计划验收项)。

覆盖:
  1. 编码正确性: 4 字节大端长度头 + JSON 正文;
  2. 半包: 一个帧分多次 recv 到达;
  3. 粘包: 两个帧粘连在一次 recv 里, 跨两次 request() 分别取到;
  4. 跨请求残留: 上一帧尾部残留下一帧的部分字节;
  5. 非法长度: 0 / >4096 抛 ProtocolError 且连接被关闭;
  6. 响应非 JSON 抛 ProtocolError;
  7. 请求锁: 在途请求未完成时, 第二个请求被阻塞(串行化);
  8. 真实服务: 连 127.0.0.1:6000 发 type:3 查票(服务端未启动则跳过)。

运行: python -m unittest agent.test_protocol -v
"""

from __future__ import annotations

import json
import socket
import struct
import threading
import unittest

from agent.protocol import (
    MAX_BODY_LEN,
    TicketClient,
    TransportError,
    ProtocolError,
    encode_frame,
)


class FakeSocket:
    """脚本化假 socket: recv() 依次弹出预设字节块, sendall() 记录发送内容。"""

    def __init__(self, chunks: list[bytes]):
        self.chunks = list(chunks)
        self.sent = b""
        self.recv_calls = 0
        self.closed = False

    def sendall(self, data):
        self.sent += data

    def recv(self, n):
        self.recv_calls += 1
        if not self.chunks:
            # 缓冲里已有完整帧时不应再调用 recv —— 调了说明取帧逻辑有 bug
            raise AssertionError("unexpected recv(): frame should come from buffer")
        return self.chunks.pop(0)

    def close(self):
        self.closed = True


def frame_of(payload: dict) -> bytes:
    """构造服务端风格的响应帧(与 encode_frame 同构, 用独立实现交叉验证)。"""
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    return struct.pack(">I", len(body)) + body


class TestEncode(unittest.TestCase):
    def test_frame_layout(self):
        payload = {"type": 3}
        body = json.dumps(payload, separators=(",", ":")).encode()
        frame = encode_frame(payload)
        self.assertEqual(frame[:4], struct.pack(">I", len(body)))
        self.assertEqual(frame[4:], body)

    def test_chinese_not_escaped_and_len_by_bytes(self):
        frame = encode_frame({"type": 2, "user_name": "王思雨"})
        (n,) = struct.unpack(">I", frame[:4])
        self.assertEqual(n, len(frame) - 4)          # 长度按 utf-8 字节数
        self.assertIn("王思雨".encode(), frame[4:])   # 中文原样传输


class TestDecode(unittest.TestCase):
    def _client(self, chunks):
        c = TicketClient()
        fake = FakeSocket(chunks)
        c.connect(sock=fake)
        return c, fake

    def test_half_packet_split(self):
        """半包: 长度头和正文各拆成多块到达。"""
        resp = frame_of({"status": "OK", "user_name": "agenttest"})
        # 拆成 1 + 3 + 5 + ... 字节的碎块
        pieces = [resp[:1], resp[1:4], resp[4:9], resp[9:]]
        c, _ = self._client(pieces)
        out = c.request({"type": 1, "user_tel": "x", "user_passwd": "y"})
        self.assertEqual(out, {"status": "OK", "user_name": "agenttest"})

    def test_two_frames_glued(self):
        """粘包: 两个响应帧一次到达, 两次 request() 各取各的。"""
        glued = frame_of({"status": "OK", "n": 1}) + frame_of({"status": "OK", "n": 2})
        c, fake = self._client([glued])
        self.assertEqual(c.request({"type": 3})["n"], 1)
        self.assertEqual(c.request({"type": 3})["n"], 2)
        self.assertEqual(fake.recv_calls, 1)  # 第二次直接消费缓冲, 不再 recv

    def test_partial_leftover_across_requests(self):
        """上一帧尾部粘连了下一帧的半截头部, 留给下次 request() 拼完。"""
        f2 = frame_of({"status": "OK", "n": 2})
        head2 = f2[:2]  # 下一帧长度头的前 2 字节
        c, _ = self._client([frame_of({"status": "OK", "n": 1}) + head2,
                             f2[2:]])
        self.assertEqual(c.request({"type": 3})["n"], 1)
        self.assertEqual(c.request({"type": 3})["n"], 2)

    def test_illegal_length_zero(self):
        c, fake = self._client([struct.pack(">I", 0)])
        with self.assertRaises(ProtocolError):
            c.request({"type": 3})
        self.assertFalse(c.connected)
        self.assertTrue(fake.closed)

    def test_illegal_length_too_large(self):
        c, fake = self._client([struct.pack(">I", MAX_BODY_LEN + 1)])
        with self.assertRaises(ProtocolError):
            c.request({"type": 3})
        self.assertFalse(c.connected)

    def test_bad_json_body(self):
        bad = b"not a json"
        c, fake = self._client([struct.pack(">I", len(bad)) + bad])
        with self.assertRaises(ProtocolError):
            c.request({"type": 3})
        self.assertFalse(c.connected)

    def test_invalid_request_type_rejected(self):
        c, _ = self._client([])
        with self.assertRaises(ProtocolError):
            c.request({"type": 99})

    def test_request_without_connect(self):
        c = TicketClient()
        with self.assertRaises(TransportError):
            c.request({"type": 3})


class TestRequestLock(unittest.TestCase):
    def test_inflight_request_blocks_others(self):
        """在途请求(已发送、等响应)期间, 锁被持有, 第二个请求必须等待。"""
        release = threading.Event()
        resp = frame_of({"status": "OK"})

        class BlockingSocket(FakeSocket):
            def recv(self, n):
                self.recv_calls += 1
                if self.chunks:
                    return self.chunks.pop(0)
                release.wait(5)  # 模拟服务端处理中, 响应迟迟不来
                return resp

        c = TicketClient()
        c.connect(sock=BlockingSocket([]))
        t = threading.Thread(target=lambda: c.request({"type": 3}))
        t.start()

        deadline_wait = 2.0
        self.assertFalse(c._inflight.acquire(timeout=deadline_wait),
                         "在途请求未完成时锁不应可得")
        release.set()
        t.join(5)
        self.assertTrue(c._inflight.acquire(timeout=1), "响应收讫后锁应释放")
        c._inflight.release()
        c.close()


class TestRealServer(unittest.TestCase):
    """真实服务端集成测试: WSL 里的 ./ser 监听 6000, 未启动则跳过。"""

    def test_view_tickets(self):
        try:
            probe = socket.create_connection(("127.0.0.1", 6000), timeout=1)
            probe.close()
        except OSError:
            self.skipTest("127.0.0.1:6000 无服务端, 跳过真实调用")
        with TicketClient("127.0.0.1", 6000) as c:
            resp = c.request({"type": 3})
            self.assertEqual(resp["status"], "OK")
            self.assertGreaterEqual(resp["num"], 1)
            first = resp["arr"][0]
            for key in ("tk_id", "addr", "max", "num", "use_date"):
                self.assertIn(key, first)


if __name__ == "__main__":
    unittest.main(verbosity=2)
