"""M2 工具层: 把四个业务操作封装为带结构化返回的函数, 供 Agent 调用。

设计约定:
  1. 登录态(tel/user_name)由 TicketSession 持有, 不经过 LLM —— 密码永远
     不进入模型上下文; 四个工具只做"登录后的业务";
  2. 每个工具返回纯 dict(JSON 可序列化), 形如
        {"ok": bool, "action": str, ...detail, "reason": str, "message": str}
     失败时 reason 取值固定(见 REASON_*), 供模型决定下一步话术;
  3. 服务端响应不区分失败原因(只回 status:"ERR"), 工具层用"先查后做"
     补出人话原因: 售罄/无此票/非本人预约等; 无法判定的归 server_rejected;
  4. 服务端事实(以 db_manager.cpp 为准): 余票扣减在 C++ 事务里完成,
     工具层不碰库存一致性; 服务端不防重复预订(同一 tel 可重复订同一班次)。

响应字段注意: tk_id/max/num/yd_id 在服务端响应里是字符串(MySQL 行值
直接赋给 Json::Value), 本层统一转 int。
"""

from __future__ import annotations

from agent.protocol import TicketClient, TicketClientError

# 失败原因码(供 Agent 判断话术, 不直接展示给用户)
REASON_NOT_LOGGED_IN = "not_logged_in"          # 未登录(会话无 tel)
REASON_SOLD_OUT = "sold_out"                    # 已售罄(余票 0)
REASON_TICKET_NOT_FOUND = "ticket_not_found"    # tk_id 不存在
REASON_NOT_YOURS = "not_yours"                  # 预约号不存在或不属于当前账号
REASON_SERVER_REJECTED = "server_rejected"      # 服务端拒绝且原因不明(如并发抢完)
REASON_NETWORK = "network_error"                # 连接/超时/协议错误


class TicketSession:
    """一次 Agent 会话 = 一条 TCP 连接 + 登录态。

    login/register 由 CLI 在进入 LLM 循环前确定性地完成(重试由人控制),
    LLM 只拿到"当前用户是谁", 拿不到密码。
    """

    def __init__(self, client: TicketClient | None = None):
        self.client = client if client is not None else TicketClient()
        self.tel: str | None = None
        self.user_name: str | None = None

    # ---- 登录/注册(非 LLM 工具, CLI 调用) --------------------------------

    def register(self, tel: str, user_name: str, passwd: str) -> bool:
        resp = self._req({"type": 2, "user_tel": tel,
                          "user_name": user_name, "user_passwd": passwd})
        return resp.get("status") == "OK"

    def login(self, tel: str, passwd: str) -> bool:
        resp = self._req({"type": 1, "user_tel": tel, "user_passwd": passwd})
        if resp.get("status") == "OK":
            self.tel = tel
            self.user_name = resp.get("user_name", "")
            return True
        return False

    def logout(self) -> None:
        self.tel = None
        self.user_name = None

    def _req(self, payload: dict) -> dict:
        """统一请求出口: 网络/协议异常转成带 reason 的 dict, 便于 CLI 提示。"""
        try:
            return self.client.request(payload)
        except TicketClientError as e:
            return {"status": "ERR", "reason": REASON_NETWORK, "message": str(e)}

    # ---- 工具内部公用 -----------------------------------------------------

    def _require_login(self) -> dict | None:
        """未登录时返回失败 dict(含提示先登录), 已登录返回 None。"""
        if self.tel is None:
            return {"ok": False, "action": "", "reason": REASON_NOT_LOGGED_IN,
                    "message": "当前未登录, 请先完成登录"}
        return None


# ---------------------------------------------------------------------------
# 四个业务工具: 函数签名与 docstring 即 LLM 可读的能力说明
# ---------------------------------------------------------------------------

def query_tickets(session: TicketSession) -> dict:
    """查询全部可售车票列表(班次、日期、总票数、已订数、余票)。

    Returns:
        {"ok": True, "tickets": [{"tk_id", "addr", "use_date", "total", "used", "remaining"}]}
        失败: {"ok": False, "reason", "message"}
    """
    resp = session._req({"type": 3})
    if resp.get("status") != "OK":
        return _fail("query_tickets", resp.get("reason", REASON_SERVER_REJECTED),
                     "查询车票失败, 服务端返回错误")
    tickets = []
    for row in resp.get("arr", []):
        try:
            total = int(row["max"])
            used = int(row["num"])
        except (KeyError, ValueError, TypeError):
            continue  # 脏数据跳过, 不让单行问题炸掉整个查询
        tickets.append({
            "tk_id": int(row["tk_id"]),
            "addr": row["addr"],
            "use_date": row["use_date"],
            "total": total,
            "used": used,
            "remaining": total - used,
        })
    tickets.sort(key=lambda t: t["tk_id"])
    return {"ok": True, "action": "query_tickets", "tickets": tickets}


def reserve_ticket(session: TicketSession, tk_id: int, *, confirmed: bool = False) -> dict:
    """预订指定班次一张票(需用户已确认, confirmed=True 才真正下单)。

    Args:
        tk_id: 班次编号(来自 query_tickets)
        confirmed: M3 确认门控标记 —— 工具执行器只在 confirmed=True 时
            才允许调用本函数真实下单; 详见 agent.py 的门控说明。

    Returns:
        成功: {"ok": True, "tk_id", "addr", "use_date", "remaining_after"}
        失败: {"ok": False, "reason", "message", 可选 "alternatives"}
    """
    if (deny := session._require_login()):
        return deny

    # 先查后做: 用最新余票做前置判断, 同时取信息用于确认话术与失败归因
    listing = query_tickets(session)
    if not listing["ok"]:
        return _fail("reserve", listing["reason"], listing["message"])
    ticket = next((t for t in listing["tickets"] if t["tk_id"] == tk_id), None)
    if ticket is None:
        return _fail("reserve", REASON_TICKET_NOT_FOUND,
                     f"不存在编号为 {tk_id} 的班次")
    if ticket["remaining"] <= 0:
        return {
            "ok": False, "action": "reserve", "reason": REASON_SOLD_OUT,
            "message": f"{ticket['addr']} {ticket['use_date']} 已售罄"
                       f"({ticket['used']}/{ticket['total']})",
            "alternatives": [t for t in listing["tickets"] if t["remaining"] > 0],
        }

    resp = session._req({"type": 4, "tel": session.tel, "index": tk_id})
    if resp.get("status") != "OK":
        # 预查有余票但服务端拒绝: 典型为并发抢完; 服务端不防重复预订,
        # 同一账号重复订同一班次不会走到这个分支
        return _fail("reserve", REASON_SERVER_REJECTED,
                     f"预订 {ticket['addr']} 失败, 可能已被抢完, 建议重新查询")
    return {
        "ok": True, "action": "reserve",
        "tk_id": tk_id, "addr": ticket["addr"], "use_date": ticket["use_date"],
        "remaining_after": ticket["remaining"] - 1,
    }


def my_reservations(session: TicketSession) -> dict:
    """查询当前用户全部预约记录(预约号、班次、日期)。

    Returns:
        {"ok": True, "reservations": [{"yd_id", "addr", "use_date"}]}
    """
    if (deny := session._require_login()):
        return deny
    resp = session._req({"type": 5, "tel": session.tel})
    if resp.get("status") != "OK":
        return _fail("my_reservations", REASON_SERVER_REJECTED,
                     "查询我的预约失败, 服务端返回错误")
    reservations = []
    for row in resp.get("arr", []):
        try:
            reservations.append({
                "yd_id": int(row["yd_id"]),
                "addr": row["addr"],
                "use_date": row["use_date"],
            })
        except (KeyError, ValueError, TypeError):
            continue
    reservations.sort(key=lambda r: r["yd_id"])
    return {"ok": True, "action": "my_reservations", "reservations": reservations}


def cancel_reservation(session: TicketSession, yd_id: int, *, confirmed: bool = False) -> dict:
    """取消本人一条预约(需用户已确认, confirmed=True 才真正取消)。

    Args:
        yd_id: 预约编号(来自 my_reservations)
        confirmed: 同 reserve_ticket 的门控标记。

    Returns:
        成功: {"ok": True, "yd_id", "addr", "use_date"}
        失败: {"ok": False, "reason", "message"}
    """
    if (deny := session._require_login()):
        return deny

    # 先查后做: 服务端取消校验 yd_id+tel 双条件(db_manager.cpp
    # cancelReservedTicket), 预查可把"别人的预约号"归因得更准
    listing = my_reservations(session)
    if not listing["ok"]:
        return _fail("cancel", listing["reason"], listing["message"])
    mine = next((r for r in listing["reservations"] if r["yd_id"] == yd_id), None)
    if mine is None:
        return _fail("cancel", REASON_NOT_YOURS,
                     f"预约号 {yd_id} 不存在或不属于当前账号, 无法取消"
                     f"(当前账号共有 {len(listing['reservations'])} 条预约)")

    resp = session._req({"type": 6, "tel": session.tel, "index": yd_id})
    if resp.get("status") != "OK":
        return _fail("cancel", REASON_SERVER_REJECTED,
                     f"取消预约 {yd_id} 失败, 服务端返回错误")
    return {"ok": True, "action": "cancel", **mine}


def _fail(action: str, reason: str, message: str) -> dict:
    return {"ok": False, "action": action, "reason": reason, "message": message}


# Agent 工具注册表: 名称 -> (函数, 是否危险操作[需确认门控])
TOOL_REGISTRY: dict[str, tuple] = {
    "query_tickets": (query_tickets, False),
    "reserve_ticket": (reserve_ticket, True),
    "my_reservations": (my_reservations, False),
    "cancel_reservation": (cancel_reservation, True),
}
