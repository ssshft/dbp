// ShmSpmcQueue（行情发布实际在用的那套）vs sp::Writer/Reader（dbp 价差在用的那套）
//
// 上一轮 benchmark 测的是 include/shm_spmc_queue.h 里的 SPMCQueue —— 那是**旧**的一套。
// 行情发布用的是 include/ShmSpmcQueue.h 里的 ShmSpmcQueue（seqlock + published_seq +
// popLatest O(1)），两者不是一回事，必须分开测。
//
// 这里测三件事：
//   1. push 一条（600 B 价差 / 112 B 行情）要多久 —— 载荷是 kWords 个 seq_cst 原子写
//   2. popLatest 一次要多久（O(1) 跳转）
//   3. 多读者时 push 会不会被拖慢（文档说 10 个自旋读者 4.5ns -> 248ns）

#include <ShmSpmcQueue.h>
#include <shmpool/shmpool.h>
#include <dbp/include.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;

static double nsPer(clk::time_point a, clk::time_point b, long n) {
    return std::chrono::duration<double, std::nano>(b - a).count() / (double)n;
}

// 和 md::Depth1 同尺寸（112 B）的替身。队列只关心 sizeof(T) 和 trivially copyable，
// 不关心字段含义，所以这里不需要真实布局。
struct P112 {
    double v[14];
};

template <class Q, class T>
static void benchQueue(const char* name, const T& sample, long N) {
    const std::string shmName = std::string("/bench_") + name;
    Q* q = nullptr;
    try {
        q = new Q(shmName);
    }
    catch (const std::exception& e) {
        std::printf("  [%s] 打开失败: %s\n", name, e.what());
        return;
    }

    std::printf("\n--- %s  sizeof(T)=%zu  kWords=%zu  slot=%zu  cap=%zu  段=%.1f KB ---\n",
                name, sizeof(T),
                (sizeof(T) + 7) / 8,
                Q::slotSize(), Q::capacity(),
                (double)Q::layoutSize() / 1024.0);

    // 1) push
    auto a = clk::now();
    for (long i = 0; i < N; ++i) q->push(sample);
    auto b = clk::now();
    const double pushNs = nsPer(a, b, N);
    std::printf("  push                    %8.1f ns/条\n", pushNs);

    // 2) popLatest：追上之后每次都没有新数据（最常见的一轮）
    T out{};
    q->popLatest(out);
    auto c = clk::now();
    for (long i = 0; i < N; ++i) q->popLatest(out);
    auto d = clk::now();
    std::printf("  popLatest miss          %8.1f ns/次\n", nsPer(c, d, N));

    // 3) push + popLatest 交替（1:1 稳态）
    auto e = clk::now();
    for (long i = 0; i < N; ++i) {
        q->push(sample);
        q->popLatest(out);
    }
    auto f = clk::now();
    std::printf("  push+popLatest（1:1）   %8.1f ns/轮\n", nsPer(e, f, N));

    // 4) 10 个读者都在追（只读 published_seq，不 pop）—— 测发布侧被拖慢多少
    {
        std::vector<Q*> readers;
        for (int i = 0; i < 10; ++i) {
            try { readers.push_back(new Q(shmName)); } catch (...) {}
        }
        auto g = clk::now();
        for (long i = 0; i < N; ++i) {
            q->push(sample);
            for (auto* r : readers) (void)r->publishedSequence();
        }
        auto h = clk::now();
        std::printf("  push + 10 读者瞄 seq    %8.1f ns/轮  (读者 %zu 个)\n",
                    nsPer(g, h, N), readers.size());
        for (auto* r : readers) delete r;
    }

    delete q;
    ::shm_unlink(shmName.c_str());
}

int main() {
    std::printf("=== ShmSpmcQueue（行情发布实际在用）===\n");

    dbp::DbpData d{};
    d.spreadBidAsk = 0.000123;
    d.generateTs = 1700000000000000LL;

    P112 p{};

    const long N = 200000;
    benchQueue<ShmSpmcQueue<dbp::DbpData, 1024>>("dbpdata1024", d, N);
    benchQueue<ShmSpmcQueue<P112, 16384>>("p112_16384", p, N);

    std::printf("\n（对照）sp::Writer/Reader 上一轮实测：\n");
    std::printf("  写一条（Fetch+memcpy+Commit）   18.0 ns\n");
    std::printf("  读 miss 单列                     1.8 ns\n");
    std::printf("  写+读一条（命中）               29.4 ns\n");
    return 0;
}
