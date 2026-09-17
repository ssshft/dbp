#pragma once
#include <cfloat>
#include <dbp/include.h>
#include <dbw/include.h>
#include <sig/shmpool/shmpool.h>

#define SPREAD_MAX  999999999
#define SPREAD_MIN  -999999999

namespace dbp{

    struct BestPxState {
        double ap1{0};
        double bp1{0};
        double av1{0};
        double bv1{0};
        int64_t depthUs{0};

        double tradesBuyPrice{0};
        double tradesSellPrice{0};
        double tradesBuyUs{0};
        double tradesSellUs{0};

        int64_t ts{0};
        int64_t exchActiveTradeDelay{0};
        int64_t exchPassiveTradeDelay{0};
    };

    
    class DbSnapListener
    {
    public:
        virtual void OnDBSnapUpdate(const dbw::DBdata& data) = 0;
        virtual void OnDBSnapUpdate(const md::CryptoMarketData& d, int topicId) = 0;
    };

    class DbSnap{
    public:
        DbSnap(dbw::DBTopic& t){
            memcpy(&topic,&t,sizeof(dbw::DBTopic));
            memset(&data,0,sizeof(dbw::DBdata));
        }

        DbSnap() {
            memset(&data,0,sizeof(dbw::DBdata));
        }

        virtual ~DbSnap(){}

        dbw::DBdata& Data(){return data;}

        void Update(dbw::DBdata& d){
            // if (d.d.header.marketTypeEnum >= md::DEPTH1 && d.d.header.marketTypeEnum <= md::DEPTH20){
            //     if ((data.d.body.depth1.ts != 0 && d.d.body.depth1.ts <= data.d.body.depth1.ts)
            //         || d.d.body.depth1.ap1 <= d.d.body.depth1.bp1
            //         || d.d.body.depth1.av1 <= 0 || d.d.body.depth1.bv1 <= 0)
            //         return;
            //     memcpy(&data,&d,sizeof(dbw::DBdata));
            // }

            for(auto iter = listeners.begin(); iter != listeners.end(); ++ iter){
                (*iter)->OnDBSnapUpdate(d);
            }
        }

        void Update(const md::CryptoMarketData& d, int topicId) {
            for(auto iter = listeners.begin(); iter != listeners.end(); ++ iter){
                (*iter)->OnDBSnapUpdate(d, topicId);
            }
        }

        void AddListener(DbSnapListener* l){
            listeners.push_back(l);
        }

    private:
        dbw::DBTopic topic;
        dbw::DBdata  data;
        vector<DbSnapListener*> listeners;
    } ;

    typedef DbSnap* DbSnapPtr;


    class DbSpread : DbSnapListener{
    public:
        DbSpread(DbpTopic* _topic, sp::Writer<DbpTopic,DbpData>* _writer){
            topic = _topic;
            writer = _writer;
            memset(&data,0,sizeof(data));

            for (int i =0 ; i< 4;++i){
                spreadMax[i] = SPREAD_MIN;
                spreadMin[i] = SPREAD_MAX;
            }            
        }

        //Dbpdata& Data(){return data;};

        virtual ~DbSpread(){}


        bool update_by_depth1(BestPxState& px, const md::CryptoMarketData& cmd, bool isActive) {
            bool updated = false;
            auto& d = cmd.body.depth1;

            if (d.tsTrans <= px.depthUs || d.ap1 <= d.bp1 || d.av1 <= 0 || d.bp1 <= 0) {
                return updated;
            }

            px.depthUs = d.tsTrans;

            if (px.tradesSellUs >= px.depthUs && px.depthUs > 0) {
                px.bp1 = std::min(d.bp1, px.tradesSellPrice);
                px.bv1 = 0;
            }
            else {
                px.bp1 = d.bp1;
                px.bv1 = d.bv1;
            }

            if (px.tradesBuyUs >= px.depthUs && px.depthUs > 0) {
                px.ap1 = std::max(d.ap1, px.tradesBuyPrice);
                px.av1 = 0;
            }
            else {
                px.ap1 = d.ap1;
                px.av1 = d.av1;
            }

            px.ts = d.tsEvent;

            if (isActive) {
                px.exchActiveTradeDelay = d.tsEvent - d.tsTrans;
            }
            else {
                px.exchPassiveTradeDelay = d.tsEvent - d.tsTrans;
            }

            updated = true;
            return updated;
        }

        bool update_by_trades(BestPxState& px, const md::CryptoMarketData& cmd, bool isActive) {
            bool updated = false;
            auto& t = cmd.body.trades;

            if (t.tsTrans <= 0 || t.sz <= 0 || t.px <= 0) {
                return updated;
            }

            if (t.direction == DT_SHORT && t.tsTrans >= px.tradesSellUs) {
                px.tradesSellUs = t.tsTrans;
                px.tradesSellPrice = t.px;
                if (px.tradesSellUs >= px.depthUs && px.depthUs > 0) {
                    px.bp1 = std::min(px.bp1, t.px);
                    px.bv1 = 0;
                    px.ts = t.tsEvent;
                    if (isActive) {
                        px.exchActiveTradeDelay = t.tsEvent - t.tsTrans;
                    }
                    else {
                        px.exchPassiveTradeDelay = t.tsEvent - t.tsTrans;
                    }
                }
                else {
                    return updated;
                }
            }
            else {
                return updated;
            }

            updated = true;
            return updated;
        }


        virtual void OnDBSnapUpdate(const md::CryptoMarketData& d, int topicId) {
            bool bActive = false;
            if (d.header.marketTypeEnum == md::FUNDING_RATE) {
                bActive = (topicId == topic->activeFundingRateDBWID);

                auto& fundingrt = bActive ? data.activeFundingRate : data.passiveFundingRate;
                auto& nextfundingrt = bActive ? data.activeNextFundingRate : data.passiveNextFundingRate;
                auto& fundingts = bActive ? data.activeFundingTs : data.passiveFundingTs;
                fundingrt = d.body.fundingRate.fundingRate;
                nextfundingrt = d.body.fundingRate.nextFundingRate;
                fundingts = d.body.fundingRate.fundingTime;
                return;
            }

            if (d.header.marketTypeEnum == md::DEPTH1) {
                bActive = (topicId == topic->activeDepth1DBWID);
            }
            else if (d.header.marketTypeEnum == md::TRADES) {
                bActive = (topicId == topic->passiveDepth1DBWID);
            }
            else {
                return;
            }

            auto& px = bActive ? activePx : passivePx;

            bool updated = false;
            if (d.header.marketTypeEnum == md::DEPTH1) {
                updated = update_by_depth1(px, d, bActive);

                if (!updated) {
                    return;
                }
            }
            else if (d.header.marketTypeEnum == md::TRADES) {
                updated = update_by_trades(px, d, bActive);
            }
            else {
                return;
            }

            auto curtime = high_resolution_clock::now().time_since_epoch().count()/1000;
            auto calctema = [&](const double spread,double& _data){      
                if (data.generateTs == 0 || (topic->stematime < curtime - data.generateTs)) {
                    _data = spread;
                    return;
                }
                //auto d = (double)(curtime - data.generateTs);
                //_data = ( d * spread + (topic->stematime - d) * _data) / topic->stematime;
                _data = ( 2 * spread + 8 * _data) / 10;
            };
            auto calcspread = [&](const double d1,const double d2,double& spread){
                if (topic->spreadCalcType == SPCT_PRICEDIV1)
                    spread = (d1-d2)/d1;
                else if (topic->spreadCalcType == SPCT_PRICEDIV2)
                    spread = (d2-d1)/d1; 
                else if (topic->spreadCalcType == SPCT_PRICESUB1)
                    spread = d1-d2;
                else 
                    spread = d2-d1;
            };
            auto calcminmax = [&](const double spread,double& min,double& max){
                if (spread < min){
                    min = spread;
                }
                if (spread > max){
                    max = spread;
                }
            };

            if (bActive) {
                data.exchActiveTradeDelay = px.exchActiveTradeDelay;

                data.activeAskPrice[0] = px.ap1;
                data.activeBidPrice[0] = px.bp1;
                data.activeAskVolume[0] = px.av1;
                data.activeBidVolume[0] = px.bv1;
                data.activeDepthTs = px.ts;
                data.activeDepthDelay = curtime - data.activeDepthTs;

                if (updated) {
                    if (data.activePriceTema < DBL_MIN) {
                        data.activePriceTema = 0.5 * (data.activeAskPrice[0] + data.activeBidPrice[0]);
                    }
                    else {
                        calctema(0.5 * (data.activeAskPrice[0] + data.activeBidPrice[0]), data.activePriceTema);
                    }
                }
            }
            else {
                data.exchPassiveTradeDelay = px.exchPassiveTradeDelay;

                data.passiveAskPrice[0] = px.ap1;
                data.passiveBidPrice[0] = px.bp1;
                data.passiveAskVolume[0] = px.av1;
                data.passiveBidVolume[0] = px.bv1;
                data.passiveDepthTs = px.ts;
                data.passiveDepthDelay = curtime - data.passiveDepthTs;

                if (updated) {
                    if (data.passivePriceTema < DBL_MIN) {
                        data.passivePriceTema = 0.5 * (data.passiveAskPrice[0] + data.passiveBidPrice[0]);
                    }
                    else {
                        calctema(0.5 * (data.passiveAskPrice[0] + data.passiveBidPrice[0]), data.passivePriceTema);
                    }
                }    
            }

            data.diffTs = data.activeDepthTs - data.passiveDepthTs;
            if (curtime - px.ts > topic->activeCheckspan && px.ts > 0) {
                data.spreadEffective = false;
                data.statEffective = false;
            }
            else if (abs(data.diffTs) > topic->activeCheckspan) {
                data.spreadEffective = false;
                data.statEffective = false;
            }
            else {
                data.spreadEffective = true;
                data.statEffective = true;
            }

            if (updated) {
                calcspread(data.activeBidPrice[0], data.passiveAskPrice[0], data.spreadBidAsk);
                calcspread(data.activeBidPrice[0], data.passiveBidPrice[0], data.spreadBidBid);
                calcspread(data.activeAskPrice[0], data.passiveBidPrice[0], data.spreadAskBid);
                calcspread(data.activeAskPrice[0], data.passiveAskPrice[0], data.spreadAskAsk);
            }


            if (data.statEffective && updated) {
                calctema(data.spreadBidAsk,data.spreadBidAskTema);
                calctema(data.spreadBidBid,data.spreadBidBidTema);
                calctema(data.spreadAskBid,data.spreadAskBidTema);
                calctema(data.spreadAskAsk,data.spreadAskAskTema);
                calcminmax(data.spreadBidAsk,spreadMin[0],spreadMax[0]);
                calcminmax(data.spreadBidBid,spreadMin[1],spreadMax[1]);
                calcminmax(data.spreadAskBid,spreadMin[2],spreadMax[2]);
                calcminmax(data.spreadAskAsk,spreadMin[3],spreadMax[3]);
            }

            data.generateTs =  high_resolution_clock::now().time_since_epoch().count()/1000;
            if ((data.generateTs - resetts > topic->maxmintime) && updated) {
                resetts = data.generateTs;
                data.spreadBidAskMax = spreadMax[0];
                data.spreadBidBidMax = spreadMax[1];
                data.spreadAskBidMax = spreadMax[2];
                data.spreadAskAskMax = spreadMax[3];
                data.spreadBidAskMin = spreadMin[0];
                data.spreadBidBidMin = spreadMin[1];
                data.spreadAskBidMin = spreadMin[2];
                data.spreadAskAskMin = spreadMin[3];
                for (int i =0 ; i< 4;++i){
                    spreadMax[i] = SPREAD_MIN;
                    spreadMin[i] = SPREAD_MAX;
                }
            }

            auto _data = writer->Fetch(topic->__mnodeid);
            memcpy(_data,&data,sizeof(DbpData));
            writer->Commit(topic->__mnodeid);
        }


        virtual void OnDBSnapUpdate(const dbw::DBdata& d)
        {
            
        }

        bool OnCheck(dbw::DBdata& d)
        {
            

            return true;
        }

    private:
        DbpTopic* topic;
        DbpData  data;
        sp::Writer<DbpTopic,DbpData>* writer;

        //for minmax
        uint64_t resetts{0};
        // bool resetminmax{true};  //first time need refresh max,min
        double spreadMax[4];     //current span time
        double spreadMin[4];

        BestPxState activePx;
        BestPxState passivePx;
    } ;
    
    typedef DbSpread* DbSpreadPtr;


}