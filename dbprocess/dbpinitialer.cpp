#include <boost/algorithm/string.hpp>
#include <chrono>
#include <iostream>
#include <unordered_set>
#include <shmpool/shmpool.h>
#include <dbw/include.h>
#include <dbp/include.h>
#include "fileutil.h"

using namespace std::chrono;
using namespace sp;
using namespace dbp;

int main(int argc, char* argv[])
{
    std::cout << "dbprocess initialer executed..." << std::endl;
    for(auto i = 0; i < argc; ++ i){
        std::cout << "argv[" << i << "]:" << argv[i] << std::endl;
    }
    if (argc < 2){
        std::cout << "Incorrect parameters, please check!"<< std::endl;
        return 0;
    }
    ::chdir(argv[1]);
 
    try
    {
        auto filename = "dbprocess.xml";
        boost::property_tree::ptree pt;
        boost::property_tree::xml_parser::read_xml(filename, pt);

        auto version = pt.get<uint32_t>("dbprocess.version");
        auto mpath = pt.get<string>("dbprocess.mpath");
        auto dpath = pt.get<string>("dbprocess.dpath");
        auto topiclen = pt.get<uint32_t>("dbprocess.topiclen");
        cout << "version:" << version << endl;
        cout << "mpath:" << mpath << endl;
        cout << "dpath:" << dpath << endl;
        cout << "topiclen:" << topiclen << endl;

        auto skipdata = pt.get<uint32_t>("dbprocess.skipdata");
        auto stematime = pt.get<uint32_t>("dbprocess.stematime");
        auto tematime = pt.get<uint32_t>("dbprocess.tematime");
        auto maxmintime = pt.get<uint32_t>("dbprocess.maxmintime");
        auto timsspan = pt.get<uint32_t>("dbprocess.timsspan");
        auto checkspan = pt.get<uint32_t>("dbprocess.checkspan");

        auto marketype = pt.get<uint32_t>("dbprocess.marketype");
        auto spreadtype = pt.get<uint32_t>("dbprocess.spreadtype");
        auto spreadcalctype = pt.get<uint32_t>("dbprocess.spreadcalctype");
        auto spreaddrive = pt.get<uint32_t>("dbprocess.spreaddrive");

        cout << "marketype:" << marketype << endl;
        cout << "spreadtype:" << spreadtype << endl;
        cout << "spreadcalctype:" << spreadcalctype << endl;
        cout << "spreaddrive:" << spreaddrive << endl;
        
        auto marketType = pt.get<std::string>("dbprocess.submkttypes", "DEPTH1|FUNDING_RATE");
        std::vector<std::string> vMarketTypeStr;
        std::vector<md::MarketType> vMarketType;
        boost::split(vMarketTypeStr, marketType, boost::is_any_of("|"));
        for (auto i = 0; i < vMarketTypeStr.size(); ++i) {
            auto& typeStr = vMarketTypeStr[i];
            vMarketType.push_back(md::MarketTypeStr2EnumMap[typeStr]);
        }

        int topicId = 0;
        std::unordered_map<std::string, int> mTopicId;
        std::vector<DbpTopic> vc;
        std::unordered_set<std::string> setname;
        {
            auto pairlistfile = pt.get<string>("dbprocess.pairlistfile");
            cout << "pairlistfile:" << pairlistfile << endl;
            boost::property_tree::ptree pt2;
            boost::property_tree::xml_parser::read_xml(pairlistfile, pt2);
            BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt2.get_child("pairlist"))
            { 
                std::cout << "00000000000000000000000000000" << std::endl;
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

                std::cout << "----------------------------------------------" << std::endl;
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

                std::cout << "pair name:" << name << std::endl;
            }
        }
        
        uint32_t count = vc.size();
        uint32_t size = count * topiclen;
        std::cout << "total real topic size: " << count << " total real data size:" << size << std::endl;

        sp::Initialer<DbpTopic, DbpData> initialer(mpath, count, dpath, size, &vc, version, dbp::DBP_COL_SIZE, dbp::DBP_COL_SIZE * topiclen);

        std::cout << "ColumnCount " << initialer.ColumnCount() << std::endl;
        std::cout << "Version " << initialer.Version() << std::endl;
        std::cout << "mTopicId size: " << mTopicId.size() << std::endl;

        if (mTopicId.size() > 0) {
            saveMapToFile(mTopicId, "dbptopicid.csv");
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }

    return 0;
}