#include <dbprocess/dbpserver.h>


namespace dbp {

    DbpServer::DbpServer() {
        mTopicId.clear();
    }

    DbpServer::~DbpServer() {
        for (uint32_t i=0; i < snapscount; ++i ){
            delete snaps[i];
            snaps[i] = nullptr;
        }     
        snaps.clear();

        for (uint32_t i=0; i < dbspdscount; ++i ){
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
            maxmintime = std::stoi(temaS);
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

        snaps.resize(snapscount);
        for (uint32_t i = 0; i < snapscount; ++i) {
            snaps[i] = new DbSnap();
        }

        for (auto iter = mTopicId.begin(); iter != mTopicId.end(); ++iter) {
            std::string channel = iter->first;
            std::vector<std::string> result;
            boost::split(result, channel, boost::is_any_of("."));

            std::string marketTypeStr = result[result.size() - 1];
            if (marketTypeStr == "DEPTH1") {
                try {
                    pubsub::SPMCSubscriber<md::Depth1> depthSuber(channel.c_str());
                    suberDepth.push_back(depthSuber);
                }
                catch (const std::exception& e) {
                    std::cerr << "depthSuber error: " << e.what() << " channel: " << channel << std::endl;
                }
            }
            else if (marketTypeStr == "FUNDING_RATE") {
                try {
                    pubsub::SPMCSubscriber<md::FundingRate> fundingSuber(channel.c_str());
                    suberFunding.push_back(fundingSuber);
                }
                catch (const std::exception& e) {
                    std::cerr << "fundingSuber error: " << e.what() << " channel: " << channel << std::endl;
                }
            }
            else if (marketTypeStr == "TRADES") {
                try {
                    pubsub::SPMCSubscriber<md::Trades> tradesSuber(channel.c_str());
                    suberTrades.push_back(tradesSuber);
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

            long currentTime = crypto::getCurrentTimeSeconds();
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
            for (auto& suber : suberDepth) {
                bool mdPop = suber.pop_last(cmd.body.depth1);
                if (mdPop) {
                    cmd.header.marketTypeEnum = md::DEPTH1;
                    cmd.header.exchangeTypeEnum = cmd.body.depth1.exchangeTypeEnum;
                    cmd.header.instTypeEnum = cmd.body.depth1.instTypeEnum;
                    strncpy(cmd.header.instId, cmd.body.depth1.instId, INSTID_SIZE);

                    const std::string& channel = crypto::get_md_channel_key(cmd.header.exchangeTypeEnum, cmd.header.instTypeEnum, md::DEPTH1, cmd.header.instId);
                    int topicId = mTopicId[channel];
                    snaps[topicId]->Update(cmd, topicId);
                }
            }

            for (auto& suber : suberTrades) {
                bool mdPop = suber.pop_last(cmd.body.trades);
                if (mdPop) {
                    cmd.header.marketTypeEnum = md::TRADES;
                    cmd.header.exchangeTypeEnum = cmd.body.trades.exchangeTypeEnum;
                    cmd.header.instTypeEnum = cmd.body.trades.instTypeEnum;
                    strncpy(cmd.header.instId, cmd.body.trades.instId, INSTID_SIZE);

                    const std::string& channel = crypto::get_md_channel_key(cmd.header.exchangeTypeEnum, cmd.header.instTypeEnum, md::TRADES, cmd.header.instId);
                    int topicId = mTopicId[channel];
                    snaps[topicId]->Update(cmd, topicId);
                }
            }

            for (auto& suber : suberFunding) {
                bool mdPop = suber.pop_last(cmd.body.fundingRate);
                if (mdPop) {
                    cmd.header.marketTypeEnum = md::FUNDING_RATE;
                    cmd.header.exchangeTypeEnum = cmd.body.fundingRate.exchangeTypeEnum;
                    cmd.header.instTypeEnum = cmd.body.fundingRate.instTypeEnum;
                    strncpy(cmd.header.instId, cmd.body.fundingRate.instId, INSTID_SIZE);

                    const std::string& channel = crypto::get_md_channel_key(cmd.header.exchangeTypeEnum, cmd.header.instTypeEnum, md::FUNDING_RATE, cmd.header.instId);
                    int topicId = mTopicId[channel];
                    snaps[topicId]->Update(cmd, topicId);
                }
            }


            long currentTime = crypto::getCurrentTimeSeconds();
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
                            for (auto t = 0; t < vMarketType.size(); ++t) {
                                std::string activeChannel = result[0] + "." + md::MarketTypeEnum2StrMap[vMarketType[t]];

                                auto itActive = mTopicId.find(activeChannel);
                                if (itActive == mTopicId.end()) {
                                    int topicId = snapscount;
                                    mTopicId[activeChannel] = topicId;
                                    snapscount += 1;

                                    if (vMarketType[t] == md::DEPTH1) {
                                        try {
                                            pubsub::SPMCSubscriber<md::Depth1> depthSuber(activeChannel.c_str());
                                            suberDepth.push_back(depthSuber);

                                            auto db = new DbSnap();
                                            snaps.push_back(db);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "depthSuber error: " << e.what() << " channel: " << activeChannel << std::endl;
                                            updated = false;
                                        }
                                    }
                                    else if (vMarketType[t] == md::FUNDING_RATE && res1[1] != "SPOT") {
                                        try {
                                            pubsub::SPMCSubscriber<md::FundingRate> fundingSuber(activeChannel.c_str());
                                            suberFunding.push_back(fundingSuber);

                                            auto db = new DbSnap();
                                            snaps.push_back(db);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "fundingSuber error: " << e.what() << " channel: " << activeChannel << std::endl;
                                            updated = false;
                                        }
                                    }
                                    else if (vMarketType[t] == md::TRADES) {
                                        try {
                                            pubsub::SPMCSubscriber<md::Trades> tradesSuber(activeChannel.c_str());
                                            suberTrades.push_back(tradesSuber);

                                            auto db = new DbSnap();
                                            snaps.push_back(db);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "tradesSuber error: " << e.what() << " channel: " << activeChannel << std::endl;
                                            updated = false;
                                        }
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
                                        try {
                                            pubsub::SPMCSubscriber<md::Depth1> depthSuber(passiveChannel.c_str());
                                            suberDepth.push_back(depthSuber);

                                            auto db = new DbSnap();
                                            snaps.push_back(db);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "depthSuber error: " << e.what() << " channel: " << passiveChannel << std::endl;
                                            updated = false;
                                        }
                                    }
                                    else if (vMarketType[t] == md::FUNDING_RATE && res2[1] != "SPOT") {
                                        try {
                                            pubsub::SPMCSubscriber<md::FundingRate> fundingSuber(passiveChannel.c_str());
                                            suberFunding.push_back(fundingSuber);

                                            auto db = new DbSnap();
                                            snaps.push_back(db);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "fundingSuber error: " << e.what() << " channel: " << passiveChannel << std::endl;
                                            updated = false;
                                        }
                                    }
                                    else if (vMarketType[t] == md::TRADES) {
                                        try {
                                            pubsub::SPMCSubscriber<md::Trades> tradesSuber(passiveChannel.c_str());
                                            suberTrades.push_back(tradesSuber);

                                            auto db = new DbSnap();
                                            snaps.push_back(db);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "tradesSuber error: " << e.what() << " channel: " << passiveChannel << std::endl;
                                            updated = false;
                                        }
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
        }
    }
}