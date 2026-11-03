#pragma once
#include "dbprocess/dbsnap.h"
#include "dbp/include.h"
#include <set>
#include <thread>
#include "shm_global.h"
#include "concurrent_queue.h"
#include "key_util.h"
#include "log_engine.h"
#include "config.h"
#include "program_util.h"
#include <sys/resource.h>
#include <sched.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include "fileutil.h"


namespace dbp{

    class DbpServer
    {
    public:
        DbpServer();
        ~DbpServer();

        bool PreStart(Config* config);
        void Run();
        void InitShm();
        void ReadPairList();
        void UpdatePairList();
        void Execute();

    private:
        std::vector<DbSnapPtr> snaps;
        uint32_t snapscount{0};
        std::vector<DbSpreadPtr> dbspds;
        uint32_t dbspdscount{0};

        uint32_t version{0};
        std::string mpath{""};
        std::string dpath{""};

        uint32_t topiclen{0};
        uint32_t markettype{0};
        uint32_t spreadtype{0};
        uint32_t spreadcalctype{0};
        uint32_t spreaddrive{0};

        uint32_t skipdata{0};
        uint32_t stematime{0};
        uint32_t tematime{0};
        uint32_t maxmintime{0};
        uint32_t timsspan{0};
        uint32_t checkspan{0};
        uint32_t delayintvel{0};
        std::string pairlistfile{""};
        std::string submkttypes{""};
        std::string dbptopicidfile;

        // 线程绑核：只给**需要独占**的线程开键，-1 = 不绑。
        int cpuExecute{-1};

        sp::Writer<DbpTopic, DbpData>* writer;
        sp::Reader<dbw::DBTopic, dbw::DBdata>* reader;

        pubsub::ConcurrentQueueWF<std::string, 1024> symbolQueue;
        long lastReadTime{0};
        long lastUpdateTime{0};
        
        std::set<std::string> pairInstrumentKeySet;
        std::vector<md::MarketType> vMarketType;
        std::unordered_map<std::string, int> mTopicId;

        // 一个行情订阅者 + 它在 dbp 里对应的 topicId。
        template <typename SpmcT>
        struct MdSub {
            std::shared_ptr<SpmcT> suber;
            int topicId{-1};
        };

        std::vector<MdSub<Depth1Spmc>>      suberDepth;
        std::vector<MdSub<TradesSpmc>>      suberTrades;
        std::vector<MdSub<FundingRateSpmc>> suberFunding;
    };
}