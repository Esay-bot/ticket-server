"""M5 CLI 入口: 登录/注册(确定性, 不经 LLM) -> 多轮自然语言对话。

用法:
  set DEEPSEEK_API_KEY=sk-xxxx          (Windows Git Bash: export ...)
  python -m agent.main [--host 127.0.0.1] [--port 6000] [--debug]

设计:
  - 密码只在本地 CLI 与 C++ 服务端之间流转, 永不进入模型上下文;
  - 断线自动重连一次(TCP 重连 + 重新登录), 会话消息保留;
  - /debug 开关打印每轮工具调用轨迹; 输入 q/exit/退出 结束。
"""

from __future__ import annotations

import argparse
import sys

from agent.agent import TicketAgent
from agent.protocol import TicketClient, TransportError
from agent.tools import TicketSession

BANNER = r"""
==========================================================
  票务智能预订 Agent  (DeepSeek Function Calling + 自研二进制协议)
  后端: C++ libevent 票务服务(不改一行代码, 原生对接)
==========================================================
"""


def _print_reply(reply, debug: bool) -> None:
    print(f"\n助手: {reply.content}\n")
    if debug and reply.tool_trace:
        for t in reply.tool_trace:
            print(f"  [tool] {t['name']} ok={t['ok']} reason={t['reason']}")
    if reply.usage:
        u = reply.usage
        print(f"  [tokens] 本轮 prompt={u.get('prompt_tokens', 0)} "
              f"completion={u.get('completion_tokens', 0)}")


def _login_flow(session: TicketSession) -> tuple[str, str] | None:
    """CLI 侧确定性登录/注册, 返回 (tel, passwd) 供断线重连; 失败返回 None。"""
    for _ in range(3):
        choice = input("请选择: 1 登录 / 2 注册 / q 退出 > ").strip()
        tel = input("手机号 > ").strip()
        passwd = input("密码 > ").strip()  # 本地开发演示用, 不做隐藏输入
        if choice == "2":
            name = input("用户名 > ").strip()
            if session.register(tel, name, passwd):
                print(f"注册成功: {tel}")
            else:
                print("注册失败(手机号可能已存在), 重试")
                continue
        if session.login(tel, passwd):
            print(f"登录成功, 欢迎你, {session.user_name}!")
            return tel, passwd
        print("登录失败(手机号或密码错误), 重试")
    return None


def main() -> None:
    ap = argparse.ArgumentParser(description="票务智能预订 Agent CLI")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6000)
    ap.add_argument("--model", default="deepseek-chat")
    ap.add_argument("--debug", action="store_true", help="打印工具调用轨迹与token")
    args = ap.parse_args()

    try:  # Windows 控制台中文输出兜底
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass

    print(BANNER)
    client = TicketClient(args.host, args.port)
    try:
        client.connect()
    except TransportError as e:
        raise SystemExit(f"无法连接服务端 {args.host}:{args.port}: {e}\n"
                         f"请先在 WSL 启动: cd ~/ser-cli/server && ./ser")

    session = TicketSession(client=client)
    cred = _login_flow(session)
    if cred is None:
        client.close()
        return
    tel, passwd = cred

    try:
        agent = TicketAgent(session, model=args.model)
    except RuntimeError as e:
        client.close()
        raise SystemExit(str(e))

    print("开始对话吧(输入 q / exit / 退出 结束):")
    while True:
        try:
            text = input("\n你: ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n再见!")
            break
        if not text:
            continue
        if text.lower() in ("q", "exit", "quit", "退出"):
            print("再见!")
            break

        try:
            reply = agent.chat(text)
        except TransportError as e:
            # 断线重连一次: TCP 重连 + 重新登录, 对话历史仍在内存
            print(f"(连接异常: {e}, 尝试重连...)")
            try:
                client.connect()
                session.tel = None
                if not session.login(tel, passwd):
                    raise TransportError("重连后重新登录失败")
                print("(重连成功, 请重发上一条消息)")
            except TransportError as e2:
                print(f"(重连失败: {e2}, 请重启程序)")
            continue

        _print_reply(reply, args.debug)
        # 工具层网络故障也会以自然语言体现在 reply.content 里, 不在此重试

    client.close()


if __name__ == "__main__":
    main()
