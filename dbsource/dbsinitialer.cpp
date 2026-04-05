#include <boost/algorithm/string.hpp>
#include <chrono>
#include <iostream>
#include <unordered_set>
#include <shmpool/shmpool.h>
#include <dbs/include.h>

using namespace std;
using namespace std::chrono;
using namespace sp;
using namespace dbs;

int main(int argc, char* argv[])
{
    cout << "dbsource initialer executed..." << endl;
    for(auto i = 0; i < argc; ++ i){
        cout << "argv[" << i << "]:" <<argv[i] << endl;
    }
    if (argc < 2){
        cout << "Incorrect parameters, please check!"<< endl;
        return 0;
    }
    ::chdir(argv[1]);
 
    try
    {
        auto filename = "dbsource.xml";
        boost::property_tree::ptree pt;
        boost::property_tree::xml_parser::read_xml(filename, pt);

        auto version = pt.get<uint32_t>("dbsource.version");
        auto mpath = pt.get<string>("dbsource.mpath");
        auto dpath = pt.get<string>("dbsource.dpath");
        auto topiclen = pt.get<uint32_t>("dbsource.topiclen");
        cout << "version:" << version << endl;
        cout << "mpath:" << mpath << endl;
        cout << "dpath:" << dpath << endl;
        cout << "topiclen:" << topiclen << endl;

        vector<DbsTopic> vc;
        unordered_set<string> setname;
        {
            auto exchlist = pt.get<string>("dbsource.exchlistfile");
            cout << "exchlist:" << exchlist << endl;
            boost::property_tree::ptree pt2;
            boost::property_tree::xml_parser::read_xml(exchlist, pt2);
            BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt2.get_child("exchlist"))
            { 
                auto enable =  v.second.get<std::uint32_t>("enable");
                if (enable != 1)
                    continue;
                auto exchange =  v.second.get<std::string>("exchange");
                auto SubMarketType =  v.second.get<std::string>("SubMarketType");
                auto count = v.second.get<std::uint32_t>("count");

                for (auto i=0;i<count;++i){
                    DbsTopic mb;
                    auto name = exchange+ '-' + SubMarketType + '-' + to_string(i);
                    if (setname.find(name) != setname.end()){
                        continue;
                    }
                    setname.insert(name);
                    auto exchangeTypeEnum = ExchangeTypeStr2EnumMap[name];
                    mb.exchangeTypeEnum = exchangeTypeEnum;
                    strncpy(mb.__name,name.c_str(),sp::COL_NAME_LEN);
                    mb.__bufsize = topiclen;
                    vc.push_back(mb);

                    cout << "exchange name:" << name << endl;
                }
            }
        }
        
        uint32_t count = vc.size();
        uint32_t size = count*topiclen;
        cout << "size:" << size << endl;

        //
        sp::Initialer<DbsTopic,DbsData> initialer(mpath,count,dpath,size,&vc,version);
        for (uint32_t i = 0; i < count ; ++ i){
            auto m = initialer.GetColumnbyID(i);
            cout << "name " << m->__name << ",bufsize " << m->__bufsize 
                << ",mnodeid " << m->__mnodeid << ",buffrom " << m->__buffrom << endl;
        }
        cout << "ColumnCount " << initialer.ColumnCount() << endl;
        cout << "Version " << initialer.Version() << endl;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }

    return 0;
}