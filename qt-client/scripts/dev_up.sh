#!/usr/bin/env bash
# 桌面端一键启动(在 WSL 里执行): 起/复用 Agent 服务层(:8000) + 拉起 Qt 客户端(WSLg 弹到 Windows 桌面)。
# 前置: C++ 服务端(:6000)已启动; 对话需 DEEPSEEK_API_KEY 已 export。
# 用法: bash qt-client/scripts/dev_up.sh [--restart-service]
set -u
DIR="$(cd "$(dirname "$0")/../.." && pwd)"     # 仓库根
API_PORT=8000
HEALTH_URL="http://127.0.0.1:${API_PORT}/health"

# 1) 服务层: 已在跑则复用, 否则启动并等就绪(python 冷启动需数秒)
if curl -s --noproxy '*' -m 2 "$HEALTH_URL" | grep -q '"ok"'; then
  echo "[dev_up] 服务层已在 :${API_PORT} 运行, 复用"
else
  cd "$DIR"
  nohup python3 -m agent.service --port "$API_PORT" >/tmp/agent-service.log 2>&1 &
  echo $! > /tmp/agent-service.pid
  for _ in $(seq 1 40); do
    sleep 1
    if curl -s --noproxy '*' -m 2 "$HEALTH_URL" | grep -q '"ok"'; then
      echo "[dev_up] 服务层已就绪 :${API_PORT} (pid $(cat /tmp/agent-service.pid))"
      break
    fi
  done
  curl -s --noproxy '*' -m 2 "$HEALTH_URL" | grep -q '"ok"' \
    || { echo "[dev_up] 服务层启动失败, 看 /tmp/agent-service.log"; exit 1; }
fi

# 2) Qt 客户端: 杀旧起新(窗口经 WSLg 显示在 Windows 桌面)
pkill -x ticket-client 2>/dev/null && sleep 1
cd "$DIR/qt-client/build"
nohup ./ticket-client >/tmp/qt-client.log 2>&1 &
echo "[dev_up] Qt 客户端已启动 (窗口弹在 Windows 桌面; 日志 /tmp/qt-client.log)"
