#pragma once
#include <shmpool/shmpool.h>
#include <dbp/include.h>


namespace dbp {
	typedef std::function<void(dbp::DbpTopic* topic, dbp::DbpData* data, uint32_t jumpedNum)> dbpcallback;

    class DbpReader {
    public:
        explicit DbpReader(const std::string& mpath, const std::string& dpath) :spreader(mpath,dpath) {
            auto& l = spreader.GetColumns();
            _pcols = &l;
        }
        
        ~DbpReader() {}

        void SetCallback(dbpcallback ck) {
            _dbpcallback = ck;
        }

        void Subscribe(const std::vector<std::string>& topics) {
            for (uint32_t i=0; i < topics.size(); ++i) {
                Subscribe(topics[i]);
            }
        }

        bool Subscribe(const std::string& topic) {
            UpdateByHeader(); // 订阅时更新

            auto iter = _pcols->find(topic);
            if (iter != _pcols->end()){
                submap[iter->second->__mnodeid] = 1;
                return true;
            }
            return false;
        }

        void UnSubscribe(const string& topic){
            auto iter = _pcols->find(topic);
            if (iter != _pcols->end()){
                submap[iter->second->__mnodeid] = 0;
            }
        }

        void FetchLast(){
            static DbpData* data{nullptr};
            static DbpTopic* topic{nullptr};
            static uint32_t jumppednum = 0;
            auto iter = submap.begin();
            while (iter != submap.end()) {
                if (iter->second != 1) {
                    submap.erase(iter++);
                    continue;
                }
                jumppednum = spreader.FetchLast(iter->first, &data);
                if (jumppednum > 0){
                    topic = spreader.GetColumnbyID(iter->first);
                    _dbpcallback(topic, data, jumppednum);
                }
                ++iter;
            }
        }

        void FetchNext(){
            static DbpData* data{nullptr};
            static DbpTopic* topic{nullptr};
            static bool jumpped = false;
            auto iter = submap.begin();
            while(iter != submap.end()) {
                if (iter->second != 1) {
                    submap.erase(iter++);
                    continue;
                }
                jumpped = spreader.Fetch(iter->first, &data);
                if (jumpped) {
                    topic = spreader.GetColumnbyID(iter->first);
                    _dbpcallback(topic, data, 1);
                }
                ++iter;
            }
        }

        void UpdateByHeader() {
            if (spreader.UpdateByHeader()) {
                auto& l = spreader.GetColumns();
                _pcols = &l;
            }
        }

    private:
        sp::Reader<DbpTopic, DbpData> spreader;
        const std::unordered_map<std::string, dbp::DbpTopic*>* _pcols{nullptr};
        std::unordered_map<uint32_t, uint32_t> submap;
        dbpcallback _dbpcallback{nullptr};
    };

};