#!/usr/bin/env bash
# WSL 侧服务层冒烟: 起 FastAPI -> /health -> 注册 -> /tickets -> 收尾。
# 用法(在 WSL 里): bash agent/scripts/wsl_smoke.sh [端口]
set -u
PORT="${1:-8904}"
DIR="$(cd "$(dirname "$0")/../.." && pwd)"   # 仓库根

pkill -f "agent.service --port $PORT" 2>/dev/null
cd "$DIR"
python3 -m agent.service --port "$PORT" >/tmp/svc_smoke.log 2>&1 &
SVC=$!

R=""
for _ in $(seq 1 40); do
  sleep 1
  R=$(curl -s --noproxy '*' -m 2 "http://127.0.0.1:$PORT/health")
  [ -n "$R" ] && break
done
echo "health: $R"
[ -z "$R" ] && { echo "服务未就绪"; kill $SVC 2>/dev/null; tail -5 /tmp/svc_smoke.log; exit 1; }

TEL="1397777$(date +%S)$(date +%M | tail -c2)"
SID=$(curl -s --noproxy '*' -m 8 -X POST "http://127.0.0.1:$PORT/register" \
  -H 'Content-Type: application/json' \
  -d "{\"tel\": \"$TEL\", \"user_name\": \"wsl_smoke\", \"passwd\": \"demo123\"}" \
  | python3 -c 'import sys,json; print(json.load(sys.stdin).get("session_id",""))')
echo "register: session=${SID:0:8}..."
[ -z "$SID" ] && { kill $SVC 2>/dev/null; exit 1; }

echo "tickets: $(curl -s --noproxy '*' -m 5 "http://127.0.0.1:$PORT/tickets?session_id=$SID" | head -c 100)"
curl -s --noproxy '*' -m 5 -X POST "http://127.0.0.1:$PORT/logout" \
  -H 'Content-Type: application/json' -d "{\"session_id\": \"$SID\"}" >/dev/null
kill $SVC 2>/dev/null
echo "smoke done"
