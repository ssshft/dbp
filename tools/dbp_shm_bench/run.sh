#!/usr/bin/env bash
# dbp 共享内存方案 benchmark —— 一键编译并运行。
#
# 为什么需要这套桩：本机 macOS 编不了整个 dbp（缺 boost/fmt/rapidjson），
# 但 sp::Writer / sp::Reader / fmapmem::mmap / ShmSpmcQueue 都是纯 POSIX
# 头文件（+ 一个 .cxx），可以直接编。真正编不过的只有 data_struct.h
# （它拖进 rapidjson + fmt），而 DbpTopic/DbpData 一个字段都不用 data_struct.h
# —— 所以用 stub/ 里的 10 行最小桩顶上，结构体定义仍从 dbp/sig/dbp/include.h
# 原样编进来。sizeof 因此就是生产值。
#
# 用法：
#   ./run.sh          sp::Writer/Reader vs 旧 SPMCQueue（shm_spmc_queue.h）
#   ./run.sh spmc     ShmSpmcQueue（行情发布实际在用的那套，seqlock + popLatest O(1)）
#   ./run.sh slim     载荷 600 → 344 B 的收益（8 个 [5] 数组只用 [0]）
#   ./run.sh topicid  Execute 热路径：拼 key / find / 自带 id / 时钟读取（不需要桩）
#   ./run.sh hotadd   热加对子的不变式：snaps.size() == snapscount（不需要桩）
#   ./run.sh crash    「第 501 个对子 SIGSEGV」复现（预期 exit 139）
#   ./run.sh all      全部跑一遍
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"          # → dbp/
CXX="${CXX:-/usr/bin/clang++}"
OUT="${TMPDIR:-/tmp}/dbp_shm_bench"
MODE="${1:-base}"

mkdir -p "$OUT"

# 公共编译参数。
#   -w  : shmpool.h 里有若干 "字符串字面量 + 整数" 的告警（异常路径），与本测量无关
#   -I  : stub 在前，保证 stub/data_struct.h 遮蔽 include/data_struct.h
#   -include shm_shim.h : 必须最先，见该文件头部说明（macOS 的 shm flock）
COMMON=(-std=c++17 -O2 -w
        -I"$HERE/stub" -I"$ROOT/sig" -I"$ROOT/../include"
        -include "$HERE/shm_shim.h")

build() {   # build <源文件> <输出名>
    "$CXX" "${COMMON[@]}" -o "$OUT/$2" "$HERE/$1" "$ROOT/sig/src/mmap.cxx"
}

case "$MODE" in
    base)
        build bench.cpp bench && "$OUT/bench"
        ;;
    spmc)
        build bench_spmc.cpp bench_spmc && "$OUT/bench_spmc"
        ;;
    slim)
        build bench_slim.cpp bench_slim && "$OUT/bench_slim"
        ;;
    topicid)
        # 这个不依赖任何 dbp 头，单独编就行（不用 COMMON 里的 -I/-include）
        "$CXX" -std=c++17 -O2 -w -o "$OUT/bench_topicid" "$HERE/bench_topicid.cpp" \
            && "$OUT/bench_topicid"
        ;;
    hotadd)
        # 同上，纯 std，不依赖 dbp 头
        "$CXX" -std=c++17 -O1 -w -o "$OUT/hotadd_check" "$HERE/hotadd_check.cpp" \
            && "$OUT/hotadd_check"
        ;;
    crash)
        build crash_test.cpp crash_test
        echo "--- 预期在「已加到 500 列」之后 SIGSEGV（exit 139）---"
        set +e
        "$OUT/crash_test"
        echo "EXIT=$?"
        exit 0
        ;;
    all)
        for m in base spmc slim; do
            echo "================ $m ================"
            build "bench$([ "$m" = base ] && echo '' || echo "_$m").cpp" "b_$m"
            "$OUT/b_$m"
        done
        echo "================ topicid ================"
        "$CXX" -std=c++17 -O2 -w -o "$OUT/bench_topicid" "$HERE/bench_topicid.cpp" \
            && "$OUT/bench_topicid"
        echo "================ crash ================"
        build crash_test.cpp crash_test
        set +e
        "$OUT/crash_test"
        echo "EXIT=$?"
        exit 0
        ;;
    *)
        echo "未知参数: $MODE（可用: base|spmc|slim|crash|all）" >&2
        exit 2
        ;;
esac
