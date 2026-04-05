#include <iostream>
#include <unistd.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
#include "redis_client.h"
#include <cpprest/json.h>
#include <shmpool/shmpool.h>
#include <dbp/include.h>
#include <set>
#include "time_util.h"

#include <csignal>
#include "log_engine.h"
#include "program_util.h"
#include "config.h"
#include "crypto_exception.h"


using namespace std;
using namespace sp;
using namespace web;


namespace dbp {

    class DbpSnapshot {
    public:
        DbpSnapshot() {

        }

        ~DbpSnapshot() {

        }

        bool PreStart(Config* config) {
            bool started = true;

            std::string portStr = "";
            if (config->get_redis_config("host", host) && config->get_redis_config("port", portStr) && config->get_redis_config("password", password)) {
                port = std::stoi(portStr);
            }
            else {
                LOG_ERROR("redis not set, host:{} port:{} password:{}", host, port, password); 
                started = false;     
            }

            if (config->get_dbpsnapshot_config("mpath", mpath) && config->get_dbpsnapshot_config("dpath", dpath)) {
                if (!crypto::is_file_existed(mpath) || !crypto::is_file_existed(dpath)) {
                    LOG_ERROR("mpath: {} or dpath: {} not exist!", mpath, dpath);
                    started = false;
                }
            }
            else {
                LOG_ERROR("mpath: {} or dpath: {} not set!", mpath, dpath); 
                started = false;
            }

            std::string spanS = "";
            if (config->get_dbpsnapshot_config("spansecond", spanS)) {
                spansecond = std::stoi(spanS);
            }

            if (!config->get_dbpsnapshot_config("pubchannel", pubchannel)) {
                LOG_ERROR("pubchannel is null : {}", pubchannel);
                started = false;
            }
            
            return started;
        }

        void Run() {
            try
            {
                long lastUpdateTime = crypto::getCurrentTimeSeconds();

                sp::Reader<dbp::DbpTopic,dbp::DbpData> reader(mpath, dpath, dbp::DBP_COL_SIZE);
                auto ColumnCount = reader.ColumnCount();

                LOG_INFO("ColumnCount: {}", ColumnCount);

                redisClient = new RedisClient(host.c_str(), port, password.c_str(), false, true);

                auto skippednum = 0;
                dbp::DbpData * d = nullptr;
                json::value obj;
                uint64_t count = 0;

                while(1){
                    sleep(spansecond);
                    for (uint64_t j = 0 ; j < ColumnCount; ++j) {
                        skippednum = reader.FetchLast(j,&d);
                        if (skippednum > 0){
                            auto& data = *d;
                            auto topic = reader.GetColumnbyID(j);
                            
                            obj["topic"]=json::value::string(topic->__name);
                            obj["spef"]=json::value::number(data.spreadEffective);
                            obj["stef"]=json::value::number(data.statEffective);
                            obj["sba"]=json::value::number(data.spreadBidAsk);
                            obj["sbb"]=json::value::number(data.spreadBidBid);
                            obj["sab"]=json::value::number(data.spreadAskBid);
                            obj["saa"]=json::value::number(data.spreadAskAsk);
                            obj["sbat"]=json::value::number(data.spreadBidAskTema);
                            obj["sbbt"]=json::value::number(data.spreadBidBidTema);
                            obj["sabt"]=json::value::number(data.spreadAskBidTema);
                            obj["saat"]=json::value::number(data.spreadAskAskTema);
                            obj["sbamax"]=json::value::number(data.spreadBidAskMax);
                            obj["sbbmax"]=json::value::number(data.spreadBidBidMax);
                            obj["sabmax"]=json::value::number(data.spreadAskBidMax);
                            obj["saamax"]=json::value::number(data.spreadAskAskMax);
                            obj["sbamin"]=json::value::number(data.spreadBidAskMin);
                            obj["sbbmin"]=json::value::number(data.spreadBidBidMin);
                            obj["sabmin"]=json::value::number(data.spreadAskBidMin);
                            obj["saamin"]=json::value::number(data.spreadAskAskMin);
                            obj["aap1"]=json::value::number(data.activeAskPrice[0]);
                            obj["abp1"]=json::value::number(data.activeBidPrice[0]);
                            obj["aav1"]=json::value::number(data.activeAskVolume[0]);
                            obj["abv1"]=json::value::number(data.activeBidVolume[0]);
                            obj["pap1"]=json::value::number(data.passiveAskPrice[0]);
                            obj["pbp1"]=json::value::number(data.passiveBidPrice[0]);
                            obj["pav1"]=json::value::number(data.passiveAskVolume[0]);
                            obj["pbv1"]=json::value::number(data.passiveBidVolume[0]);
                            obj["afr"]=json::value::number(data.activeFundingRate);
                            obj["pfr"]=json::value::number(data.passiveFundingRate);
                            obj["anfr"]=json::value::number(data.activeNextFundingRate);
                            obj["pnfr"]=json::value::number(data.passiveNextFundingRate);
                            obj["apt"]=json::value::number(data.activePriceTema);
                            obj["ppt"]=json::value::number(data.passivePriceTema);
                            obj["aft"]=json::value::number(data.activeFundingTs);
                            obj["pft"]=json::value::number(data.passiveFundingTs);
                            obj["adt"]=json::value::number(data.activeDepthTs);
                            obj["pdt"]=json::value::number(data.passiveDepthTs);
                            obj["dts"]=json::value::number(data.diffTs);
                            obj["gts"]=json::value::number(data.generateTs);

                            if (redisClient) {
                                redisClient->publish(pubchannel.c_str(), obj.serialize().c_str());
                            }
                        }
                    }


                    long currentTime = crypto::getCurrentTimeSeconds();
                    if (currentTime - lastUpdateTime > 60) {
                        lastUpdateTime = currentTime;
                        if (reader.UpdateByHeader()) {
                            ColumnCount = reader.ColumnCount();
                        }
                    }
                }
            }
            catch(const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
        }

    private:
        std::string host{""};
        int port{0};
        std::string password{""};
        RedisClient* redisClient;

        std::string mpath{""};
        std::string dpath{""};
        std::string pubchannel{""};
        int spansecond{5};
    };
}


static void signal_handler(int signum) {
    LOG_INFO("handler interrupt signal {} received, dbprocess will exit now.", signum);
    fprintf(stdout, "handler interrupt signal (%d) received.\n", signum);
    fmtlog::poll();
    sleep(0.5);//wait logger write data to disk
    exit(signum);
}

static void usage(void){
    fprintf(stderr, "\nusage:\n");
    fprintf(stderr, "./application json_config_file \n");
    fprintf(stderr, "\n");
    exit(-1);
}

int main(int argc, char* argv[]) {
    if ( argc != 2 ){
        usage();
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    Config* config = Config::instance();
    config->load(argv[1]);

    std::string tag = "";
    std::string logPath = "";
    std::string logLevel = "";

    if (config->get_string("tag", tag) && config->get_log_config("log_path", logPath) && config->get_log_config("level", logLevel)) {
        if (crypto::create_directory(logPath)) {
            log_maintain(tag, logPath, logLevel);
        }
        else {
            exit(-1);
        }
    }
    else {
        fprintf(stderr, "not found log config info in %s,\nneed to add log config\n", argv[1]);
        exit(-1);
    }

    std::string program = tag;
    long currentPid = getpid();
    long filePid = crypto::get_program_pid(program);
    if(!crypto::ensure_one_instance(program) && currentPid != filePid) {
        std::string errormsg = tag + " with pid=" + std::to_string(filePid) + " already exists, aborted";
        cryptothrow(errormsg.c_str(), -1);
    }
    crypto::write_program_pid(program);

    dbp::DbpSnapshot* op = new dbp::DbpSnapshot();
    if(op->PreStart(config)) {
        op->Run();
    }
    else {
        cryptothrow("dbpnsapshot start failed!", -1);
    }

    while(1) {
        log_maintain(tag, logPath, logLevel);
        usleep(1000);
    }

    return 0;
}