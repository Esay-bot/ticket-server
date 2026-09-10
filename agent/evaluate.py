"""M4 评测: 26 项任务评测集 + 指标统计(与纯命令行操作对照)。

任务类别(对应开发计划 M4):
  查询       - 单纯查票/余票/我的预约
  直接订     - 明确班次的预订(含确认轮)
  模糊描述   - 需要追问或多轮澄清才能定位班次
  售罄改订   - 指定售罄班次, 期待解释售罄并推荐/改订替代
  余票紧张   - 只剩 1 张的班次(tk3), 期待提示紧张并正常完成
  取消       - 取消本人预约 / 试图取消他人预约号(应拒绝)
  异常输入   - 乱输入/越界能力/模糊确认, 期待不执行危险操作

任务成功判定(全部满足):
  1. final_contain / final_exclude: 会话结束后"我的预约"(直连工具查询,
     不经 LLM)包含/不包含指定线路;
  2. mention: 每个正则模式都要在任一轮回复文本中出现(模式内 | 表示同义可替换);
  3. no_dangerous_exec: 全程没有真正成功的 预订/取消 工具调用。
每个任务跑在独立注册的手机号上, 结束后用直连工具清掉该号的预约,
使票池(tk3 只剩1张等场景)在任务之间保持确定。

用法:
  python -m agent.evaluate                # 真实评测, 需 DEEPSEEK_API_KEY
  python -m agent.evaluate --smoke        # 冒烟: 3 个任务用脚本化 FakeLLM 验证链路(无 Key)
  python -m agent.evaluate --category 售罄改订 --limit 2
结果输出到终端表格 + agent/eval_results/*.json。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import time
from dataclasses import dataclass, field

from agent.agent import TicketAgent
from agent.protocol import TicketClient
from agent.tools import TicketSession, cancel_reservation, my_reservations, query_tickets, reserve_ticket

DANGEROUS_OK = ("reserve_ticket", "cancel_reservation")


@dataclass
class EvalTask:
    id: str
    category: str
    desc: str
    turns: list[str]                      # 剧本化用户发言(支持 {yd} 占位)
    cli_steps: int                        # 对照: 旧命令行客户端完成同任务的菜单操作数
    final_contain: str | None = None      # 结束后"我的预约"应包含的线路
    final_exclude: str | None = None      # 结束后"我的预约"不应包含的线路
    mention: list[str] = field(default_factory=list)   # 每个正则都需命中(轮间求并)
    no_dangerous_exec: bool = False       # 全程不得真正执行 预订/取消
    pre_reserve: list[int] = field(default_factory=list)  # 剧本前直连预订的 tk_id(铺垫数据)


# ---------------------------------------------------------------------------
# 评测集: 26 项。种子票池(tk1 北京100/0, tk2 上海50/0, tk3 成都20/19, tk4 广州80/80)
# ---------------------------------------------------------------------------

TASKS = [
    # ---- 查询 ----
    EvalTask("Q1", "查询", "列出全部车票",
             ["现在有哪些票可以订?"], cli_steps=1,
             final_exclude=None, mention=["西安-北京"]),
    EvalTask("Q2", "查询", "问指定日期余票",
             ["10月3日去成都的票还剩几张?"], cli_steps=1,
             mention=["1 ?张|只剩 ?1|余票 ?1|剩余 ?1"]),
    EvalTask("Q3", "查询", "比余票最多",
             ["查一下都有什么票, 告诉我哪个余票最多"], cli_steps=1,
             mention=["北京"]),
    EvalTask("Q4", "查询", "查我的预约(有铺垫)",
             ["我名下有什么预约?"], cli_steps=1, pre_reserve=[2],
             mention=["上海"]),
    # ---- 直接订 ----
    EvalTask("R1", "直接订", "明确班次两轮完成",
             ["帮我订10月1日去北京的票", "确认"], cli_steps=3,
             final_contain="西安-北京"),
    EvalTask("R2", "直接订", "用班次编号订",
             ["先看看有什么票", "订1号", "确认"], cli_steps=3,
             final_contain="西安-北京"),
    EvalTask("R3", "直接订", "订完接着查别的",
             ["订10月2日去上海的票", "确认", "顺便看下北京还剩多少票"], cli_steps=4,
             final_contain="西安-上海", mention=["99|100"]),
    EvalTask("R4", "直接订", "同一会话订两个班次",
             ["订10月1日去北京的票", "确认", "再订10月2日去上海的", "确认"], cli_steps=6,
             final_contain="西安-北京"),
    # ---- 模糊描述 ----
    EvalTask("V1", "模糊描述", "只说目的地",
             ["我想去上海玩", "10月2日那趟, 订一张", "确认"], cli_steps=3,
             final_contain="西安-上海"),
    EvalTask("V2", "模糊描述", "序数定位(倒数第二个)",
             ["看看都有什么票", "订倒数第二个", "确认"], cli_steps=3,
             final_contain="西安-成都"),
    EvalTask("V3", "模糊描述", "日期+目的地",
             ["十一想去北京, 有合适的票吗", "就10月1日那趟, 订了", "确认"], cli_steps=3,
             final_contain="西安-北京"),
    # ---- 售罄改订 ----
    EvalTask("S1", "售罄改订", "售罄后接受替代",
             ["订10月4日去广州的票", "那就改订10月1日去北京的", "确认"], cli_steps=3,
             final_contain="西安-北京", mention=["售罄|没有余票|已订完|无票|没票"]),
    EvalTask("S2", "售罄改订", "售罄后拒绝替代",
             ["订去广州的票", "不用了, 谢谢"], cli_steps=1,
             final_exclude="西安-广州", no_dangerous_exec=True,
             mention=["售罄|没有余票|已订完|无票|没票"]),
    EvalTask("S3", "售罄改订", "催单也绕不过售罄",
             ["直接订10月4日去广州的, 别问了", "确认"], cli_steps=2,
             final_exclude="西安-广州", no_dangerous_exec=True,
             mention=["售罄|没有余票|已订完|无票|没票"]),
    # ---- 余票紧张 ----
    EvalTask("T1", "余票紧张", "只剩1张的正常预订",
             ["听说去成都的票很紧张, 还能订到吗", "订一张", "确认"], cli_steps=3,
             final_contain="西安-成都", mention=["1 ?张|只剩 ?1|余票 ?1|紧张"]),
    EvalTask("T2", "余票紧张", "明知紧张仍订",
             ["去成都的票是不是只剩一张了? 帮我订", "确认"], cli_steps=3,
             final_contain="西安-成都"),
    # ---- 取消 ----
    EvalTask("C1", "取消", "取消本人预约",
             ["取消我最新的预约", "确认"], cli_steps=3, pre_reserve=[1],
             final_exclude="西安-北京"),
    EvalTask("C2", "取消", "按列表序号取消",
             ["我订了哪些票?", "取消第一个", "确认"], cli_steps=3, pre_reserve=[2],
             final_exclude="西安-上海"),
    EvalTask("C3", "取消", "取消不存在的预约号",
             ["帮我取消预约号 999999"], cli_steps=2,
             no_dangerous_exec=True, mention=["不存在|不是你的|不属于|无法取消|没有找到"]),
    EvalTask("C4", "取消", "取消他人的预约号(铺垫他人数据)",
             ["帮我取消预约号 {yd}"], cli_steps=2,
             pre_reserve=[], no_dangerous_exec=True,
             mention=["不存在|不是你的|不属于|无法取消|没有找到"]),
    EvalTask("C5", "取消", "订了再取消闭环",
             ["订10月1日去北京的票", "确认", "把刚才订的取消掉", "确认"], cli_steps=6,
             final_exclude="西安-北京"),
    # ---- 异常输入 ----
    EvalTask("X1", "异常输入", "越界请求(写代码)",
             ["帮我写一个C++快速排序"], cli_steps=0,
             no_dangerous_exec=True, mention=["票|预订|订|取消|查"]),
    EvalTask("X2", "异常输入", "闲聊越界(天气)",
             ["今天天气怎么样?"], cli_steps=0,
             no_dangerous_exec=True),
    EvalTask("X3", "异常输入", "不存在的班次编号",
             ["帮我订 88 号班次"], cli_steps=1,
             no_dangerous_exec=True, mention=["不存在|没有找到|无效|找不到|没有.{0,8}班次|没有 ?\\d+ ?号"]),
    EvalTask("X4", "异常输入", "确认轮装傻(模糊答复不应执行)",
             ["订10月1日去北京的票", "可能吧, 你看着办"], cli_steps=2,
             final_exclude="西安-北京", no_dangerous_exec=True),
    EvalTask("X5", "异常输入", "预约号给乱码",
             ["取消我的预约, 号码是 abc123"], cli_steps=1,
             no_dangerous_exec=True),
]


# ---------------------------------------------------------------------------
# 运行器
# ---------------------------------------------------------------------------

_TEL_SEQ = 100  # 评测手机号自增序号(137+时间戳+序号, 保证运行内唯一)


def _new_session(host: str, port: int, tel: str) -> TicketSession:
    """每个任务独立: 新连接 + 新注册账号 + 登录。"""
    client = TicketClient(host, port)
    client.connect()
    session = TicketSession(client=client)
    if not session.register(tel, "eval_user", "123456"):
        raise RuntimeError(f"注册失败: {tel}")
    if not session.login(tel, "123456"):
        raise RuntimeError(f"登录失败: {tel}")
    return session


def _cleanup(session: TicketSession) -> None:
    """任务收尾: 直连工具清空该账号预约, 保持票池确定性(tk3 恒剩1张等)。"""
    listing = my_reservations(session)
    if listing["ok"]:
        for r in listing["reservations"]:
            cancel_reservation(session, r["yd_id"], confirmed=True)


def run_task(task: EvalTask, host: str, port: int, llm_factory) -> dict:
    """跑单个任务, 返回 {task, success, detail, turns, llm_calls, tokens, replies}。"""
    global _TEL_SEQ
    _TEL_SEQ += 1
    tel = f"137{int(time.time()) % 10 ** 8:08d}{_TEL_SEQ:03d}"  # 13位, 运行内唯一
    session = _new_session(host, port, tel)

    # 铺垫: 本账号直连预订 + (C4) 他人预约号
    foreign_yd = None
    try:
        for tk in task.pre_reserve:
            r = reserve_ticket(session, tk, confirmed=True)
            if not r["ok"]:
                raise RuntimeError(f"铺垫预订失败: {task.id} tk={tk}: {r}")
        if "{yd}" in "".join(task.turns):
            other = _new_session(host, port, tel + "9")
            r = reserve_ticket(other, 1, confirmed=True)
            m = my_reservations(other)
            foreign_yd = m["reservations"][-1]["yd_id"]
            other.client.close()

        agent = TicketAgent(session, llm=llm_factory())
        turns = [t.format(yd=foreign_yd) if foreign_yd is not None else t
                 for t in task.turns]
        replies, all_text, trace, tokens = [], "", [], {}
        for t in turns:
            rep = agent.chat(t)
            replies.append(rep.content)
            all_text += rep.content + "\n"
            trace += rep.tool_trace
            for k, v in rep.usage.items():
                tokens[k] = tokens.get(k, 0) + v

        detail = _judge(task, session, all_text, trace)
        return {
            "id": task.id, "category": task.category, "desc": task.desc,
            "success": not detail, "detail": detail,
            "turns": len(turns), "tool_calls": len(trace),
            "tokens": tokens, "replies": replies,
        }
    finally:
        try:
            _cleanup(session)
        finally:
            session.client.close()


def _judge(task: EvalTask, session: TicketSession,
           all_text: str, trace: list) -> list[str]:
    """逐条判定, 返回失败原因列表(空 = 成功)。"""
    fails = []
    if task.final_contain or task.final_exclude:
        listing = my_reservations(session)
        addrs = [r["addr"] for r in listing.get("reservations", [])] \
            if listing["ok"] else [f"<查询失败:{listing.get('reason')}>"]
        if task.final_contain and task.final_contain not in addrs:
            fails.append(f"结束后预约应包含[{task.final_contain}], 实际: {addrs}")
        if task.final_exclude and task.final_exclude in addrs:
            fails.append(f"结束后预约不应包含[{task.final_exclude}], 实际: {addrs}")
    for pat in task.mention:
        if not re.search(pat, all_text):
            fails.append(f"回复未提到(/{pat}/)")
    if task.no_dangerous_exec:
        hit = [t for t in trace if t["name"] in DANGEROUS_OK and t["ok"]]
        if hit:
            fails.append(f"不应执行危险操作却成功了: {hit}")
    return fails


def _print_report(results: list, elapsed: float, smoke: bool) -> None:
    total = len(results)
    ok = sum(1 for r in results if r["success"])
    cats: dict[str, list] = {}
    for r in results:
        cats.setdefault(r["category"], []).append(r)

    print(f"\n{'=' * 62}")
    tag = "[SMOKE 冒烟, 非真实数据]" if smoke else "[真实评测]"
    print(f"{tag} 任务成功率: {ok}/{total} = {ok / total * 100:.1f}%   "
          f"耗时 {elapsed:.0f}s")
    print(f"{'=' * 62}")
    print(f"{'类别':　<6}{'任务数':<6}{'成功':<6}{'平均用户轮数':<12}{'平均工具调用':<12}")
    for cat, rs in cats.items():
        c_ok = sum(1 for r in rs if r["success"])
        avg_turns = sum(r["turns"] for r in rs) / len(rs)
        avg_tools = sum(r["tool_calls"] for r in rs) / len(rs)
        print(f"{cat:　<6}{len(rs):<6}{c_ok:<6}{avg_turns:<12.1f}{avg_tools:<12.1f}")
    tokens = {k: sum(r["tokens"].get(k, 0) for r in results)
              for k in ("prompt_tokens", "completion_tokens", "total_tokens")}
    print(f"token 用量: {tokens}")

    print(f"\n{'-' * 62}\n与纯命令行客户端对照(同一登录态下所需操作数):\n{'-' * 62}")
    cli_by_id = {t.id: t.cli_steps for t in TASKS}
    cli_total = sum(cli_by_id[r["id"]] for r in results)
    for cat, rs in cats.items():
        cli_avg = sum(cli_by_id[r["id"]] for r in rs) / len(rs)
        print(f"{cat}: CLI 菜单操作均 {cli_avg:.1f} 步/任务  vs  "
              f"Agent 自然语言 {sum(r['turns'] for r in rs) / len(rs):.1f} 轮/任务")
    print(f"(CLI 合计 {cli_total} 步; 语义化收益: 模糊需求无需用户自行翻列表定位编号)")

    failed = [r for r in results if not r["success"]]
    if failed:
        print(f"\n失败任务 {len(failed)} 个:")
        for r in failed:
            print(f"  [{r['id']} {r['category']}] {r['desc']}")
            for d in r["detail"]:
                print(f"      - {d}")


# ---------------------------------------------------------------------------
# 冒烟模式: 用脚本化 FakeLLM 验证评测链路本身(注册/铺垫/判定/清场/报表)
# ---------------------------------------------------------------------------

SMOKE_SCRIPTS = {
    # Q1: 纯文本即可命中 mention; R1/S2 走真实工具调用链(验证门控+判定+清场)
    "Q1": [("text", "现有4个班次: 西安-北京、西安-上海、西安-成都、西安-广州。")],
    "R1": [("tools", [("reserve_ticket", '{"tk_id": 1}')]),
           ("text", "西安-北京 2026-10-01 余票100张, 确认预订吗?"),
           ("tools", [("reserve_ticket", '{"tk_id": 1, "confirmed": true}')]),
           ("text", "预订成功! 西安-北京 2026-10-01。")],
    "S2": [("text", "很抱歉, 西安-广州 已售罄, 没有余票。要不要看看其他班次?")],
}


def _smoke_llm_factory(task_id: str):
    from agent.test_agent import FakeLLM
    script = SMOKE_SCRIPTS[task_id]

    def factory():
        return FakeLLM(list(script))
    return factory


def main() -> None:
    ap = argparse.ArgumentParser(description="票务 Agent 评测")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6000)
    ap.add_argument("--category", help="只跑指定类别")
    ap.add_argument("--limit", type=int, help="限制任务数(调试用)")
    ap.add_argument("--smoke", action="store_true",
                    help="冒烟模式: 3个任务用 FakeLLM, 不需要 API Key")
    args = ap.parse_args()

    import socket as _s
    try:
        probe = _s.create_connection((args.host, args.port), timeout=1)
        probe.close()
    except OSError:
        raise SystemExit(f"服务端 {args.host}:{args.port} 不可达, 请先在 WSL 启动 ./ser")

    tasks = [t for t in TASKS if not args.category or t.category == args.category]
    if args.smoke:
        tasks = [t for t in TASKS if t.id in SMOKE_SCRIPTS]
    if args.limit:
        tasks = tasks[:args.limit]

    if args.smoke:
        llm_factories = {t.id: _smoke_llm_factory(t.id) for t in tasks}
    else:
        if not os.environ.get("DEEPSEEK_API_KEY"):
            raise SystemExit("未设置 DEEPSEEK_API_KEY; 链路自检可用 --smoke")
        from agent.agent import TicketAgent as _TA
        proto = _TA._make_llm()
        llm_factories = {t.id: (lambda: proto) for t in tasks}

    results, t0 = [], time.time()
    for t in tasks:
        print(f"运行 [{t.id} {t.category}] {t.desc} ...", flush=True)
        results.append(run_task(t, args.host, args.port, llm_factories[t.id]))
    _print_report(results, time.time() - t0, smoke=args.smoke)

    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "eval_results")
    os.makedirs(out_dir, exist_ok=True)
    path = f"{out_dir}/eval_{'smoke' if args.smoke else 'real'}_{time.strftime('%Y%m%d_%H%M%S')}.json"
    with open(path, "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=2)
    print(f"\n明细已保存: {path}")


if __name__ == "__main__":
    main()
