#include <dbprocess/dbpserver.h>
#include "SpinBackoff.h"


namespace dbp {

    DbpServer::DbpServer() {
        mTopicId.clear();
    }

    DbpServer::~DbpServer() {
        for (size_t i = 0; i < snaps.size(); ++i) {
            delete snaps[i];
            snaps[i] = nullptr;
        }
        snaps.clear();

        for (size_t i = 0; i < dbspds.size(); ++i) {
            delete dbspds[i];
            dbspds[i] = nullptr;
        }
        dbspds.clear();
    }

    bool DbpServer::PreStart(Config* config) {
        bool started = true;

        std::string verS = "";
        if (config->get_dbprocess_config("version", verS)) {
            version = std::stoi(verS);
        }
        
        std::string topicS = "";
        if (config->get_dbprocess_config("topiclen", topicS)) {
            topiclen = std::stoi(topicS);
        }
        
        std::string skipS = "";
        if (config->get_dbprocess_config("skipdata", skipS)) {
            skipdata = std::stoi(skipS);
        } 
        
        std::string stemS = "";
        if (config->get_dbprocess_config("stematime", stemS)) {
            stematime = std::stoi(stemS);
        } 
         
        std::string temaS = "";
        if (config->get_dbprocess_config("tematime", temaS)) {
            tematime = std::stoi(temaS);
        } 

        std::string maxmS = "";
        if (config->get_dbprocess_config("maxmintime", maxmS)) {
            maxmintime = std::stoi(maxmS);
        } 

        std::string timsS = "";
        if (config->get_dbprocess_config("timsspan", timsS)) {
            timsspan = std::stoi(timsS);
        } 

        std::string checkS = "";
        if (config->get_dbprocess_config("checkspan", checkS)) {
            checkspan = std::stoi(checkS);
        } 

        std::string delayS = "";
        if (config->get_dbprocess_config("delayintvel", delayS)) {
            delayintvel = std::stoi(delayS);
        } 

        std::string spreadS = "";
        if (config->get_dbprocess_config("spreadtype", spreadS)) {
            spreadtype = std::stoi(spreadS);
        } 

        std::string spreadcalS = "";
        if (config->get_dbprocess_config("spreadcalctype", spreadcalS)) {
            spreadcalctype = std::stoi(spreadcalS);
        } 

        std::string spreaddriS = "";
        if (config->get_dbprocess_config("spreaddrive", spreaddriS)) {
            spreaddrive = std::stoi(spreaddriS);
        } 

        if (config->get_dbprocess_config("pairlistfile", pairlistfile)) {
            if (!crypto::is_file_existed(pairlistfile)) {
                LOG_ERROR("pairlistfile: {} not exist!", pairlistfile);
                started = false;
            }
        }
        else {
            LOG_ERROR("pairlistfile: {} not set!", pairlistfile);
            started = false;
        }

        if (!config->get_dbprocess_config("submkttypes", submkttypes)) {
            LOG_ERROR("submkttypes: {} not set!", submkttypes); 
            started = false;  
        }

        std::vector<std::string> vMarketTypeStr;
        boost::split(vMarketTypeStr, submkttypes, boost::is_any_of("|"));
        for (auto i = 0; i < vMarketTypeStr.size(); ++i) {
            auto& typeStr = vMarketTypeStr[i];
            vMarketType.push_back(md::MarketTypeStr2EnumMap[typeStr]);
        }

        if (!config->get_dbprocess_config("dbptopicidfile", dbptopicidfile)) {
            LOG_ERROR("dbptopicidfile: {} not set!", dbptopicidfile); 
            started = false;  
        }  

        if (config->get_dbprocess_config("mpath", mpath) && config->get_dbprocess_config("dpath", dpath)) {
            if (!crypto::is_file_existed(mpath) || !crypto::is_file_existed(dpath)) {
                LOG_INFO("start init shm, mpath: {}, dpath: {}", mpath, dpath);
                InitShm();
            }
        }
        else {
            LOG_ERROR("mpath: {} or dpath: {} not set!", mpath, dpath); 
            started = false;
        } 

        LOG_INFO("version: {}", version);
        LOG_INFO("mpath: {}", mpath);
        LOG_INFO("dpath: {}", dpath);
        LOG_INFO("topiclen: {}", topiclen);
        LOG_INFO("skipdata: {}", skipdata);
        LOG_INFO("stematime: {}", stematime);
        LOG_INFO("tematime: {}", tematime);
        LOG_INFO("maxmintime: {}", maxmintime);
        LOG_INFO("timsspan: {}", timsspan);
        LOG_INFO("checkspan: {}", checkspan);
        LOG_INFO("delayintvel: {}", delayintvel);
        LOG_INFO("spreadtype: {}", spreadtype);
        LOG_INFO("spreadcalctype: {}", spreadcalctype);
        LOG_INFO("spreaddrive: {}", spreaddrive);
        LOG_INFO("pairlistfile: {}", pairlistfile);
        LOG_INFO("submkttypes: {}", submkttypes);
        LOG_INFO("dbptopicidfile: {}", dbptopicidfile);

        lastReadTime = crypto::getCurrentTimeSeconds();
        lastUpdateTime = crypto::getCurrentTimeSeconds();

        ReadPairList();

        loadMapFromFile(mTopicId, dbptopicidfile);
        snapscount = mTopicId.size();

        LOG_INFO("db snapscount: {}", snapscount);

        // 校验 mTopicId 里的 id 恰好是 0..snapscount-1 的一个排列。
        std::vector<char> seen(snapscount, 0);
        for (const auto& kv : mTopicId) {
            if (kv.second < 0 || static_cast<uint32_t>(kv.second) >= snapscount || seen[kv.second]) {
                LOG_ERROR("dbptopicid file corrupted: channel {} -> id {}, snapscount {}, dup or out of range. "
                            "delete {} and the shm files to rebuild", kv.first, kv.second, snapscount, dbptopicidfile);
                return false;
            }
            seen[kv.second] = 1;
        }

        snaps.resize(snapscount);
        for (uint32_t i = 0; i < snapscount; ++i) {
            snaps[i] = new DbSnap();
        }

        for (auto iter = mTopicId.begin(); iter != mTopicId.end(); ++iter) {
            std::string channel = iter->first;
            const int topicId = iter->second;

            std::vector<std::string> result;
            boost::split(result, channel, boost::is_any_of("."));
            std::string marketTypeStr = result[result.size() - 1];
            if (marketTypeStr == "DEPTH1") {
                try {
                    suberDepth.push_back({std::make_shared<Depth1Spmc>(channel), topicId});
                }
                catch (const std::exception& e) {
                    std::cerr << "depthSuber error: " << e.what() << " channel: " << channel << std::endl;
                }
            }
            else if (marketTypeStr == "FUNDING_RATE") {
                try {
                    suberFunding.push_back({std::make_shared<FundingRateSpmc>(channel), topicId});
                }
                catch (const std::exception& e) {
                    std::cerr << "fundingSuber error: " << e.what() << " channel: " << channel << std::endl;
                }
            }
            else if (marketTypeStr == "TRADES") {
                try {
                    suberTrades.push_back({std::make_shared<TradesSpmc>(channel), topicId});
                }
                catch (const std::exception& e) {
                    std::cerr << "tradesSuber error: " << e.what() << " channel: " << channel << std::endl;
                }
            }
        }


        writer = new sp::Writer<DbpTopic, DbpData>(mpath, dpath, dbp::DBP_COL_SIZE);
        auto dbp = writer->GetColumns();
        dbspdscount = writer->ColumnCount();

        LOG_INFO("dbspdscount: {}", dbspdscount);

        // 共享内存里已有的列数不能超过 CSV 里登记的 channel 数。
        if (dbspdscount > snapscount) {
            LOG_ERROR("shm has {} columns but dbptopicid only has {} channels. "
                      "delete {} and the shm files to rebuild", dbspdscount, snapscount, dbptopicidfile);
            return false;
        }

        dbspds.resize(dbspdscount);

        for (uint32_t i = 0; i < dbspdscount; ++i) {
            auto m = writer->GetColumnbyID(i);
            dbspds[i] = new DbSpread(m, writer);

            if (m->activeDepth1DBWID >= 0) {
                snaps[m->activeDepth1DBWID]->AddListener((DbSnapListener*)dbspds[i]);
            }

            if (m->activeFundingRateDBWID >= 0) {
                snaps[m->activeFundingRateDBWID]->AddListener((DbSnapListener*)dbspds[i]);
            }

            if (m->activeTradesDBWID >= 0) {
                snaps[m->activeTradesDBWID]->AddListener((DbSnapListener*)dbspds[i]);
            }

            if (m->passiveDepth1DBWID >= 0) {
                snaps[m->passiveDepth1DBWID]->AddListener((DbSnapListener*)dbspds[i]);
            }

            if (m->passiveFundingRateDBWID >= 0) {
                snaps[m->passiveFundingRateDBWID]->AddListener((DbSnapListener*)dbspds[i]);
            }

            if (m->passiveTradesDBWID >= 0) {
                snaps[m->passiveTradesDBWID]->AddListener((DbSnapListener*)dbspds[i]);
            }
        }

        return started;
    }

    void DbpServer::InitShm() {
        int topicId = 0;
        std::vector<DbpTopic> vc;
        std::unordered_set<std::string> setname;
        
        boost::property_tree::ptree pt2;
        boost::property_tree::xml_parser::read_xml(pairlistfile, pt2);
        BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt2.get_child("pairlist")) { 
            DbpTopic mb;
            auto enable =  v.second.get<std::uint32_t>("enable");
            if (enable != 1) 
                continue;
            auto name = v.second.get<std::string>("name");
            if (setname.find(name) != setname.end()){
                continue;
            }
            setname.insert(name);

            std::vector<std::string> result;
            boost::split(result, name, boost::is_any_of("|"));
            if (result.size() != 2)
                continue;

            std::vector<std::string> vActive;
            boost::split(vActive, result[0], boost::is_any_of("."));
            if (vActive.size() != 3)
                continue;

            std::vector<std::string> vPassive;
            boost::split(vPassive, result[1], boost::is_any_of("."));
            if (vPassive.size() != 3)
                continue;

            for (auto t = 0; t < vMarketType.size(); ++t) {
                std::string activeChannel = result[0] + "." + md::MarketTypeEnum2StrMap[vMarketType[t]];
                auto itActiveTopic = mTopicId.find(activeChannel);
                if (itActiveTopic == mTopicId.end()) {
                    mTopicId[activeChannel] = topicId;
                    topicId += 1;
                }

                if (vMarketType[t] == md::DEPTH1) {
                    mb.activeDepth1DBWID = mTopicId[activeChannel];
                }
                else if (vMarketType[t] == md::FUNDING_RATE && vActive[1] != "SPOT") {
                    mb.activeFundingRateDBWID = mTopicId[activeChannel];
                }
                else if (vMarketType[t] == md::TRADES) {
                    mb.activeTradesDBWID = mTopicId[activeChannel];
                }


                std::string passiveChannel = result[1] + "." + md::MarketTypeEnum2StrMap[vMarketType[t]];
                auto itPassiveTopic = mTopicId.find(passiveChannel);
                if (itPassiveTopic == mTopicId.end()) {
                    mTopicId[passiveChannel] = topicId;
                    topicId += 1;
                }

                if (vMarketType[t] == md::DEPTH1) {
                    mb.passiveDepth1DBWID = mTopicId[passiveChannel];
                }
                else if (vMarketType[t] == md::FUNDING_RATE && vPassive[1] != "SPOT") {
                    mb.passiveFundingRateDBWID = mTopicId[passiveChannel];
                }
                else if (vMarketType[t] == md::TRADES) {
                    mb.passiveTradesDBWID = mTopicId[passiveChannel];
                }
            }

            strncpy(mb.__name, name.c_str(), sp::COL_NAME_LEN);
            strncpy(mb.activeInstrumentKey, result[0].c_str(), sp::KEY_NAME_LEN);
            strncpy(mb.passiveInstrumentKey, result[1].c_str(), sp::KEY_NAME_LEN);

            mb.__bufsize = topiclen;
            mb.activeMultiply = v.second.get<uint32_t>("activemultiply");
            mb.passiveMultiply = v.second.get<uint32_t>("passivemultiply");
            mb.activeCheckspan = static_cast<uint64_t>(v.second.get<uint32_t>("delayintvel")) * 1000;
            mb.passivecCheckspan = mb.activeCheckspan;

            mb.spreadDrive = (SpreadDrive)spreaddrive;
            mb.spreadType = (SpreadType)spreadtype;
            mb.spreadCalcType = (SpreadCalcType)spreadcalctype;
            mb.maxmintime = maxmintime * 1000;
            mb.stematime = stematime * 1000;
            mb.tematime = tematime * 1000;

            vc.push_back(mb);

            LOG_INFO("pair name: {}", name);
        }
        
        uint32_t count = vc.size();
        uint32_t size = count * topiclen;
        LOG_INFO("total real topic size: {} total real data size: {}", count, size);

        sp::Initialer<DbpTopic, DbpData> initialer(mpath, count, dpath, size, &vc, version, dbp::DBP_COL_SIZE, dbp::DBP_COL_SIZE * topiclen);

        LOG_INFO("ColumnCount: {}", initialer.ColumnCount());
        LOG_INFO("Version: {}", initialer.Version());
        LOG_INFO("mTopicId size: {}", mTopicId.size());

        if (mTopicId.size() > 0) {
            saveMapToFile(mTopicId, dbptopicidfile);
        }
    }

    void DbpServer::Run() {
        std::thread updateThread(&DbpServer::UpdatePairList, this);
        updateThread.detach();

        std::thread executeThread(&DbpServer::Execute, this);
        executeThread.detach();
    }

    void DbpServer::ReadPairList() {
        try {
            boost::property_tree::ptree pt2;
            boost::property_tree::xml_parser::read_xml(pairlistfile, pt2);
            BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt2.get_child("pairlist")) {
                auto enable = v.second.get<uint32_t>("enable");
                if (enable != 1) {
                    continue;
                }

                auto name = v.second.get<std::string>("name");
                pairInstrumentKeySet.insert(name);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("read pairlist file error: {}", e.what());
        }
        catch (...) {
             LOG_ERROR("read pairlist file unknown error");
        }
    }

    void DbpServer::UpdatePairList() {
        while (1) {
            sleep(10);

            int64_t currentTime = crypto::getCurrentTimeSeconds();
            if (currentTime - lastReadTime > 5 * 60) {
                lastReadTime = currentTime;

                try {
                    boost::property_tree::ptree pt2;
                    boost::property_tree::xml_parser::read_xml(pairlistfile, pt2);
                    BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt2.get_child("pairlist")) {
                        auto enable = v.second.get<uint32_t>("enable");
                        if (enable != 1) {
                            continue;
                        }

                        auto name = v.second.get<std::string>("name");
                        std::vector<std::string> result;
                        boost::split(result, name, boost::is_any_of("|"));
                        if (result.size() != 2) {
                            continue;
                        }

                        if (pairInstrumentKeySet.find(name) == pairInstrumentKeySet.end()) {
                            symbolQueue.push(name);
                            pairInstrumentKeySet.insert(name);
                        }
                    }
                }
                catch (const std::exception& e) {
                    LOG_ERROR("read pairlist file error: {}", e.what());
                }
                catch (...) {
                    LOG_ERROR("read pairlist file unknown error");
                }
            }
        }
    }

    void DbpServer::Execute() {
        std::string symbolStr;
        md::CryptoMarketData cmd;
        while (1) {
            bool idle = true;
            for (auto& sub : suberDepth) {
                if (!sub.suber->tryPopLatest(cmd.body.depth1)) {
                    continue;
                }
                idle = false;
                cmd.header.marketTypeEnum = md::DEPTH1;
                snaps[sub.topicId]->Update(cmd, sub.topicId);
            }

            for (auto& sub : suberTrades) {
                if (!sub.suber->tryPopLatest(cmd.body.trades)) {
                    continue;
                }
                idle = false;
                cmd.header.marketTypeEnum = md::TRADES;
                snaps[sub.topicId]->Update(cmd, sub.topicId);
            }

            for (auto& sub : suberFunding) {
                if (!sub.suber->tryPopLatest(cmd.body.fundingRate)) {
                    continue;
                }
                idle = false;
                cmd.header.marketTypeEnum = md::FUNDING_RATE;
                snaps[sub.topicId]->Update(cmd, sub.topicId);
            }


            int64_t currentTime = crypto::getCurrentTimeSeconds();
            if (currentTime - lastUpdateTime > 60) {
                lastUpdateTime = currentTime;

                std::vector<std::string> vSymbol;
                while (symbolQueue.pop(symbolStr)) {
                    LOG_INFO("dbprocess has new symbol, symbolStr: {}", symbolStr);
                    vSymbol.push_back(symbolStr);
                }

                for (size_t i = 0; i < vSymbol.size(); ++i) {
                    std::string symbolStr = vSymbol[i];
                    std::vector<std::string> result;
                    boost::split(result, symbolStr, boost::is_any_of("|"));
                    if (result.size() == 2) {
                        std::string symbol1 = result[0];
                        std::string symbol2 = result[1];

                        std::vector<std::string> res1;
                        boost::split(res1, symbol1, boost::is_any_of("."));

                        std::vector<std::string> res2;
                        boost::split(res2, symbol2, boost::is_any_of("."));

                        if (res1.size() >= 3 && res2.size() >= 3) {
                            DbpTopic mb;
                            bool updated = true;

                            // 给一个 channel 备好"订阅者 + 快照 + topicId"，返回它的 topicId。
                            // 返回 -1 有两种情况：① 这个 channel 不需要快照（现货腿的资金费率）；
                            // ② 建订阅者失败（此时已置 updated = false）。
                            // 调用方不用单独判断
                            auto ensureChannel = [&](const std::string& channel, md::MarketType mt, bool isSpotLeg) -> int {
                                auto it = mTopicId.find(channel);
                                if (it != mTopicId.end()) {
                                    return it->second;
                                }

                                const bool wantSnap = (mt == md::DEPTH1) || (mt == md::FUNDING_RATE && !isSpotLeg) || (mt == md::TRADES);
                                if (!wantSnap) {
                                    // 关键：**先判断要不要建快照，再决定占不占 topicId**。
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
                                    LOG_ERROR("create subscriber failed: {} channel: {}", e.what(), channel);
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

                            for (auto t = 0; t < vMarketType.size(); ++t) {
                                const md::MarketType mt = vMarketType[t];
                                const std::string marketTypeStr = md::MarketTypeEnum2StrMap[mt];

                                // 返回值不判 -1：
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

                            if (updated) {
                                strncpy(mb.__name, symbolStr.c_str(), sp::COL_NAME_LEN);
                                strncpy(mb.activeInstrumentKey, result[0].c_str(), sp::KEY_NAME_LEN);
                                strncpy(mb.passiveInstrumentKey, result[1].c_str(), sp::KEY_NAME_LEN);

                                mb.__bufsize = topiclen;
                                mb.activeMultiply = 1;
                                mb.passiveMultiply = 1;
                                mb.activeCheckspan = delayintvel * 1000;
                                mb.passivecCheckspan = mb.activeCheckspan;

                                mb.spreadDrive = (SpreadDrive)spreaddrive;
                                mb.spreadType = (SpreadType)spreadtype;
                                mb.spreadCalcType = (SpreadCalcType)spreadcalctype;
                                mb.maxmintime = maxmintime * 1000;
                                mb.stematime = stematime * 1000;
                                mb.tematime = tematime * 1000;
                                LOG_INFO("dbprocess start to add new column: {}", mb.__name);
                                writer->AddNewColumn(mb);

                                auto m = writer->GetColumnbyID(dbspdscount);
                                auto dbspread = new DbSpread(m, writer);
                                dbspds.push_back(dbspread);

                                if (m->activeDepth1DBWID >= 0) {
                                    snaps[m->activeDepth1DBWID]->AddListener((DbSnapListener*)dbspds[dbspdscount]);
                                }

                                if (m->activeFundingRateDBWID >= 0) {
                                    snaps[m->activeFundingRateDBWID]->AddListener((DbSnapListener*)dbspds[dbspdscount]);
                                }

                                if (m->activeTradesDBWID >= 0) {
                                    snaps[m->activeTradesDBWID]->AddListener((DbSnapListener*)dbspds[dbspdscount]);
                                }

                                if (m->passiveDepth1DBWID >= 0) {
                                    snaps[m->passiveDepth1DBWID]->AddListener((DbSnapListener*)dbspds[dbspdscount]);
                                }

                                if (m->passiveFundingRateDBWID >= 0) {
                                    snaps[m->passiveFundingRateDBWID]->AddListener((DbSnapListener*)dbspds[dbspdscount]);
                                }

                                if (m->passiveTradesDBWID >= 0) {
                                    snaps[m->passiveTradesDBWID]->AddListener((DbSnapListener*)dbspds[dbspdscount]);
                                }

                                dbspdscount += 1;

                                saveMapToFile(mTopicId, dbptopicidfile);
                            }
                            else {
                                LOG_WARN("symbolStr: {} not add successfully this time!", symbolStr);
                                symbolQueue.push(symbolStr);
                            }
                        }
                    }
                }
            }

            if (idle) {
                spin::cpu_relax();  
            }
        }
    }
}