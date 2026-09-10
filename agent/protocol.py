"""M1 协议层: 自研「4 字节大端长度头 + JSON」二进制协议的 TCP 客户端。

对接本人开发的 C++ libevent 票务服务端(server/), 帧格式以其源码为准:
  - server/connection.cpp sendResponse(): 发送 htonl(json长度) + json正文
  - server/connection.cpp handleRead():   循环拆包, 天然支持粘包/半包

设计要点(与计划 M1 对应):
  1. 编码: struct.pack('>I', len(body)) + body, body 为 utf-8 的 JSON 文本;
  2. 解码: 接收缓冲累积, 先凑齐 4 字节长度头, 再凑齐正文, 天然处理 TCP 粘包/半包;
     长度非法(0 或 > 4096)说明字节流失步, 抛 ProtocolError 并关闭连接;
  3. 请求锁: 服务端"严格一问一答且响应不回显请求 type"(threadpool.cpp 单任务单响应,
     无请求标识字段), 因此同一时刻只允许一个在途请求, 收到响应后才允许发下一个;
  4. 超时 5 秒: 与旧 C++ 命令行客户端(client/client.cpp)的 SO_RCVTIMEO 保持一致。

用法:
    cli = TicketClient("127.0.0.1", 6000)
    cli.connect()
    resp = cli.request({"type": 3})     # 查票
    cli.close()
"""

from __future__ import annotations

import json
import socket
import struct
import threading

# 与 client/client.cpp recvPacket() 的合法性校验保持一致
MAX_BODY_LEN = 4096
HEADER_LEN = 4

# 服务端操作枚举(server/threadpool.h OP_TYPE)
OP_LOGIN = 1
OP_REGISTER = 2
OP_VIEW = 3
OP_RESERVE = 4
OP_MY_RESERVE = 5
OP_CANCEL = 6
VALID_OPS = {OP_LOGIN, OP_REGISTER, OP_VIEW, OP_RESERVE, OP_MY_RESERVE, OP_CANCEL}


class TicketClientError(Exception):
    """协议客户端基础异常"""


class TransportError(TicketClientError):
    """连接失败 / 连接断开 / 收发超时"""


class ProtocolError(TicketClientError):
    """字节流失步: 长度非法 / JSON 解析失败"""


def encode_frame(payload: dict) -> bytes:
    """dict -> 完整帧: 4 字节大端长度头 + utf-8 JSON 正文。"""
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(body) > MAX_BODY_LEN:
        raise ProtocolError(f"请求正文 {len(body)} 字节, 超过上限 {MAX_BODY_LEN}")
    return struct.pack(">I", len(body)) + body


def try_extract_frame(buf: bytes):
    """从缓冲区尝试取出一个完整帧。

    返回 (帧字节, 剩余缓冲); 数据不足时返回 (None, buf)。
    长度非法时抛 ProtocolError —— 调用方应立即关闭连接, 缓冲已不可信。
    """
    if len(buf) < HEADER_LEN:
        return None, buf
    (body_len,) = struct.unpack(">I", buf[:HEADER_LEN])
    if body_len == 0 or body_len > MAX_BODY_LEN:
        raise ProtocolError(f"非法帧长度: {body_len} (合法范围 1~{MAX_BODY_LEN})")
    if len(buf) < HEADER_LEN + body_len:
        return None, buf
    return buf[HEADER_LEN:HEADER_LEN + body_len], buf[HEADER_LEN + body_len:]


class TicketClient:
    """线程安全的单连接请求-响应客户端。

    connect() 后可反复 request(); 内部用一把互斥锁保证同一时刻只有一个
    在途请求(请求发出 -> 响应收讫 为一个临界区), 多线程调用会被串行化。
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 6000, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: socket.socket | None = None
        self._recv_buf = b""
        self._inflight = threading.Lock()

    # ---- 生命周期 -------------------------------------------------------

    def connect(self, sock: socket.socket | None = None) -> None:
        """建立连接。sock 参数用于单元测试注入假 socket, 生产调用不传。"""
        self.close()
        if sock is not None:
            self._sock = sock
        else:
            try:
                self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
            except OSError as e:
                raise TransportError(f"连接 {self.host}:{self.port} 失败: {e}") from e
        self._recv_buf = b""

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        self._recv_buf = b""

    @property
    def connected(self) -> bool:
        return self._sock is not None

    def __enter__(self) -> "TicketClient":
        if not self.connected:
            self.connect()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # ---- 收发 -----------------------------------------------------------

    def request(self, payload: dict) -> dict:
        """发送一个 JSON 请求并阻塞等待对应响应, 返回解析后的 dict。

        - payload 必须含合法 type(1~6), 提早拦截低级错误;
        - 持锁期间完成 发送->接收->解析, 保证一问一答不被交错;
        - 缓冲区可能残留下一帧的头部字节(服务端不应主动多发, 但粘包残留
          属于正常防御), 留给下一次 request() 优先消费。
        """
        if not isinstance(payload, dict) or payload.get("type") not in VALID_OPS:
            raise ProtocolError(f"非法请求 type: {payload!r}")

        with self._inflight:
            sock = self._sock
            if sock is None:
                raise TransportError("未连接, 请先 connect()")

            try:
                sock.sendall(encode_frame(payload))
                body = self._recv_one_frame(sock)
            except TicketClientError:
                raise
            except socket.timeout as e:  # 3.10+ socket.timeout 即 TimeoutError
                self.close()
                raise TransportError(f"等待响应超时 {self.timeout}s") from e
            except OSError as e:
                self.close()
                raise TransportError(f"连接异常: {e}") from e

            try:
                resp = json.loads(body.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as e:
                self.close()
                raise ProtocolError(f"响应不是合法 JSON: {body[:80]!r}") from e
            if not isinstance(resp, dict):
                self.close()
                raise ProtocolError(f"响应不是 JSON 对象: {resp!r}")
            return resp

    def _recv_one_frame(self, sock: socket.socket) -> bytes:
        """从 socket 循环收数据直到凑齐一个完整帧, 返回帧正文(不含长度头)。

        半包: 缓冲不足时继续 recv; 粘包: 多余字节保留在 _recv_buf。
        """
        while True:
            try:
                frame, self._recv_buf = try_extract_frame(self._recv_buf)
            except ProtocolError:
                # 长度非法 => 字节流失步, 该连接不可再信, 立即关闭
                self.close()
                raise
            if frame is not None:
                return frame
            chunk = sock.recv(4096)
            if not chunk:
                self.close()
                raise TransportError("连接已被服务端关闭")
            self._recv_buf += chunk
