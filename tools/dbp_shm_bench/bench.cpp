// dbp 共享内存方案对比 benchmark
//
// 目的：把「所有对子放一个 shm」vs「每对子一个 SPMC shm」的差别变成 ns 数字。
//
// 关键约束（决定了这个 harness 的可信度）：
//   1. 用的是**真实的** sp::Initialer / sp::Writer / sp::Reader（dbp/sig/shmpool/shmpool.h），
//      和**真实的** fmapmem mmap 实现（dbp/sig/src/mmap.cxx），不是重写的近似版。
//   2. DbpTopic / DbpData 是 dbp/sig/dbp/include.h 原样编进来的，一个字段没改
//      ⇒ 打印出来的 sizeof 就是生产上的 sizeof。
//   3. 唯一被替换的是 data_struct.h（DbpData/DbpTopic 完全不用它，只有 DbpConfig 用），
//      换成 10 行的最小桩，避免拖进 rapidjson/fmt。
//
// 因此这个 harness 只能在本机跑，但它测的是**同一份写/读代码路径**。

#include <shmpool/shmpool.h>
#include <dbp/include.h>
#include <dbp/dbpreader.h>
#include <shm_spmc_queue.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using clk = std::chrono::steady_clock;

static double nsPer(clk::time_point a, clk::time_point b, long n) {
    return std::chrono::duration<double, std::nano>(b - a).count() / (double)n;
}

// 防止循环被优化掉
static volatile void* g_sink = nullptr;

int main() {
    std::printf("=== 结构体尺寸（生产同值）===\n");
    std::printf("  sizeof(sp::CNB)   = %zu\n", sizeof(sp::CNB));
    std::printf("  sizeof(sp::RNB)   = %zu\n", sizeof(sp::RNB));
    std::printf("  sizeof(DbpTopic)  = %zu\n", sizeof(dbp::DbpTopic));
    std::printf("  sizeof(DbpData)   = %zu\n", sizeof(dbp::DbpData));

    const uint32_t topiclen = 100;   // dbp/etc/dbprocess.xml: <topiclen>100</topiclen>
    const uint32_t colsize  = 500;   // dbp::DBP_COL_SIZE
    const uint32_t P        = 8;     // 订阅对子数

    // ---------------------------------------------------------------- 初始化
    const char* mpath = "/tmp/dbpbench/shm/DBP_TOPIC_bench";
    const char* dpath = "/tmp/dbpbench/shm/DBP_DATAS_bench";
    std::remove(mpath);
    std::remove(dpath);

    std::vector<dbp::DbpTopic> vc(P);
    for (uint32_t i = 0; i < P; ++i) {
        std::snprintf(vc[i].__name, sizeof(vc[i].__name), "BINANCE.USDT_SWAP.COIN%u-USDT|GATEIO.USDT_SWAP.COIN%u-USDT", i, i);
        vc[i].__bufsize = topiclen;
        vc[i].activeDepth1DBWID = i;
        vc[i].passiveDepth1DBWID = i;
    }

    // 与生产完全同参：count=P, size=P*topiclen, topicdefaultsize=500,
    // datadefaultsize=500*100=50000
    {
        sp::Initialer<dbp::DbpTopic, dbp::DbpData> init(
            mpath, P, dpath, P * topiclen, &vc, 1, colsize, colsize * topiclen);
    }

    sp::Writer<dbp::DbpTopic, dbp::DbpData> w(mpath, dpath, colsize);
    sp::Reader<dbp::DbpTopic, dbp::DbpData> r(mpath, dpath, colsize);

    std::printf("\n=== 段大小（P=%u 对子, topiclen=%u）===\n", P, topiclen);
    std::printf("  mpath 映射 = %u 列 × %zu B = %.1f KB\n", colsize, sizeof(dbp::DbpTopic),
                colsize * sizeof(dbp::DbpTopic) / 1024.0);
    std::printf("  dpath 映射 = %u 行 × %zu B = %.1f MB（稀疏，实际只用 %u 行）\n",
                colsize * topiclen, sizeof(dbp::DbpData),
                colsize * topiclen * sizeof(dbp::DbpData) / 1048576.0, P * topiclen);
    std::printf("  实际占用   = %u 对 × %u 槽 × %zu B = %.1f KB\n",
                P, topiclen, sizeof(dbp::DbpData),
                P * topiclen * sizeof(dbp::DbpData) / 1024.0);

    dbp::DbpData src{};
    src.spreadBidAsk = 0.000123;
    src.generateTs = 1700000000000000LL;
    src.activeAskPrice[0] = 100.1;

    const long N = 300000;

    // ------------------------------------------------- A. 写一条（生产写路径）
    auto t0 = clk::now();
    for (long i = 0; i < N; ++i) {
        dbp::DbpData* p = w.Fetch(0);
        std::memcpy(p, &src, sizeof(dbp::DbpData));
        w.Commit(0);
    }
    auto t1 = clk::now();
    std::printf("\n=== 写侧 ===\n");
    std::printf("  Fetch+memcpy+Commit            %7.1f ns/条\n", nsPer(t0, t1, N));

    // ------------------------------------------------- B. 读：没有新数据
    {
        dbp::DbpData* p = nullptr;
        r.FetchLast(0, &p);                       // 追上
        auto a = clk::now();
        for (long i = 0; i < N; ++i) {
            g_sink = p;
            r.FetchLast(0, &p);
        }
        auto b = clk::now();
        std::printf("\n=== 读侧（sp::Reader::FetchLast）===\n");
        std::printf("  单列 miss（无新数据）           %7.1f ns/次\n", nsPer(a, b, N));

        // 多列 miss —— 这就是 heavy_work 每轮真正付的钱
        for (uint32_t i = 0; i < P; ++i) r.FetchLast(i, &p);
        auto c = clk::now();
        for (long i = 0; i < N / 10; ++i) {
            for (uint32_t k = 0; k < P; ++k) r.FetchLast(k, &p);
        }
        auto d = clk::now();
        std::printf("  %u 列一轮 miss（空转一轮）      %7.1f ns/轮  (%.1f ns/列)\n",
                    P, nsPer(c, d, N / 10), nsPer(c, d, N / 10) / P);
    }

    // ------------------------------------------------- C. 读：有新数据
    {
        dbp::DbpData* p = nullptr;
        auto a = clk::now();
        for (long i = 0; i < N; ++i) {
            dbp::DbpData* q = w.Fetch(0);
            std::memcpy(q, &src, sizeof(dbp::DbpData));
            w.Commit(0);
            r.FetchLast(0, &p);
            g_sink = p;
        }
        auto b = clk::now();
        std::printf("  写+读一条（命中）              %7.1f ns/轮\n", nsPer(a, b, N));
    }

    // ------------------------------------------------- D. SPMC 写/读
    {
        constexpr uint32_t CNT = 1024;
        // macOS 的 shm_open/ftruncate 对 640 KB 的对象报 EINVAL，所以这里用匿名内存。
        // 被测量的东西（acquire load / release store / memcpy 600 B）与后备存储无关 ——
        // 两边都是 page-fault-in 之后的普通内存访问，差异只在第一次触页。
        auto* q = static_cast<SPMCQueue<dbp::DbpData, CNT>*>(
            std::calloc(1, sizeof(SPMCQueue<dbp::DbpData, CNT>)));
        if (!q) { std::printf("\nSPMC 分配失败，跳过\n"); return 1; }

        std::printf("\n=== 读侧（SPMCQueue<DbpData,%u>）===\n", CNT);
        std::printf("  单对子段大小                   %7.1f KB\n",
                    sizeof(SPMCQueue<dbp::DbpData, CNT>) / 1024.0);

        auto a = clk::now();
        for (long i = 0; i < N; ++i) q->write(src);
        auto b = clk::now();
        std::printf("  write（memcpy+release store）  %7.1f ns/条\n", nsPer(a, b, N));

        auto rd = q->getReader();
        auto c = clk::now();
        for (long i = 0; i < N; ++i) { g_sink = rd.read(); }
        auto d = clk::now();
        std::printf("  read miss（acquire load）      %7.1f ns/次\n", nsPer(c, d, N));

        // P 个独立队列，一轮空转
        std::vector<SPMCQueue<dbp::DbpData, CNT>::Reader> rs;
        for (uint32_t i = 0; i < P; ++i) rs.push_back(q->getReader());
        auto e = clk::now();
        for (long i = 0; i < N / 10; ++i) {
            for (uint32_t k = 0; k < P; ++k) g_sink = rs[k].read();
        }
        auto f = clk::now();
        std::printf("  %u 个队列一轮 miss              %7.1f ns/轮  (%.1f ns/列)\n",
                    P, nsPer(e, f, N / 10), nsPer(e, f, N / 10) / P);
    }

    // ------------------------------------------------- E. 消费侧的真实开销
    {
        // 这就是 on_dbpdata → OnSpread 每 tick 干的第一件事：
        //   std::string pairKey(topic->__name);   // __name 是 char[128]，必走堆
        //   pim.GetPairInfo(pairKey);             // unordered_map<string, PairInfo*>
        std::unordered_map<std::string, int> pim;
        for (uint32_t i = 0; i < P; ++i) pim[std::string(vc[i].__name)] = (int)i;

        const dbp::DbpTopic* topic = &vc[0];
        long acc = 0;
        auto a = clk::now();
        for (long i = 0; i < N; ++i) {
            std::string pairKey(topic->__name);
            acc += pim.find(pairKey)->second;
        }
        auto b = clk::now();
        g_sink = (void*)(intptr_t)acc;
        std::printf("\n=== 消费侧（每 tick、每对子、每上下文各一次）===\n");
        std::printf("  std::string(__name) + map::find  %7.1f ns/次\n", nsPer(a, b, N));
        std::printf("  → on_dbpdata 里调了 2 个上下文    %7.1f ns/tick\n", 2 * nsPer(a, b, N));
    }

    // ------------------------------------------------- F. 真实调用点：DbpReader::FetchLast()
    // 这才是 heavy_work 每轮真正调的那个函数（base_strategy.h:133）：
    // 遍历 submap（unordered_map<uint32_t,uint32_t>）+ 逐列 sp::Reader::FetchLast。
    {
        dbp::DbpReader dr(mpath, dpath);
        int cbCount = 0;
        dr.SetCallback([&](dbp::DbpTopic*, dbp::DbpData*, uint32_t) { ++cbCount; });
        for (uint32_t i = 0; i < P; ++i) dr.Subscribe(vc[i].__name);

        auto a = clk::now();
        for (long i = 0; i < N / 10; ++i) dr.FetchLast();
        auto b = clk::now();
        std::printf("\n=== 真实调用点 DbpReader::FetchLast()（%u 对子订阅）===\n", P);
        std::printf("  一轮空转（无新数据）            %7.1f ns/轮\n", nsPer(a, b, N / 10));
        g_sink = (void*)(intptr_t)cbCount;
    }

    std::printf("\n");
    return 0;
}
