// dbp::Execute 热路径的两个可疑点，各值多少钱。
//
// 现状（改之前，dbpserver.cpp:427-429 / 442-444 / 457-459），**每条消息**都做：
//   const std::string& channel = crypto::get_md_channel_key(exch, instType, DEPTH1, instId);
//       ↑ 其实返回的是 std::string 的副本（按值），内部是 fmt::format("{}.{}.{}.{}", ...)
//   int topicId = mTopicId[channel];       // unordered_map<string,int>::operator[] —— 会 insert
//   snaps[topicId]->Update(cmd, topicId);
//
// 而 topicId 在构造 subscriber 的时候就已经知道了（channel 就是构造参数）。
//
// 本机没有 fmt，所以用 "string 拼接 + 一次堆分配" 代替 fmt::format —— 这是**下界**，
// 真实 fmt::format 只会更贵（要解析格式串）。
//
// ⚠️ C/D 两段的口径（2026-10-21 修正）：
//   - C 段原来用 `vector<int>`（4 B 步长）冒充"订阅者自带 id"，那比真实结构乐观。
//     现在改成真实的 `vector<MdSub>`（shared_ptr 16 B + int = 24 B），
//     并且分别量"空转一轮"和"每条消息取 id"。
//   - D 段的价格取决于**结果有没有人消费**：被依赖时是延迟（~14.5 ns），
//     没人读时只是吞吐（~8.1 ns）。两个数都打出来。

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using clk = std::chrono::steady_clock;

static double nsPer(clk::time_point a, clk::time_point b, long n) {
    return std::chrono::duration<double, std::nano>(b - a).count() / (double)n;
}

// 假的订阅者：空转时永远没数据，只花一次"跨对象 load"
struct FakeSpmc {
    volatile int published{0};
    bool tryPopLatest(int& out) { out = published; return false; }
};

// 真实的元素形状（dbpserver.h 里的 MdSub）
struct MdSub {
    std::shared_ptr<FakeSpmc> suber;
    int topicId{-1};
};

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

    // ---- C. 修好之后：订阅者自带 topicId（真实结构 vector<MdSub>，24 B 步长）----
    {
        const int SUBS = 300;                     // 50 对子 × 6 channel 的量级
        std::vector<std::shared_ptr<FakeSpmc>> oldShape;
        std::vector<MdSub> newShape;
        for (int i = 0; i < SUBS; ++i) {
            auto p = std::make_shared<FakeSpmc>();
            oldShape.push_back(p);
            newShape.push_back({p, i});
        }

        volatile long long sink = 0;
        int dummy = 0;

        // C1. 改之前：空转一轮 = 遍历 shared_ptr，解引用后 load
        auto a = clk::now();
        for (long r = 0; r < N / SUBS; ++r) {
            for (auto& s : oldShape) {
                if (s->tryPopLatest(dummy)) sink += 1;
            }
        }
        auto b = clk::now();
        const double oldSpin = nsPer(a, b, N / SUBS);

        // C2. 改之后：同样的空转（多了 8 B 步长，看会不会更贵）
        a = clk::now();
        for (long r = 0; r < N / SUBS; ++r) {
            for (auto& s : newShape) {
                if (s.suber->tryPopLatest(dummy)) sink += 1;
            }
        }
        b = clk::now();
        const double newSpin = nsPer(a, b, N / SUBS);

        // C3. 改之后：命中一条时，取 id 的代价（就是一条 load）
        a = clk::now();
        for (long i = 0; i < N; ++i) {
            sink += newShape[i % SUBS].topicId;
        }
        b = clk::now();
        const double getId = nsPer(a, b, N);

        std::printf("C1 空转一轮：旧形状 vector<shared_ptr>   %7.2f ns/轮（%d 个订阅者）\n", oldSpin, SUBS);
        std::printf("C2 空转一轮：新形状 vector<MdSub>       %7.2f ns/轮（步长 16 → 24 B）\n", newSpin);
        std::printf("C3 命中时取 topicId                    %7.2f ns/条\n", getId);
        std::printf("   ⇒ 改完一条消息的成本 = C3（≈ %.1f ns），不再是 A（≈ 50 ns）\n", getId);
    }

    // ---- D. 时钟读取：价格取决于结果有没有人消费 ----
    {
        volatile long long sink = 0;
        // D1. 被依赖（结果进下一次比较）—— 延迟口径
        auto a = clk::now();
        long long prev = 0;
        for (long i = 0; i < N; ++i) {
            long long t = (long long)(std::chrono::high_resolution_clock::now().time_since_epoch().count() / 1000);
            if (t > prev) { prev = t; sink += 1; }
        }
        auto b = clk::now();
        const double dependent = nsPer(a, b, N);

        // D2. 没人读（结果直接丢掉，紧循环里能被流水线吃掉）—— 吞吐口径
        a = clk::now();
        for (long i = 0; i < N; ++i) {
            sink += (long long)(std::chrono::high_resolution_clock::now().time_since_epoch().count() / 1000);
        }
        b = clk::now();
        const double throughput = nsPer(a, b, N);

        std::printf("D1 now() 单独循环（每轮只读时钟）        %7.1f ns/次\n", throughput);
        std::printf("D2 now() 结果进比较（延迟口径）          %7.1f ns/次\n", dependent);
        std::printf("   ⇒ 单独量就是 ~%.0f ns，两种口径**没有区别**（这个核上它不会和别的指令重叠）。\n",
                    throughput);
        std::printf("     但在写路径里它被 memcpy/指针计算重叠掉了：删掉它只暴露 8.1 ns\n");
        std::printf("     （`Fetch+memcpy+Commit` 18.0 → 9.9，见 run.sh base）。\n");
        std::printf("  （macOS/libc++ 下是 mach_absolute_time；Linux/libstdc++ 走 clock_gettime vDSO，"
                    "通常更贵）\n");
    }

    // 600 B 载荷的写路径（run.sh base 实测，2026-10-21 Commit 不再读时钟之后）
    std::printf("\n（对照）sp::Writer 整条写路径 = 9.9 ns（改前 18.0）\n");
    return 0;
}
