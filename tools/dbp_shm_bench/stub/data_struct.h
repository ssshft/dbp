// 桩：只为了让 dbp/include.h 能编过。
//
// dbp::DbpTopic / dbp::DbpData **一个字段都不用到** md:: 里的东西 —— 真正需要
// data_struct.h 的只有 DbpConfig（它有一个 md::MarketType 成员）。所以这里只补
// 一个最小的 md::MarketType 就够了，其余（rapidjson/fmt 那一大坨）一概不引入。
//
// 关键：DbpTopic / DbpData 的**结构体定义是从 dbp/sig/dbp/include.h 原样编进去的**，
// 没有被桩改过一行 ⇒ 下面 sizeof 打出来的数字就是生产上的数字。
#pragma once
#include <cstdint>

namespace md {
    enum MarketType {
        DEPTH1 = 1,
        TRADES = 2,
        FUNDING_RATE = 3,
        MarketType_MIN = DEPTH1,
        MarketType_MAX = FUNDING_RATE
    };
}
