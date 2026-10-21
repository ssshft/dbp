// dbp::Execute 热路径的两个可疑点，各值多少钱。
//
// 现状（dbpserver.cpp:427-429 / 442-444 / 457-459），**每条消息**都做：
//   const std::string& channel = crypto::get_md_channel_key(exch, instType, DEPTH1, instId);
//       ↑ 其实返回的是 std::string 的副本（按值），内部是 fmt::format("{}.{}.{}.{}", ...)
//   int topicId = mTopicId[channel];       // unordered_map<string,int>::operator[] —— 会 insert
//   snaps[topicId]->Update(cmd, topicId);
//
// 而 topicId 在构造 subscriber 的时候就已经知道了（channel 就是构造参数）。
//
// 本机没有 fmt，所以用 "string 拼接 + 一次堆分配" 代替 fmt::format —— 这是**下界**，
// 真实 fmt::format 只会更贵（要解析格式串）。

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using clk = std::chrono::steady_clock;

static double nsPer(clk::time_point a, clk::time_point b, long n) {
    return std::chrono::duration<double, std::nano>(b - a).count() / (double)n;
}

int main() {
    // 生产上的真实形状：exchange.instType.instId.marketType
    const char* exchId   = "BINANCE";
    const char* instType = "USDT_SWAP";
    const char* instId   = "BTC-USDT";
    const char* mktType  = "DEPTH1";

    std::unordered_map<std::string, int> mTopicId;
    // 模拟 500 个 channel
    for (int i = 0; i < 500; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "EX%d.USDT_SWAP.COIN%d-USDT.DEPTH1", i % 10, i);
        mTopicId[buf] = i;
    }
    mTopicId["BINANCE.USDT_SWAP.BTC-USDT.DEPTH1"] = 7;

    const long N = 1000000;

    // ---- A. 现状：拼 key + operator[] ----
    {
        volatile int sink = 0;
        auto a = clk::now();
        for (long i = 0; i < N; ++i) {
            const std::string key = std::string(exchId) + "." + instType + "." + instId + "." + mktType;
            int topicId = mTopicId[key];          // operator[]：miss 就 insert
            sink += topicId;
        }
        auto b = clk::now();
        std::printf("A 现状：拼 key + mTopicId[key]        %7.1f ns/条\n", nsPer(a, b, N));
        std::printf("  （key 长度 %zu，operator[] 会 insert，map 已涨到 %zu 项）\n",
                    std::strlen(exchId) + std::strlen(instType) + std::strlen(instId)
                        + std::strlen(mktType) + 3,
                    mTopicId.size());
    }

    // ---- B. 只查不拼：find（用已有的 key 对象）----
    {
        const std::string key = std::string(exchId) + "." + instType + "." + instId + "." + mktType;
        volatile int sink = 0;
        auto a = clk::now();
        for (long i = 0; i < N; ++i) {
            auto it = mTopicId.find(key);
            sink += (it != mTopicId.end()) ? it->second : -1;
        }
        auto b = clk::now();
        std::printf("B 只 find（key 已存在，不拼）         %7.1f ns/条\n", nsPer(a, b, N));
    }

    // ---- C. 修好：subscriber 里存 topicId ----
    {
        std::vector<int> idOfSub;                 // 每个 subscriber 一个 id
        for (int i = 0; i < 500; ++i) idOfSub.push_back(i);
        volatile int sink = 0;
        auto a = clk::now();
        for (long i = 0; i < N; ++i) {
            sink += idOfSub[i % idOfSub.size()];
        }
        auto b = clk::now();
        std::printf("C 修好：subscriber 自带 topicId       %7.1f ns/条\n", nsPer(a, b, N));
    }

    // ---- D. 时钟读取（dbp 每条消息读 3 次）----
    {
        volatile long long sink = 0;
        auto a = clk::now();
        for (long i = 0; i < N; ++i) {
            sink += (long long)(std::chrono::high_resolution_clock::now().time_since_epoch().count() / 1000);
        }
        auto b = clk::now();
        const double one = nsPer(a, b, N);
        std::printf("D high_resolution_clock::now()        %7.1f ns/次   ×3 = %.1f ns/条\n",
                    one, one * 3);
        std::printf("  （macOS/libc++ 下是 mach_absolute_time；Linux/libstdc++ 走 clock_gettime vDSO，"
                    "通常更贵）\n");
    }

    std::printf("\n（对照）sp::Writer 整条写路径 = 18.0 ns\n");
    return 0;
}
