"""V1-M1 服务化层: 把 CLI 形态的票务 Agent 包成本地 HTTP 服务(FastAPI)。

职责(见《Qt+Agent桌面端开发计划》V1-M1):
  - POST /login /register: 建 TicketSession(连 TCP、注册/登录), 返回 session_id;
  - POST /chat:            调 TicketAgent.chat(), 返回 reply/tool_trace/usage/confirm_request;
  - GET  /tickets:         转发 query_tickets(桌面端表格的数据源);
  - GET  /reservations:    转发 my_reservations(桌面端"我的预约"页签);
  - POST /logout:          主动关闭 TCP 并移除会话;
  - GET  /health:          存活探测(界面判断"服务层是否已启动")。

会话管理(计划面试自测题: 多用户会话与连接泄漏):
  - session_id(uuid4.hex) -> _Entry{TicketSession, TicketAgent, Lock, last_active};
    Agent 在首次 /chat 时懒创建 —— 登录与表格不依赖 LLM Key;
  - TTL 清理: 空闲超 30 分钟的会话由后台协程周期清理 + 每次取用时惰性清理,
    清理即关闭 TCP 防连接泄漏; logout 主动关闭;
  - 同一会话的 chat/tickets 持同一把锁串行化(TicketClient 还有请求锁双保险,
    且防 messages 列表被并发写坏); 不同会话并行。

错误分级(人话, 不把 reason 码直接甩给用户):
  - C++ 服务端不可达/连接断开: 统一 503, 提示先在 WSL 启动服务端;
  - 登录失败 401, 注册冲突 409, 会话不存在或过期 404, 参数校验 422(pydantic);
  - chat 中工具的网络故障不转 503: 与 CLI 一致, 工具层把 TicketClientError
    吞成 ok=False 的结构化结果, 由模型转成自然语言向用户解释。

线程模型: 端点全部为同步 def, Starlette 自动丢进线程池 —— agent.chat() 里
最长 60s 的 LLM 调用不会卡住事件循环; 后台清理协程只做字典遍历, 极快。

启动:
  python -m agent.service [--host 127.0.0.1] [--port 8000]
                          [--server-host 127.0.0.1] [--server-port 6000]
  C++ 服务端地址亦可用环境变量 TICKET_SERVER_HOST / TICKET_SERVER_PORT 指定。
"""

from __future__ import annotations

import argparse
import asyncio
import os
import threading
import time
import uuid
from contextlib import asynccontextmanager, suppress
from dataclasses import dataclass, field
from typing import Callable

from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from agent.agent import TicketAgent
from agent.protocol import TicketClient, TransportError
from agent.tools import REASON_NETWORK, TicketSession, my_reservations, query_tickets

DEFAULT_HTTP_HOST = "127.0.0.1"
DEFAULT_HTTP_PORT = 8000
DEFAULT_SESSION_TTL = 30 * 60      # 空闲 30 分钟: 关 TCP, 防连接泄漏
DEFAULT_SWEEP_INTERVAL = 60.0      # 后台清理周期(秒)


# ---------------------------------------------------------------------------
# 会话存储: session_id -> 登录态 TCP 会话 + Agent, 带 TTL 空闲清理
# ---------------------------------------------------------------------------

@dataclass
class _Entry:
    session: TicketSession
    agent: TicketAgent | None = None      # 懒创建: 首次 /chat 时构造, 登录不依赖 LLM Key
    lock: threading.Lock = field(default_factory=threading.Lock)
    last_active: float = field(default_factory=time.monotonic)


def _close_quietly(session: TicketSession) -> None:
    try:
        session.client.close()
    except OSError:
        pass


class SessionStore:
    """所有活跃会话的容器; TTL 到期的会话关闭 TCP 并移除。"""

    def __init__(self, ttl: float = DEFAULT_SESSION_TTL,
                 sweep_interval: float = DEFAULT_SWEEP_INTERVAL):
        self.ttl = ttl
        self.sweep_interval = sweep_interval
        self._entries: dict[str, _Entry] = {}
        self._last_sweep = time.monotonic()

    def create(self, session: TicketSession) -> str:
        session_id = uuid.uuid4().hex
        self._entries[session_id] = _Entry(session=session)
        return session_id

    def get(self, session_id: str) -> _Entry | None:
        """取会话并刷新活跃时间; 顺带触发一次限频的惰性 TTL 清理。"""
        self._maybe_sweep()
        entry = self._entries.get(session_id)
        if entry is not None:
            entry.last_active = time.monotonic()
        return entry

    def pop(self, session_id: str) -> _Entry | None:
        """移除会话并关闭其 TCP 连接(logout / 过期清理共用)。"""
        entry = self._entries.pop(session_id, None)
        if entry is not None:
            _close_quietly(entry.session)
        return entry

    def sweep(self) -> int:
        """关闭并移除所有空闲超时的会话, 返回清理数量。"""
        now = time.monotonic()
        expired = [sid for sid, e in self._entries.items()
                   if now - e.last_active > self.ttl]
        for sid in expired:
            self.pop(sid)
        return len(expired)

    def close_all(self) -> None:
        for sid in list(self._entries):
            self.pop(sid)

    def _maybe_sweep(self) -> None:
        if time.monotonic() - self._last_sweep >= self.sweep_interval:
            self._last_sweep = time.monotonic()
            self.sweep()

    def __len__(self) -> int:
        return len(self._entries)


async def _sweep_loop(store: SessionStore) -> None:
    while True:
        await asyncio.sleep(store.sweep_interval)
        store.sweep()


# ---------------------------------------------------------------------------
# 请求模型(校验放门口, 业务字段同 CLI)
# ---------------------------------------------------------------------------

class LoginIn(BaseModel):
    tel: str = Field(min_length=1, max_length=20)
    passwd: str = Field(min_length=1, max_length=64)


class RegisterIn(LoginIn):
    user_name: str = Field(min_length=1, max_length=32)


class ChatIn(BaseModel):
    session_id: str
    text: str = Field(min_length=1, max_length=2000)


class SessionIn(BaseModel):
    session_id: str


# ---------------------------------------------------------------------------
# 应用工厂: 测试通过注入 connect / agent_factory 替身, 生产走默认实现
# ---------------------------------------------------------------------------

def create_app(*, server_host: str | None = None, server_port: int | None = None,
               connect: Callable[[], TicketClient] | None = None,
               agent_factory: Callable[[TicketSession], TicketAgent] | None = None,
               session_ttl: float = DEFAULT_SESSION_TTL,
               sweep_interval: float = DEFAULT_SWEEP_INTERVAL) -> FastAPI:
    server_host = server_host or os.environ.get("TICKET_SERVER_HOST", "127.0.0.1")
    if server_port is None:
        server_port = int(os.environ.get("TICKET_SERVER_PORT", "6000"))

    if connect is None:
        def connect() -> TicketClient:      # noqa: F811 (默认实现, 测试可注入替身)
            client = TicketClient(server_host, server_port)
            client.connect()                # TransportError 由端点统一转 503
            return client
    if agent_factory is None:
        agent_factory = TicketAgent         # 缺 DEEPSEEK_API_KEY 时 RuntimeError -> 503

    store = SessionStore(ttl=session_ttl, sweep_interval=sweep_interval)

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        sweeper = asyncio.create_task(_sweep_loop(store))
        yield
        sweeper.cancel()
        with suppress(asyncio.CancelledError):
            await sweeper
        store.close_all()                   # 进程退出不残留 TCP 连接

    app = FastAPI(title="票务 Agent 服务层", version="0.1.0", lifespan=lifespan)
    app.state.store = store
    app.state.server_addr = f"{server_host}:{server_port}"
    app.state.connect = connect
    app.state.agent_factory = agent_factory
    app.add_middleware(CORSMiddleware, allow_origins=["*"],
                       allow_methods=["*"], allow_headers=["*"])

    # ---- 登录/注册 ---------------------------------------------------------

    def _open_logged_in(state, tel: str, passwd: str,
                        user_name: str | None = None) -> TicketSession:
        """建 TCP 连接并完成注册(可选)+登录; 失败抛 HTTPException 并关闭连接。

        密码只在本函数与 C++ 服务端之间流转, 不进模型上下文、不回传客户端。
        request() 在网络/协议故障时会自动关闭 socket —— 用"连接是否仍在"
        区分 503(链路问题)与 401/409(业务拒绝)。
        """
        try:
            client = state.connect()
        except TransportError as e:
            raise HTTPException(503, f"票务服务端不可达({state.server_addr}): {e}。"
                                     "请先在 WSL 启动 C++ 服务端(见 agent/README.md)")
        session = TicketSession(client=client)
        if user_name is not None and not session.register(tel, user_name, passwd):
            unreachable = not session.client.connected
            _close_quietly(session)
            if unreachable:
                raise HTTPException(503, _net_msg(state))
            raise HTTPException(409, "注册失败: 该手机号可能已被注册")
        if not session.login(tel, passwd):
            unreachable = not session.client.connected
            _close_quietly(session)
            if unreachable:
                raise HTTPException(503, _net_msg(state))
            raise HTTPException(401, "登录失败: 手机号或密码错误")
        return session

    def _net_msg(state) -> str:
        return f"与票务服务端({state.server_addr})的连接异常, 请稍后重试或重新登录"

    @app.post("/login")
    def login(body: LoginIn, request: Request):
        session = _open_logged_in(request.app.state, body.tel, body.passwd)
        return {"session_id": store.create(session),
                "tel": body.tel, "user_name": session.user_name}

    @app.post("/register")
    def register(body: RegisterIn, request: Request):
        session = _open_logged_in(request.app.state, body.tel, body.passwd,
                                  user_name=body.user_name)
        return {"session_id": store.create(session),
                "tel": body.tel, "user_name": session.user_name}

    # ---- 对话与数据 ---------------------------------------------------------

    @app.post("/chat")
    def chat(body: ChatIn, request: Request):
        entry = store.get(body.session_id)
        if entry is None:
            raise HTTPException(404, "会话不存在或已过期, 请重新登录")
        if not entry.session.client.connected:
            raise HTTPException(503, "与票务服务端的连接已断开, 请重新登录")
        with entry.lock:
            # Agent 懒创建: 登录/表格不需要 Key; 缺 Key 只影响对话, 会话仍可用
            if entry.agent is None:
                try:
                    entry.agent = request.app.state.agent_factory(entry.session)
                except RuntimeError as e:
                    raise HTTPException(503, f"Agent 初始化失败: {e}")
            reply = entry.agent.chat(body.text)   # 同步 def: 线程池执行, 不卡事件循环
        return {"reply": reply.content,
                "tool_trace": reply.tool_trace,
                "usage": reply.usage,
                "confirm_request": reply.confirm_request,
                "error": reply.error}

    @app.get("/tickets")
    def tickets(session_id: str, request: Request):
        entry = store.get(session_id)
        if entry is None:
            raise HTTPException(404, "会话不存在或已过期, 请重新登录")
        with entry.lock:
            out = query_tickets(entry.session)
        if not out.get("ok") and out.get("reason") == REASON_NETWORK:
            raise HTTPException(503, out.get("message") or _net_msg(request.app.state))
        return out

    @app.get("/reservations")
    def reservations(session_id: str, request: Request):
        """我的预约(桌面端"我的预约"页签数据源; 转发 my_reservations)。"""
        entry = store.get(session_id)
        if entry is None:
            raise HTTPException(404, "会话不存在或已过期, 请重新登录")
        with entry.lock:
            out = my_reservations(entry.session)
        if not out.get("ok") and out.get("reason") == REASON_NETWORK:
            raise HTTPException(503, out.get("message") or _net_msg(request.app.state))
        return out

    @app.post("/logout")
    def logout(body: SessionIn, request: Request):
        if store.pop(body.session_id) is None:
            raise HTTPException(404, "会话不存在或已过期")
        return {"ok": True}

    @app.get("/health")
    def health(request: Request):
        return {"status": "ok", "sessions": len(store)}

    return app


# uvicorn agent.service:app --port 8000 直接可用
app = create_app()


def main() -> None:
    import uvicorn

    ap = argparse.ArgumentParser(description="票务 Agent HTTP 服务层(FastAPI)")
    ap.add_argument("--host", default=DEFAULT_HTTP_HOST, help="HTTP 监听地址")
    ap.add_argument("--port", type=int, default=DEFAULT_HTTP_PORT, help="HTTP 监听端口")
    ap.add_argument("--server-host", default=None,
                    help="C++ 票务服务端地址(默认 127.0.0.1 或 TICKET_SERVER_HOST)")
    ap.add_argument("--server-port", type=int, default=None,
                    help="C++ 票务服务端端口(默认 6000 或 TICKET_SERVER_PORT)")
    args = ap.parse_args()
    uvicorn.run(create_app(server_host=args.server_host, server_port=args.server_port),
                host=args.host, port=args.port)


if __name__ == "__main__":
    main()
