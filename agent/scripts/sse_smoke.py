import json
import subprocess
import sys
import time
import urllib.request

# V2-M1 冒烟: 起服务 -> 登录 -> SSE 流式 /chat/stream -> 校验事件序(缺 Key 时 error+done)
PORT = 8910
BASE = f"http://127.0.0.1:{PORT}"

proc = subprocess.Popen([sys.executable, "-m", "agent.service", "--port", str(PORT)],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    for _ in range(30):
        time.sleep(1)
        try:
            urllib.request.urlopen(f"{BASE}/health", timeout=2).read()
            break
        except Exception:
            pass
    else:
        raise SystemExit("service not ready")

    body = json.dumps({"tel": "13977770944", "user_name": "sse_smoke",
                       "passwd": "demo123"}).encode()
    req = urllib.request.Request(f"{BASE}/register", data=body,
                                 headers={"Content-Type": "application/json"})
    sid = json.loads(urllib.request.urlopen(req, timeout=10).read())["session_id"]
    print("login ok, sid =", sid[:8])

    body = json.dumps({"session_id": sid, "text": "有哪些票"}).encode()
    req = urllib.request.Request(f"{BASE}/chat/stream", data=body,
                                 headers={"Content-Type": "application/json"})
    resp = urllib.request.urlopen(req, timeout=60)
    ctype = resp.headers.get("Content-Type", "")
    print("content-type:", ctype)
    assert ctype.startswith("text/event-stream"), ctype
    events = []
    for raw in resp:
        line = raw.decode("utf-8").strip()
        if line.startswith("data: "):
            events.append(json.loads(line[6:]))
    kinds = [e["type"] for e in events]
    print("event kinds:", kinds)
    assert kinds[-1] == "done", kinds
    if "error" in kinds:  # 无 Key 环境: 优雅收尾
        assert kinds[0] == "error" and "DEEPSEEK" in events[0].get("message", "")
    print("SSE smoke PASS")
finally:
    proc.terminate()
