// 验证 dbpserver.cpp 热加对子路径的**不变式**：
//
//   snaps.size() == snapscount，且 mTopicId 里每个 id < snapscount
//   ⇒ Execute() 里 snaps[sub.topicId] 永远有效
//
// 这条不变式现在是硬需求：Execute 不再拼 key 查表，直接拿订阅者自带的 topicId
// 索引 snaps，没有"查不到就退化成 0"的缓冲了。
//
// 同一份场景跑两套逻辑：
//   old = 改之前（先无条件 snapscount += 1，再在三个分支里决定推不推 snaps）
//   new = 改之后（先判断要不要建快照，再决定占不占 topicId；失败回滚）
//
// 代码是从 dbpserver.cpp 里**原样**抄过来的（只把成员换成本文件的字段）。
//
// ⚠️ **这个文件会漂移**：它抄了 dbpserver.cpp 的逻辑，改了那边就要同步改这里。
// 它守的是"不变式 + 边界场景"，不是逐行等价；真正的编译闸门仍然是部署机的
// `cd dbp && make -j`。
//
// 用法：./run.sh hotadd
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------- 最小桩
namespace md {
    enum MarketType { DEPTH1 = 1, DEPTH5, DEPTH10, DEPTH20, TRADES, FUNDING_RATE, SPOT };
    static std::unordered_map<md::MarketType, std::string> MarketTypeEnum2StrMap = {
        {DEPTH1, "DEPTH1"}, {TRADES, "TRADES"}, {FUNDING_RATE, "FUNDING_RATE"}
    };
}

// 让指定 channel 的构造抛异常，用来测"失败要回滚"
static std::string g_throwOnChannel;

struct FakeSpmc {
    explicit FakeSpmc(const std::string& ch) {
        if (!g_throwOnChannel.empty() && ch.find(g_throwOnChannel) != std::string::npos) {
            throw std::runtime_error("boom: " + ch);
        }
    }
};
using Depth1Spmc      = FakeSpmc;
using TradesSpmc      = FakeSpmc;
using FundingRateSpmc = FakeSpmc;

struct DbSnap {};

struct DbpTopic {
    int activeDepth1DBWID{-1},  activeFundingRateDBWID{-1},  activeTradesDBWID{-1};
    int passiveDepth1DBWID{-1}, passiveFundingRateDBWID{-1}, passiveTradesDBWID{-1};
};

template <typename SpmcT>
struct MdSub {
    std::shared_ptr<SpmcT> suber;
    int topicId{-1};
};

#define LOG_ERROR(...) std::printf("    LOG_ERROR: " __VA_ARGS__)

struct Server {
    std::vector<DbSnap*> snaps;
    uint32_t snapscount{0};
    std::unordered_map<std::string, int> mTopicId;
    std::vector<md::MarketType> vMarketType;
    std::vector<MdSub<Depth1Spmc>>      suberDepth;
    std::vector<MdSub<TradesSpmc>>      suberTrades;
    std::vector<MdSub<FundingRateSpmc>> suberFunding;

    // ============================================================
    //  ↓↓↓ 以下两块是 dbpserver.cpp 里的逻辑，逐字抄（new）/ 复刻（old）
    // ============================================================

    // 热加之后 mb / updated 长什么样，用来验证"ensureChannel 返回 -1 不用单独判断"
    DbpTopic lastMb;
    bool     lastUpdated{false};

    void hotAddNew(const std::vector<std::string>& result,
                   const std::vector<std::string>& res1,
                   const std::vector<std::string>& res2) {
        DbpTopic mb;
        bool updated = true;

        auto ensureChannel = [&](const std::string& channel, md::MarketType mt, bool isSpotLeg) -> int {
            auto it = mTopicId.find(channel);
            if (it != mTopicId.end()) {
                return it->second;
            }

            const bool wantSnap = (mt == md::DEPTH1)
                               || (mt == md::FUNDING_RATE && !isSpotLeg)
                               || (mt == md::TRADES);
            if (!wantSnap) {
                return -1;
            }

            const int topicId = static_cast<int>(snapscount);
            bool ok = false;
            try {
                if (mt == md::DEPTH1) {
                    suberDepth.push_back({std::make_shared<Depth1Spmc>(channel), topicId});
                    ok = true;
                }
                else if (mt == md::FUNDING_RATE) {
                    suberFunding.push_back({std::make_shared<FundingRateSpmc>(channel), topicId});
                    ok = true;
                }
                else if (mt == md::TRADES) {
                    suberTrades.push_back({std::make_shared<TradesSpmc>(channel), topicId});
                    ok = true;
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR("%s channel: %s\n", e.what(), channel.c_str());
            }

            if (!ok) {
                updated = false;
                return -1;
            }

            snaps.push_back(new DbSnap());
            snapscount = static_cast<uint32_t>(snaps.size());
            mTopicId[channel] = topicId;
            return topicId;
        };

        const bool activeSpot  = (res1[1] == "SPOT");
        const bool passiveSpot = (res2[1] == "SPOT");

        for (auto t = 0; t < (int)vMarketType.size(); ++t) {
            const md::MarketType mt = vMarketType[t];
            const std::string marketTypeStr = md::MarketTypeEnum2StrMap[mt];

            const int activeId = ensureChannel(result[0] + "." + marketTypeStr, mt, activeSpot);
            if (mt == md::DEPTH1) {
                mb.activeDepth1DBWID = activeId;
            }
            else if (mt == md::FUNDING_RATE && !activeSpot) {
                mb.activeFundingRateDBWID = activeId;
            }
            else if (mt == md::TRADES) {
                mb.activeTradesDBWID = activeId;
            }

            const int passiveId = ensureChannel(result[1] + "." + marketTypeStr, mt, passiveSpot);
            if (mt == md::DEPTH1) {
                mb.passiveDepth1DBWID = passiveId;
            }
            else if (mt == md::FUNDING_RATE && !passiveSpot) {
                mb.passiveFundingRateDBWID = passiveId;
            }
            else if (mt == md::TRADES) {
                mb.passiveTradesDBWID = passiveId;
            }
        }
        // 生产里这里是 `if (updated) { ...; writer->AddNewColumn(mb); }`
        lastUpdated = updated;
        lastMb = mb;
    }

    // 改之前的逻辑（复刻 §1.2）：先无条件占 id，再在分支里决定推不推 snaps
    void hotAddOld(const std::vector<std::string>& result,
                   const std::vector<std::string>& res1,
                   const std::vector<std::string>& res2) {
        DbpTopic mb;
        bool updated = true;
        for (auto t = 0; t < (int)vMarketType.size(); ++t) {
            std::string activeChannel = result[0] + "." + md::MarketTypeEnum2StrMap[vMarketType[t]];
            auto itActive = mTopicId.find(activeChannel);
            if (itActive == mTopicId.end()) {
                int topicId = snapscount;
                mTopicId[activeChannel] = topicId;
                snapscount += 1;

                if (vMarketType[t] == md::DEPTH1) {
                    suberDepth.push_back({std::make_shared<Depth1Spmc>(activeChannel), topicId});
                    snaps.push_back(new DbSnap());
                }
                else if (vMarketType[t] == md::FUNDING_RATE && res1[1] != "SPOT") {
                    suberFunding.push_back({std::make_shared<FundingRateSpmc>(activeChannel), topicId});
                    snaps.push_back(new DbSnap());
                }
                else if (vMarketType[t] == md::TRADES) {
                    suberTrades.push_back({std::make_shared<TradesSpmc>(activeChannel), topicId});
                    snaps.push_back(new DbSnap());
                }
            }
            if (vMarketType[t] == md::DEPTH1) {
                mb.activeDepth1DBWID = mTopicId[activeChannel];
            }
            else if (vMarketType[t] == md::FUNDING_RATE && res1[1] != "SPOT") {
                mb.activeFundingRateDBWID = mTopicId[activeChannel];
            }
            else if (vMarketType[t] == md::TRADES) {
                mb.activeTradesDBWID = mTopicId[activeChannel];
            }

            std::string passiveChannel = result[1] + "." + md::MarketTypeEnum2StrMap[vMarketType[t]];
            auto itPassive = mTopicId.find(passiveChannel);
            if (itPassive == mTopicId.end()) {
                int topicId = snapscount;
                mTopicId[passiveChannel] = topicId;
                snapscount += 1;

                if (vMarketType[t] == md::DEPTH1) {
                    suberDepth.push_back({std::make_shared<Depth1Spmc>(passiveChannel), topicId});
                    snaps.push_back(new DbSnap());
                }
                else if (vMarketType[t] == md::FUNDING_RATE && res2[1] != "SPOT") {
                    suberFunding.push_back({std::make_shared<FundingRateSpmc>(passiveChannel), topicId});
                    snaps.push_back(new DbSnap());
                }
                else if (vMarketType[t] == md::TRADES) {
                    suberTrades.push_back({std::make_shared<TradesSpmc>(passiveChannel), topicId});
                    snaps.push_back(new DbSnap());
                }
            }
            if (vMarketType[t] == md::DEPTH1) {
                mb.passiveDepth1DBWID = mTopicId[passiveChannel];
            }
            else if (vMarketType[t] == md::FUNDING_RATE && res2[1] != "SPOT") {
                mb.passiveFundingRateDBWID = mTopicId[passiveChannel];
            }
            else if (vMarketType[t] == md::TRADES) {
                mb.passiveTradesDBWID = mTopicId[passiveChannel];
            }
        }
        (void)updated;
    }

    // ============================================================
    //  校验
    // ============================================================

    // 返回违反不变式的 id（-1 = 全部合法）
    int firstBadId() const {
        for (const auto& kv : mTopicId) {
            if (kv.second < 0 || (size_t)kv.second >= snaps.size()) {
                return kv.second;
            }
        }
        // Execute 会用的所有 id 也必须 < snaps.size()
        auto chk = [&](int id) { return (id >= 0 && (size_t)id >= snaps.size()) ? id : -1; };
        for (const auto& s : suberDepth)   { if (chk(s.topicId) >= 0) return s.topicId; }
        for (const auto& s : suberTrades)  { if (chk(s.topicId) >= 0) return s.topicId; }
        for (const auto& s : suberFunding) { if (chk(s.topicId) >= 0) return s.topicId; }
        if (snapscount != snaps.size()) return (int)snapscount;
        return -1;
    }
};

static int g_fails = 0;

static void report(const char* name, const Server& s, bool expectOk) {
    int bad = s.firstBadId();
    bool ok = (bad < 0);
    bool pass = (ok == expectOk);
    std::printf("  [%s] %-34s snapscount=%-2u snaps=%-2zu ids=%-2zu  首个坏 id=%d\n",
                pass ? " OK " : "FAIL", name, s.snapscount, s.snaps.size(),
                s.mTopicId.size(), bad);
    if (!pass) ++g_fails;
}

static std::vector<std::string> split3(const std::string& s) {
    std::vector<std::string> v;
    size_t p = 0, q;
    while ((q = s.find('.', p)) != std::string::npos) { v.push_back(s.substr(p, q - p)); p = q + 1; }
    v.push_back(s.substr(p));
    return v;
}

int main() {
    const std::vector<md::MarketType> all3 = {md::DEPTH1, md::FUNDING_RATE, md::TRADES};

    // ---- 场景 1：普通对子（两腿都是 USDT_SWAP）
    {
        Server a, b;
        a.vMarketType = all3; b.vMarketType = all3;
        std::string s1 = "BINANCE.USDT_SWAP.BTC-USDT", s2 = "GATEIO.USDT_SWAP.BTC-USDT";
        auto r = {s1, s2};
        std::vector<std::string> result = {s1, s2};
        a.hotAddOld(result, split3(s1), split3(s2));
        b.hotAddNew(result, split3(s1), split3(s2));
        report("普通对子 old", a, true);
        report("普通对子 new", b, true);
    }

    // ---- 场景 2：主动腿是现货（没有资金费率）
    //      §1.2 的原型：SPOT 腿的 FUNDING_RATE 占了一个 id 却没有 snaps
    {
        Server a, b;
        a.vMarketType = all3; b.vMarketType = all3;
        std::string s1 = "BINANCE.SPOT.BTC-USDT", s2 = "GATEIO.USDT_SWAP.BTC-USDT";
        std::vector<std::string> result = {s1, s2};
        a.hotAddOld(result, split3(s1), split3(s2));
        b.hotAddNew(result, split3(s1), split3(s2));
        report("现货主动腿 old（应违反）", a, false);
        report("现货主动腿 new", b, true);
    }

    // ---- 场景 3：两腿都现货
    {
        Server a, b;
        a.vMarketType = all3; b.vMarketType = all3;
        std::string s1 = "BINANCE.SPOT.ETH-USDT", s2 = "OKX.SPOT.ETH-USDT";
        std::vector<std::string> result = {s1, s2};
        a.hotAddOld(result, split3(s1), split3(s2));
        b.hotAddNew(result, split3(s1), split3(s2));
        report("双腿现货 old（应违反）", a, false);
        report("双腿现货 new", b, true);
    }

    // ---- 场景 4：共享被动腿（同一个 channel 只能有一个 id）
    {
        Server a, b;
        a.vMarketType = all3; b.vMarketType = all3;
        std::string a1 = "BINANCE.USDT_SWAP.BTC-USDT", p1 = "GATEIO.USDT_SWAP.BTC-USDT";
        std::string a2 = "OKX.USDT_SWAP.BTC-USDT",     p2 = "GATEIO.USDT_SWAP.BTC-USDT";
        std::vector<std::string> r1 = {a1, p1}, r2 = {a2, p2};
        a.hotAddOld(r1, split3(a1), split3(p1)); a.hotAddOld(r2, split3(a2), split3(p2));
        b.hotAddNew(r1, split3(a1), split3(p1)); b.hotAddNew(r2, split3(a2), split3(p2));
        report("共享被动腿 old", a, true);
        report("共享被动腿 new", b, true);
        std::printf("       old/new ids = %zu / %zu（应相等且为 9）\n", a.mTopicId.size(), b.mTopicId.size());
        if (a.mTopicId.size() != b.mTopicId.size()) { std::printf("  [FAIL] id 数量不一致\n"); ++g_fails; }
    }

    // ---- 场景 5：构造抛异常 —— 只让**被动腿**的 DEPTH1 抛
    //      预期：抛的那条腿不占 id，另一条腿的 5 个 id 保留（部分成功是允许的），
    //            重试时把缺的那条补上 ⇒ 最终 6 个 id、6 个 snaps。
    //      ⚠️ 这正是改之前修不好的地方：原来先登记 mTopicId 再 try，失败后
    //      find 命中 ⇒ 那条腿永远建不出订阅者，而它的 DBWID 却指向一个没人喂的快照。
    {
        Server b;
        b.vMarketType = all3;
        std::string s1 = "BINANCE.USDT_SWAP.SOL-USDT", s2 = "GATEIO.USDT_SWAP.SOL-USDT";
        std::vector<std::string> result = {s1, s2};

        g_throwOnChannel = "GATEIO.USDT_SWAP.SOL-USDT.DEPTH1";
        b.hotAddNew(result, split3(s1), split3(s2));
        g_throwOnChannel.clear();

        std::printf("       失败后 mTopicId=%zu snapscount=%u snaps=%zu\n",
                    b.mTopicId.size(), b.snapscount, b.snaps.size());
        // 主动腿 3 个 + 被动腿 2 个（DEPTH1 被拒）= 5
        if (b.mTopicId.size() != 5 || b.snapscount != 5 || b.snaps.size() != 5) {
            std::printf("  [FAIL] 失败那条腿没有回滚干净\n"); ++g_fails;
        } else {
            std::printf("  [ OK ] 失败那条腿没占 id（其余 5 个保留）\n");
        }
        if (b.mTopicId.count("GATEIO.USDT_SWAP.SOL-USDT.DEPTH1")) {
            std::printf("  [FAIL] 失败的 channel 仍留在 mTopicId 里（重试会永久失败）\n"); ++g_fails;
        } else {
            std::printf("  [ OK ] 失败的 channel 没留在 mTopicId 里\n");
        }

        // 重试：应该补齐到 6
        b.hotAddNew(result, split3(s1), split3(s2));
        report("失败后重试 new", b, true);
        if (b.mTopicId.size() != 6) { std::printf("  [FAIL] 重试没补齐（%zu != 6）\n", b.mTopicId.size()); ++g_fails; }
    }

    // ---- 场景 6：真实配置的混合场景（1 个普通 + 1 个现货腿 + 1 个共享）
    {
        Server b;
        b.vMarketType = all3;
        struct P { const char* a; const char* p; };
        P pairs[] = {
            {"BINANCE.USDT_SWAP.BTC-USDT",  "GATEIO.USDT_SWAP.BTC-USDT"},
            {"BINANCE.SPOT.ETH-USDT",       "GATEIO.USDT_SWAP.ETH-USDT"},
            {"OKX.USDT_SWAP.BTC-USDT",      "GATEIO.USDT_SWAP.BTC-USDT"},
            {"HTX.SPOT.DOGE-USDT",          "MEXC.SPOT.DOGE-USDT"},
        };
        for (auto& p : pairs) {
            std::vector<std::string> r = {p.a, p.p};
            b.hotAddNew(r, split3(p.a), split3(p.p));
        }
        report("混合场景 new", b, true);
        std::printf("       ids=%zu snaps=%zu suberDepth=%zu suberTrades=%zu suberFunding=%zu\n",
                    b.mTopicId.size(), b.snaps.size(), b.suberDepth.size(),
                    b.suberTrades.size(), b.suberFunding.size());
        // 4 对子、每对 3 个 marketType：
        //   DEPTH1 : 4×2 = 8 个 channel，但 GATEIO BTC 被两对共享 ⇒ 7
        //   TRADES : 同上 ⇒ 7
        //   FUNDING_RATE : 现货腿跳过 ⇒ BINANCE BTC / GATEIO BTC / GATEIO ETH / OKX BTC = 4
        // ⇒ 18
        if (b.mTopicId.size() != 18) { std::printf("  [FAIL] channel 数应为 18\n"); ++g_fails; }
        if (b.suberFunding.size() != 4) { std::printf("  [FAIL] FUNDING_RATE 订阅者应为 4\n"); ++g_fails; }

        // 关键：任何 SPOT 腿都不该有 FUNDING_RATE 的 id / 订阅者
        int spotFunding = 0;
        for (const auto& kv : b.mTopicId) {
            if (kv.first.find(".SPOT.") != std::string::npos &&
                kv.first.find(".FUNDING_RATE") != std::string::npos) {
                ++spotFunding;
                std::printf("       多余的 SPOT 资金费率 channel: %s\n", kv.first.c_str());
            }
        }
        if (spotFunding) { std::printf("  [FAIL] 现货腿占了 %d 个资金费率 id\n", spotFunding); ++g_fails; }
        else std::printf("  [ OK ] 现货腿没有资金费率 id / 订阅者\n");
    }

    // ---- 场景 7：ensureChannel 返回 -1 到底要不要在调用方判断？
    //      结论是"不用"，但两种 -1 的**后果**不一样，这里把两者都钉住：
    //        ① 不需要快照（现货腿的 FUNDING_RATE）：分支条件挡住，连赋值都不发生，
    //           mb.activeFundingRateDBWID 保持默认 -1，updated 仍为 true（这对子照常加列）；
    //        ② 建订阅者失败：mb 里那条腿被写成 -1，但 updated=false ⇒ 生产里
    //           `if (updated)` 会跳过 AddNewColumn，整个 mb 被丢弃。
    //      ⇒ 只要 ② 的 updated 语义不被破坏，调用方就不需要单独判 -1。
    {
        // ① 现货主动腿：updated 必须是 true，资金费率那两个字段保持 -1
        Server b;
        b.vMarketType = all3;
        std::string s1 = "BINANCE.SPOT.ETH-USDT", s2 = "GATEIO.USDT_SWAP.ETH-USDT";
        std::vector<std::string> r = {s1, s2};
        b.hotAddNew(r, split3(s1), split3(s2));

        bool ok = (b.lastUpdated == true)
               && (b.lastMb.activeFundingRateDBWID == -1)
               && (b.lastMb.passiveFundingRateDBWID >= 0)
               && (b.lastMb.activeDepth1DBWID >= 0)
               && (b.lastMb.passiveTradesDBWID >= 0);
        std::printf("  [%s] ① 现货腿：updated=%d  activeFR=%d passiveFR=%d（应为 -1 / >=0）\n",
                    ok ? " OK " : "FAIL", (int)b.lastUpdated,
                    b.lastMb.activeFundingRateDBWID, b.lastMb.passiveFundingRateDBWID);
        if (!ok) ++g_fails;

        // ② 建失败：updated 必须是 false，失败那条腿写成 -1，其余腿仍然有效
        Server c;
        c.vMarketType = all3;
        std::string t1 = "BINANCE.USDT_SWAP.SOL-USDT", t2 = "GATEIO.USDT_SWAP.SOL-USDT";
        std::vector<std::string> r2 = {t1, t2};
        g_throwOnChannel = "GATEIO.USDT_SWAP.SOL-USDT.DEPTH1";
        c.hotAddNew(r2, split3(t1), split3(t2));
        g_throwOnChannel.clear();

        bool ok2 = (c.lastUpdated == false)
                && (c.lastMb.passiveDepth1DBWID == -1)      // 失败那条腿
                && (c.lastMb.activeDepth1DBWID >= 0)        // 另一条腿照旧
                && (c.lastMb.passiveTradesDBWID >= 0);
        std::printf("  [%s] ② 建失败：updated=%d  activeD1=%d passiveD1=%d（应为 false / >=0 / -1）\n",
                    ok2 ? " OK " : "FAIL", (int)c.lastUpdated,
                    c.lastMb.activeDepth1DBWID, c.lastMb.passiveDepth1DBWID);
        if (!ok2) ++g_fails;
    }

    std::printf("\n%s (fails=%d)\n", g_fails ? "有失败" : "全部通过", g_fails);
    return g_fails ? 1 : 0;
}
