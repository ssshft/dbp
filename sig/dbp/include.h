#pragma once
#include <shmpool/include.h>
#include <data_struct.h>

namespace dbp {
    const uint32_t DBP_COL_SIZE = 500;

    //价比1，价比2，价差1，价差2
    enum SpreadCalcType {
        SPCT_PRICEDIV1,
        SPCT_PRICEDIV2,
        SPCT_PRICESUB1,
        SPCT_PRICESUB2
    };

    enum SpreadType {
        SPT_BID1ASK1,
        SPT_BID1BID1,
        SPT_ASK1ASK1,
        SPT_ASK1BID1
    };    

    enum SpreadDrive {
        SPD_ALL,
        SPD_LEFT,
        SPD_RIGHT
    };

    struct DbpTopic : sp::CNB {
        md::InstrumentInfo activeII;
        md::InstrumentInfo passiveII;

        int activeDepth1DBWID{-1};
        int activeFundingRateDBWID{-1};
        int activeTradesDBWID{-1};

        int passiveDepth1DBWID{-1};
        int passiveFundingRateDBWID{-1};
        int passiveTradesDBWID{-1};

        SpreadDrive spreadDrive;  // 价差驱动
        SpreadType spreadType; // 价差类型
        SpreadCalcType spreadCalcType; // 价差类型
      
        double activeMultiply{1};
        double passiveMultiply{1};
        uint64_t activeCheckspan{30};
        uint64_t passivecCheckspan{30};

        bool skipdata;
        uint32_t stematime;
        uint32_t tematime;
        uint32_t maxmintime;
        uint32_t timsspan;
    };

    struct DbpData : sp::RNB {
        bool spreadEffective; // 价差是否有效
        bool statEffective;   // 统计量是否有效

        double spreadBidAsk{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBid{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBid{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAsk{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskTema{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidTema{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidTema{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskTema{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskMax{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidMax{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidMax{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskMax{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskMin{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidMin{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidMin{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskMin{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double activeAskPrice[5];
        double activeBidPrice[5];
        double activeAskVolume[5];
        double activeBidVolume[5];

        double passiveAskPrice[5];
        double passiveBidPrice[5];
        double passiveAskVolume[5];
        double passiveBidVolume[5];

        double activeFundingRate{0.0};
        double passiveFundingRate{0.0};
        double activeNextFundingRate{0.0};
        double passiveNextFundingRate{0.0};
        //double activeMultiply;  // 主动腿调整系数(适用于1000shib与shib的情况)
        //double passiveMultiply;  //  被动腿调整系数
        double activePriceTema{0.0};  // 主动腿Tema价格
        double passivePriceTema{0.0};  // 被动腿Tema价格

        uint64_t activeFundingTs{0};  // 主动腿funding收取时间
        uint64_t passiveFundingTs{0};  // 被动退funding收取时间
        int64_t activeDepthTs{0};  // 主动腿depth时间
        int64_t passiveDepthTs{0};  // 被动退depth时间
        int64_t diffTs{0};  // 主动退被动腿时间差
        int64_t generateTs{0};  // 价差生成时间

        int64_t activeDepthDelay{0};
        int64_t passiveDepthDelay{0};

        int64_t exchActiveTradeDelay{0};
        int64_t exchPassiveTradeDelay{0};
    };

    struct DbpConfig {
        bool skipdata;
        uint32_t stematime;
        uint32_t tematime;
        uint32_t maxmintime;
        uint32_t timsspan;
        uint32_t checkspan;
        md::MarketType marketype;
        SpreadType spreadType;
        SpreadDrive spreadDrive;
    };

}