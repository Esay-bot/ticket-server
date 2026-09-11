#!/usr/bin/env bash
# Qt 测试构建与运行(在 WSL 里执行): bash scripts/build_and_test.sh [test...]
# 默认构建并运行五个测试; model/dialog 需 C++ 服务端(:6000)已启动。
set -u
DIR="$(cd "$(dirname "$0")/.." && pwd)"     # qt-client/
cd "$DIR"

ALL="model_test codec_test net_test dialog_test flow_test"
TESTS="${*:-$ALL}"

for t in $TESTS; do
  echo "=== $t ==="
  (
    cd "tests/$t" && mkdir -p build && cd build \
      && qmake "../$t.pro" >/dev/null 2>&1 \
      && make -j4 >/dev/null 2>&1
  ) || { echo "$t BUILD_FAIL"; continue; }
  [ -x "tests/$t/build/$t" ] || { echo "$t BUILD_FAIL(no binary)"; continue; }
  ( cd "tests/$t/build" && QT_QPA_PLATFORM=offscreen timeout 180 "./$t" 2>&1 \
      | grep -E "^PASS|^FAIL|^Totals|\[PASS\]|\[FAIL\]|全部通过|项失败" | sed "s/^/  /" )
done
