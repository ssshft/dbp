// 如果 DbpData 把 8 个 [5] 数组改成 [1]，两条路各能省多少？
//
// 依据：全仓库（dbp + utrade_hft + tests）对 activeAskPrice / activeBidPrice /
// activeAskVolume / activeBidVolume / passive* 的**所有**下标访问都是 [0]，
// dbp 侧也只写 [0]（dbsnap.h:252-255 / 271-274）。所以 [1..4] 从来没被用过。
//
// 8 个数组 × 5 档 × 8 B = 320 B，其中 64 B 有用 ⇒ 256 B 是死的。
// 这里把 DbpData 原样抄一份、只把 [5] 改成 [1]，量 sizeof 和写读开销。

#include <shmpool/shmpool.h>
#include <dbp/include.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using clk = std::chrono::steady_clock;

static double nsPer(clk::time_point a, clk::time_point b, long n) {
    return std::chrono::duration<double, std::nano>(b - a).count() / (double)n;
}

// 逐字照抄 dbp::DbpData，只把 8 个数组的 [5] 改成 [1]。
struct DbpDataSlim : sp::RNB {
    bool spreadEffective;
    bool statEffective;

    double spreadBidAsk{0.0};
    double spreadBidBid{0.0};
    double spreadAskBid{0.0};
    double spreadAskAsk{0.0};

    double spreadBidAskTema{0.0};
    double spreadBidBidTema{0.0};
    double spreadAskBidTema{0.0};
    double spreadAskAskTema{0.0};

    double spreadBidAskMax{0.0};
    double spreadBidBidMax{0.0};
    double spreadAskBidMax{0.0};
    double spreadAskAskMax{0.0};

    double spreadBidAskMin{0.0};
    double spreadBidBidMin{0.0};
    double spreadAskBidMin{0.0};
    double spreadAskAskMin{0.0};

    double activeAskPrice[1];
    double activeBidPrice[1];
    double activeAskVolume[1];
    double activeBidVolume[1];

    double passiveAskPrice[1];
    double passiveBidPrice[1];
    double passiveAskVolume[1];
    double passiveBidVolume[1];

    double activeFundingRate{0.0};
    double passiveFundingRate{0.0};
    double activeNextFundingRate{0.0};
    double passiveNextFundingRate{0.0};
    double activePriceTema{0.0};
    double passivePriceTema{0.0};

    uint64_t activeFundingTs{0};
    uint64_t passiveFundingTs{0};
    int64_t activeDepthTs{0};
    int64_t passiveDepthTs{0};
    int64_t diffTs{0};
    int64_t generateTs{0};

    int64_t activeDepthDelay{0};
    int64_t passiveDepthDelay{0};

    int64_t exchActiveTradeDelay{0};
    int64_t exchPassiveTradeDelay{0};
};

int main() {
    const uint32_t topiclen = 100;
    const uint32_t colsize  = 500;
    const uint32_t P        = 8;

    std::printf("sizeof(DbpData)     = %zu\n", sizeof(dbp::DbpData));
    std::printf("sizeof(DbpDataSlim) = %zu   （省 %zu B，%.0f%%）\n\n",
                sizeof(DbpDataSlim),
                sizeof(dbp::DbpData) - sizeof(DbpDataSlim),
                100.0 * (sizeof(dbp::DbpData) - sizeof(DbpDataSlim)) / sizeof(dbp::DbpData));

    const char* mpath = "/tmp/dbpbench/shm/SLIM_TOPIC";
    const char* dpath = "/tmp/dbpbench/shm/SLIM_DATAS";
    std::remove(mpath);
    std::remove(dpath);

    std::vector<dbp::DbpTopic> vc(P);
    for (uint32_t i = 0; i < P; ++i) {
        std::snprintf(vc[i].__name, sizeof(vc[i].__name), "P%u", i);
        vc[i].__bufsize = topiclen;
    }
    {
        sp::Initialer<dbp::DbpTopic, DbpDataSlim> init(
            mpath, P, dpath, P * topiclen, &vc, 1, colsize, colsize * topiclen);
    }

    sp::Writer<dbp::DbpTopic, DbpDataSlim> w(mpath, dpath, colsize);
    sp::Reader<dbp::DbpTopic, DbpDataSlim> r(mpath, dpath, colsize);

    DbpDataSlim src{};
    src.spreadBidAsk = 0.000123;
    src.generateTs = 1700000000000000LL;
    src.activeAskPrice[0] = 100.1;

    const long N = 300000;

    auto t0 = clk::now();
    for (long i = 0; i < N; ++i) {
        DbpDataSlim* p = w.Fetch(0);
        std::memcpy(p, &src, sizeof(DbpDataSlim));
        w.Commit(0);
    }
    auto t1 = clk::now();

    DbpDataSlim* out = nullptr;
    r.FetchLast(0, &out);
    auto t2 = clk::now();
    for (long i = 0; i < N; ++i) r.FetchLast(0, &out);
    auto t3 = clk::now();

    std::printf("sp::Writer/Reader，载荷 %zu B：\n", sizeof(DbpDataSlim));
    std::printf("  写一条（Fetch+memcpy+Commit）  %7.1f ns/条\n", nsPer(t0, t1, N));
    std::printf("  读 miss 单列                    %7.1f ns/次\n", nsPer(t2, t3, N));
    std::printf("  每对子环占用                    %7.1f KB（现 60.0 KB）\n",
                P ? (double)(topiclen * sizeof(DbpDataSlim)) / 1024.0 : 0.0);

    // 不要在这里硬编码 600 B 的对照数字 —— 它随 shmpool.h 的改动而变
    // （2026-10-20 Commit 不再读时钟后，600 B 写从 18.0 掉到 9.9 ns）。
    // 要对照就同一天跑一次 ./run.sh base。
    std::printf("\n（对照：请同一天跑 ./run.sh base 取 600 B 的写/读数字）\n");
    return 0;
}
