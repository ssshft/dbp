#pragma once
#include <fstream>
#include <iostream>
#include <time.h>
#include <vector>
#include <thread>
#include <algorithm>

using namespace std;
namespace pf
{
    class perf
    {
    public:
        static struct tm FastSecToDate(int time_zone = 8)
        {
            static thread_local struct tm tm;
            static const int kHoursInDay = 24;
            static const int kMinutesInHour = 60;
            static const int kDaysFromUnixTime = 2472632;
            static const int kDaysFromYear = 153;
            static const int kMagicUnkonwnFirst = 146097;
            static const int kMagicUnkonwnSec = 1461;

            time_t unix_sec = time(NULL);
            tm.tm_sec  =  unix_sec % kMinutesInHour;
            int i      = (unix_sec/kMinutesInHour);
            tm.tm_min  = i % kMinutesInHour; //nn
            i /= kMinutesInHour;
            tm.tm_hour = (i + time_zone) % kHoursInDay; // hh
            tm.tm_mday = (i + time_zone) / kHoursInDay;
            int a = tm.tm_mday + kDaysFromUnixTime;
            int b = (a*4  + 3)/kMagicUnkonwnFirst;
            int c = (-b*kMagicUnkonwnFirst)/4 + a;
            int d =((c*4 + 3) / kMagicUnkonwnSec);
            int e = -d * kMagicUnkonwnSec;
            e = e/4 + c;
            int m = (5*e + 2)/kDaysFromYear;
            tm.tm_mday = -(kDaysFromYear * m + 2)/5 + e + 1;
            tm.tm_mon = (-m/10)*12 + m + 2 + 1;
            tm.tm_year = b*100 + d  - 6700 + (m/10) + 1900;
            return tm;
        }

        static uint64_t sttimetointtime(const struct tm* tm)
        {
            return tm->tm_year*1e10 + tm->tm_mon*1e8 + tm->tm_mday*1e6 +tm->tm_hour*1e4 +tm->tm_min*1e2 + tm->tm_sec;
        }        
    };

    struct SummaryItem
    {
        uint64_t inttime;

        //0 minute , 1 hour, 2 day
        uint32_t periodtype;

        //total items number
        uint64_t totalnumber{0};

        //total delay time
        uint64_t totaldelay{0};

        //average delay time
        double averagedelay{0};

        //max delay time(us)
        uint64_t maxdelay{0};
        
        //max delay time(us)
        uint64_t mindelay{999999999};

        //standard
        double stddelay{0};
    };

    class performance
    {
    public:
        performance(string _tagname)
        {
            tagname = _tagname;
            curItems_1m.reserve(60*24*10);
            curItems_1h.reserve(24*10);
            curItems_1d.reserve(128);

            auto tm = perf::FastSecToDate();
            auto ts = perf::sttimetointtime(&tm);

            ResetItem(curItem_1m,ts,0);
            ResetItem(curItem_1h,ts,1);
            ResetItem(curItem_1d,ts,2);

            cur_day = tm.tm_mday;
            cur_hour = tm.tm_hour;
            cur_minute = tm.tm_min;

            std::thread mdSubReceive(&performance::LoopRun,this);
            mdSubReceive.detach();            
        }

        void Update(const uint64_t& delay_us)
        {
            auto tm = perf::FastSecToDate();
            auto ts = perf::sttimetointtime(&tm);
            if (tm.tm_mday != cur_day)
            {
                curItems_1d.emplace_back(curItem_1d);
                ResetItem(curItem_1d,ts,2);
                cur_day = tm.tm_mday;
            }
            if (tm.tm_hour != cur_hour)
            {
                curItems_1h.emplace_back(curItem_1h);
                ResetItem(curItem_1h,ts,1);
                cur_hour = tm.tm_hour;
            }
            if (tm.tm_min != cur_minute)
            {
                curItems_1m.emplace_back(curItem_1m);
                ResetItem(curItem_1m,ts,0);
                cur_minute = tm.tm_min;
            }

            auto update = [&](SummaryItem& item){
                ++ item.totalnumber;
                item.totaldelay += delay_us;
                item.averagedelay = item.totaldelay/item.totalnumber;
                if (item.maxdelay < delay_us) item.maxdelay = delay_us;
                if (item.mindelay > delay_us) item.mindelay = delay_us;
            };
            update(curItem_1d);
            update(curItem_1h);
            update(curItem_1m);
        }

    private:
        void ResetItem(SummaryItem& item,uint64_t ts,uint32_t periodtype)
        {
            memset(&item,0,sizeof(item));
            item.inttime = ts;
            item.mindelay = 999999999;
            item.periodtype = periodtype;
        }

        void Outputfile()
        {
            auto tm = perf::FastSecToDate();
            auto t = perf::sttimetointtime(&tm);
            auto filename = tagname + '.' + to_string(t) + ".perf";
            ofstream outfile(filename.c_str());
            outfile << "inttime" << ","
                    << "periodtype" << ","
                    << "totalnumber" << ","
                    //<< "totaldelay" << ","
                    << "averagedelay" << ","
                    << "maxdelay" << ","
                    << "mindelay" << ","
                    << "stddelay" << "\n";            
            auto output = [&](SummaryItem& item){
                outfile << item.inttime << ","
                        << item.periodtype << ","
                        << item.totalnumber << ","
                        //<< item.totaldelay << ","
                        << item.averagedelay << ","
                        << item.maxdelay << ","
                        << item.mindelay << ","
                        << item.stddelay << "\n";
            };

            if (curItems_1m.size() > 240)
                curItems_1m.erase(curItems_1m.begin(),curItems_1m.end()-240);

            std::for_each(curItems_1d.begin(), curItems_1d.end(), output);
            std::for_each(curItems_1h.begin(), curItems_1h.end(), output);
            std::for_each(curItems_1m.begin(), curItems_1m.end(), output);

            outfile.close();
        }

        void LoopRun()
        {
            while(1)
            {
                sleep(60*60*4-15);
                Outputfile();
            }
        }

    private:

        uint32_t cur_minute{0};
        uint32_t cur_hour{0};
        uint32_t cur_day{0};

        SummaryItem curItem_1m;
        SummaryItem curItem_1h;
        SummaryItem curItem_1d;
        
        vector<SummaryItem> curItems_1m;
        vector<SummaryItem> curItems_1h;
        vector<SummaryItem> curItems_1d;

        string tagname{"perf"};

    };
}