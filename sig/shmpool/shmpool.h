#pragma once
#include <unordered_map>
#include <vector>
#include <chrono>
#include <fmapmem/journal.h> 
#include <protocol/IGException.h>
#include <shmpool/include.h>

using namespace std;
using namespace std::chrono;

namespace sp
{

    template<typename M,typename D>
    class Shm2DArray
    {
    public:
        const unordered_map<string,M*>& GetColumns() const {
            return mnodemap;
        }

        M* GetColumn(const string& mnodename) const {
            const auto got = mnodemap.find(mnodename);
            if (got == mnodemap.end()){
                string s = "Node Not Found, mnodename: " + mnodename;
                // throw ig::IGException(s.c_str());
                return nullptr;
            }

            return got->second;
        }

        M* GetColumnbyID(uint32_t mnodeid) const {
            if (!pheadmnode || mnodeid >= msize){
                string s = "mnodeid Over Length, mnodeid: " + mnodeid;
                // throw ig::IGException(s.c_str());
                return nullptr;
            }
            return pheadmnode+mnodeid;
        }

        D* Data(const string& mnodename,uint32_t offset){
            CNB* pmhead = (CNB*)(GetColumn(mnodename));
            //auto p = reinterpret_cast<D*>(pmhead->pdnodehead);
            auto p = this->pheaddata + pmhead->__buffrom;
            return p + (offset%pmhead->__bufsize);
        }

        D* Data(uint32_t mnodeid,uint32_t offset){
            CNB* pmhead =  (CNB*)(GetColumnbyID(mnodeid));
            //auto p = reinterpret_cast<D*>(pmhead->pdnodehead);
            auto p = this->pheaddata + pmhead->__buffrom;
            return p + (offset%pmhead->__bufsize);
        }

        const uint64_t ColumnCount() const{
            return msize;
        }

        const uint32_t Version() const{
            return pmnodebufhead->header()->version;
        }

    protected:
        Shm2DArray(){           
        }

        ~Shm2DArray(){
            if (pmnodebufhead)
                delete pmnodebufhead;
            if (pdnodebufhead)
                delete pdnodebufhead;
        }

    protected:
        unordered_map<string,M*> mnodemap;
        fmapmem::journal::journal<M>* pmnodebufhead{nullptr};
        fmapmem::journal::journal<D>* pdnodebufhead{nullptr};
        M* pheadmnode{nullptr};
        D* pheaddata{nullptr};
        uint32_t msize{0};
        uint32_t dsize{0};
    };

    template<typename M,typename D>
    class Reader : public Shm2DArray<M,D>
    {
    public:
        explicit Reader(const string& mpath,const string& dpath, uint32_t topicdefaultsize=COL_SIZE):
        Shm2DArray<M,D>()
        {
            fmapmem::journal::reader<M> ptemphead(mpath,sizeof(M)+sizeof(fmapmem::journal::journal_header),true);
            auto header = ptemphead.header();
            auto& msize = this->msize;
            msize = header->frame_count;
            readcounts.resize(msize);

            uint32_t topicsize = msize > topicdefaultsize ? msize : topicdefaultsize;
            uint32_t bufsize = 0;
            this->pmnodebufhead = static_cast<fmapmem::journal::journal<M>*> (new fmapmem::journal::reader<M>(mpath,topicsize*sizeof(M)+sizeof(fmapmem::journal::journal_header),true)); 

            // this->pmnodebufhead = static_cast<fmapmem::journal::journal<M>*> (new fmapmem::journal::reader<M>(mpath,msize*sizeof(M)+sizeof(fmapmem::journal::journal_header),true));          
            this->pheadmnode = this->pmnodebufhead->current_frame();
            for (uint32_t i = 0; i < msize; ++ i){
                CNB* pmnode =  (CNB*)(this->pheadmnode+i);
                this->dsize += pmnode->__bufsize;
                this->mnodemap[pmnode->__name] = this->pheadmnode+i;
                readcounts[i]=pmnode->__datacount;

                bufsize = pmnode->__bufsize;
            }

            uint32_t datasize = topicsize * bufsize;
            this->pdnodebufhead = static_cast<fmapmem::journal::journal<D>*> (new fmapmem::journal::reader<D>(dpath,datasize*sizeof(D)+sizeof(fmapmem::journal::journal_header),true));

            // this->pdnodebufhead = static_cast<fmapmem::journal::journal<D>*> (new fmapmem::journal::reader<D>(dpath,this->dsize*sizeof(D)+sizeof(fmapmem::journal::journal_header),true));
            this->pheaddata = this->pdnodebufhead->current_frame();        
        
        };

        virtual ~Reader(){
        };

        bool Fetch(const string& mnodename,D** data){
            CNB* pmhead = (CNB*)(this->GetColumn(mnodename));
            if (!pmhead){
                string s = "Get nullptr pointer, mnodename: " + mnodename;
                throw ig::IGException(s.c_str());
            }
            auto& readcount = readcounts[pmhead->__mnodeid];
            if (readcount == pmhead->__datacount){
                return false;
            }
            if (readcount > pmhead->__datacount){
                string s = "Invalid datacount, mnodename: " + mnodename;
                throw ig::IGException(s.c_str());
            }

            auto pdhead = this->pheaddata + pmhead->__buffrom;
            auto pnext = pdhead + (readcount%pmhead->__bufsize);
            auto pnextbase = (RNB*)(pnext);
            if (pnextbase->__wflag == 'T'){   //conflict with writer,wait
                return false;
            }
            *data = pnext;
            ++ readcount;

            return true;
        }

        bool Fetch(const uint32_t mnodeid,D** data){
            CNB* pmhead = (CNB*)(this->GetColumnbyID(mnodeid));
            if (!pmhead){
                string s = "Get nullptr pointer, mnodeid: " + mnodeid;
                throw ig::IGException(s.c_str());                
            }            
            auto& readcount = readcounts[pmhead->__mnodeid];
            if (readcount == pmhead->__datacount){
                return false;
            }
            if (readcount > pmhead->__datacount){
                string s = "Invalid datacount, mnodeid: " + mnodeid;
                throw ig::IGException(s.c_str());
            }

            auto pdhead = this->pheaddata + pmhead->__buffrom;
            auto pnext = pdhead + (readcount%pmhead->__bufsize);
            auto pnextbase = (RNB*)(pnext);
            if (pnextbase->__wflag == 'T'){   //conflict with writer,waiting
                return false;
            }
            *data = pnext;
            ++ readcount;

            return true;
        }

        //return:skipped number, 0:means it already the lastest data.
        uint32_t FetchLast(const uint32_t mnodeid,D** data){
            CNB* pmhead = (CNB*)(this->GetColumnbyID(mnodeid));
            if (!pmhead){
                string s = "Get nullptr pointer, mnodeid: " + mnodeid;
                throw ig::IGException(s.c_str());                
            }
            auto& readcount = readcounts[pmhead->__mnodeid];
            if (readcount == pmhead->__datacount){
                return 0;
            }
            if (readcount > pmhead->__datacount){
                string s = "Invalid datacount, mnodeid: " + mnodeid;
                throw ig::IGException(s.c_str());
            }

            auto skippednum = pmhead->__datacount - readcount;
            auto pdhead = this->pheaddata + pmhead->__buffrom;
            auto pnext = pdhead + (pmhead->__datacount-1)%pmhead->__bufsize;
            auto pnextbase = (RNB*)(pnext);
            if (pnextbase->__wflag == 'T'){   //conflict with writer,waiting
                if (skippednum <= 1)
                    return 0;
                -- skippednum;  //get prefer data
                pnext = pdhead + (pmhead->__datacount-2)%pmhead->__bufsize;
                //this wflag woudln't be 'T'
            }
            *data = pnext;
            readcount += skippednum;

            return skippednum;
        }   

        //return:skipped number, 0:means it already the lastest data.
        uint32_t FetchLast(const string& mnodename,D** data){
            CNB* pmhead = static_cast<CNB*>(this->GetColumn(mnodename));
            if (!pmhead){
                string s = "Get nullptr pointer, mnodename: " + mnodename;
                throw ig::IGException(s.c_str());
            }
            auto& readcount = readcounts[pmhead->__mnodeid];
            if (readcount == pmhead->__datacount){
                return 0;
            }            
            if (readcount > pmhead->__datacount){
                string s = "Invalid datacount, mnodename: " + mnodename;
                throw ig::IGException(s.c_str());
            }

            auto skippednum = pmhead->__datacount - readcount;
            auto pdhead = this->pheaddata + pmhead->__buffrom;
            auto pnext = pdhead + (pmhead->__datacount-1)%pmhead->__bufsize;
            auto pnextbase = (RNB*)(pnext);
            if (pnextbase->__wflag == 'T'){   //conflict with writer,waiting
                if (skippednum <= 1)
                    return 0;
                -- skippednum;  //get prefer data
                pnext = pdhead + (pmhead->__datacount-2)%pmhead->__bufsize;
                //this wflag woudln't be 'T'
            }
            *data = pnext;
            readcount += skippednum;

            return skippednum;
        }
        
        bool UpdateByHeader() {
            auto header_ = this->pmnodebufhead->header();
            int newMsize = header_->frame_count;
            auto& msize = this->msize;
            if (newMsize <= msize) {
                return false;
            }

            if (newMsize > 10000) { // 默认共享列个数小于10000
                return false;
            }

            for (int i = msize; i < newMsize; ++i) {
                CNB* pmode = (CNB*)(this->pheadmnode + i);
                this->dsize += pmode->__bufsize;
                this->mnodemap[pmode->__name] = this->pheadmnode + i;
                uint64_t datacount = pmode->__datacount;
                readcounts.push_back(datacount);
            }

            msize = newMsize;
            return true;
        }

    private:
        std::vector<uint64_t> readcounts;
    };

    template<typename M,typename D>
    class Writer : public Shm2DArray<M,D>
    {
    public:
        explicit Writer(const string& mpath,const string& dpath, uint32_t topicdefaultsize=COL_SIZE):
        Shm2DArray<M,D>()
        {
            fmapmem::journal::reader<M> ptemphead(mpath,sizeof(M)+sizeof(fmapmem::journal::journal_header),true);
            auto header = ptemphead.header();
            this->msize = header->frame_count;

            uint32_t topicsize = this->msize > topicdefaultsize ? this->msize : topicdefaultsize;
            uint32_t bufsize = 0;
            this->pmnodebufhead = static_cast<fmapmem::journal::journal<M>*> (new fmapmem::journal::single_writer<M>(mpath,topicsize*sizeof(M)+sizeof(fmapmem::journal::journal_header),true));

            // this->pmnodebufhead = static_cast<fmapmem::journal::journal<M>*> (new fmapmem::journal::single_writer<M>(mpath,this->msize*sizeof(M)+sizeof(fmapmem::journal::journal_header),true));
            this->pheadmnode = this->pmnodebufhead->current_frame();
            for (uint32_t i = 0; i < this->msize; ++ i){
                CNB* pmnode =  (CNB*)(this->pheadmnode+i);
                this->dsize += pmnode->__bufsize;
                this->mnodemap[pmnode->__name] = (this->pheadmnode+i);

                bufsize = pmnode->__bufsize;
            }

            uint32_t datasize = topicsize * bufsize;
            this->pdnodebufhead = static_cast<fmapmem::journal::journal<D>*> (new fmapmem::journal::single_writer<D>(dpath,datasize*sizeof(D)+sizeof(fmapmem::journal::journal_header),true));

            // this->pdnodebufhead = static_cast<fmapmem::journal::journal<D>*> (new fmapmem::journal::single_writer<D>(dpath,this->dsize*sizeof(D)+sizeof(fmapmem::journal::journal_header),true));
            this->pheaddata = this->pdnodebufhead->current_frame();
        };

        virtual ~Writer(){};

        D* Fetch(const string& mnodename){
            CNB* pmhead = (CNB*)(this->GetColumn(mnodename));
            if (!pmhead){
                string s = "Get nullptr pointer, mnodename: " + mnodename;
                throw ig::IGException(s.c_str());                
            }            
            auto pdhead = this->pheaddata+pmhead->__buffrom;
            auto offset = pmhead->__datacount;
            auto pnext = pdhead + (offset%pmhead->__bufsize);
            auto pnextbase = (RNB*)(pnext);
            pnextbase->__wflag = 'T';
            pnextbase->__mnodeid = pmhead->__mnodeid;
            return pnext;
        }

        D* Fetch(uint32_t mnodeid){
            CNB* pmhead = (CNB*)(this->GetColumnbyID(mnodeid));
            if (!pmhead){
                string s = "Get nullptr pointer, mnodeid: " + mnodeid;
                throw ig::IGException(s.c_str());                
            }            
            auto pdhead = this->pheaddata + pmhead->__buffrom;
            auto offset = pmhead->__datacount;
            auto pnext = pdhead + (offset%pmhead->__bufsize);
            auto pnextbase = (RNB*)(pnext);
            pnextbase->__wflag = 'T';
            pnextbase->__mnodeid = pmhead->__mnodeid;
            return pnext;
        }

        void Commit(uint32_t mnodeid){
            CNB* pmhead = static_cast<CNB*>(this->GetColumnbyID(mnodeid));
            if (!pmhead){
                string s = "Get nullptr pointer, mnodeid: " + mnodeid;
                throw ig::IGException(s.c_str());
            }
            auto pdhead = this->pheaddata + pmhead->__buffrom;            
            auto pnext = pdhead + (pmhead->__datacount%pmhead->__bufsize);
            auto pnextbase = (RNB*)(pnext);
            pnextbase->__updatestamp = high_resolution_clock::now().time_since_epoch().count();
            pnextbase->__wflag = 'F';
            pmhead->__datacount += 1;
        }

        void AddNewColumn(const M& m) {
            auto header_ = this->pmnodebufhead->header();
            memcpy(this->pheadmnode + this->msize, &m, sizeof(M));
            CNB* pmnode = (CNB*)(this->pheadmnode + this->msize);
            pmnode->__mnodeid = this->msize;
            pmnode->__buffrom = this->dsize;
            this->mnodemap[pmnode->__name] = (this->pheadmnode + this->msize);
            this->dsize += pmnode->__bufsize;
            this->msize += 1;
            header_->frame_count += 1;
        }

        void AddNewColumns(const M& m1, const M& m2) {
            auto header_ = this->pmnodebufhead->header();

            memcpy(this->pheadmnode + this->msize, &m1, sizeof(M));
            CNB* pmnode1 = (CNB*)(this->pheadmnode + this->msize);
            pmnode1->__mnodeid = this->msize;
            pmnode1->__buffrom = this->dsize;
            this->mnodemap[pmnode1->__name] = (this->pheadmnode + this->msize);
            this->dsize += pmnode1->__bufsize;
            this->msize += 1;

            memcpy(this->pheadmnode + this->msize, &m2, sizeof(M));
            CNB* pmnode2 = (CNB*)(this->pheadmnode + this->msize);
            pmnode2->__mnodeid = this->msize;
            pmnode2->__buffrom = this->dsize;
            this->mnodemap[pmnode2->__name] = (this->pheadmnode + this->msize);
            this->dsize += pmnode2->__bufsize;
            this->msize += 1;

            header_->frame_count += 2;
        }

        void AddNewColumns(string symbol1, const M& m1, string symbol2, const M& m2) {
            auto header_ = this->pmnodebufhead->header();
            
            bool flag1 = false;
            if (this->mnodemap.find(symbol1) == this->mnodemap.end()) {
                memcpy(this->pheadmnode + this->msize, &m1, sizeof(M));
                CNB* pmnode1 = (CNB*)(this->pheadmnode + this->msize);
                pmnode1->__mnodeid = this->msize;
                pmnode1->__buffrom = this->dsize;
                this->mnodemap[pmnode1->__name] = (this->pheadmnode + this->msize);
                this->dsize += pmnode1->__bufsize;
                this->msize += 1;
                flag1 = true;  
            }

            bool flag2 = false;
            if (this->mnodemap.find(symbol2) == this->mnodemap.end()) {
                memcpy(this->pheadmnode + this->msize, &m2, sizeof(M));
                CNB* pmnode2 = (CNB*)(this->pheadmnode + this->msize);
                pmnode2->__mnodeid = this->msize;
                pmnode2->__buffrom = this->dsize;
                this->mnodemap[pmnode2->__name] = (this->pheadmnode + this->msize);
                this->dsize += pmnode2->__bufsize;
                this->msize += 1;
                flag2 = true;  
            }

            if (flag1 && flag2) {
                header_->frame_count += 2;
            }
            else if (flag1 || flag2) {
                header_->frame_count += 1;
            }
        }

        void AddNewColumns(const unordered_map<string, M>& mNew) {
            auto header_ = this->pmnodebufhead->header();
            int updateCount = 0;
            for (auto iter = mNew.begin(); iter != mNew.end(); ++iter) {
                const string& channel = iter->first;
                const M& m = iter->second;
              
                if (this->mnodemap.find(channel) == this->mnodemap.end()) {
                    memcpy(this->pheadmnode + this->msize, &m, sizeof(M));
                    CNB* pmnode = (CNB*)(this->pheadmnode + this->msize);
                    pmnode->__mnodeid = this->msize;
                    pmnode->__buffrom = this->dsize;
                    this->mnodemap[pmnode->__name] = (this->pheadmnode + this->msize);
                    this->dsize += pmnode->__bufsize;
                    this->msize += 1;
                    updateCount += 1;  
                }
            }

            header_->frame_count += updateCount;
        }
        
    };

    template<typename M,typename D>
    class Initialer : public Shm2DArray<M,D>
    {
    public:
        explicit Initialer(const string& mpath,uint32_t msize,const string& dpath,uint32_t dsize,const vector<M>* initvc,uint32_t version, uint32_t topicdefaultsize=COL_SIZE, uint32_t datadefaultsize=ROW_SIZE):
        Shm2DArray<M,D>()
        {
            this->msize = msize;
            this->dsize = dsize;

            uint32_t topicsize = this->msize > topicdefaultsize ? this->msize : topicdefaultsize;
            uint32_t datasize = this->dsize > datadefaultsize ? this->dsize : datadefaultsize;

            this->pmnodebufhead = static_cast<fmapmem::journal::journal<M>*> (new fmapmem::journal::initialer<M>(mpath,topicsize*sizeof(M)+sizeof(fmapmem::journal::journal_header),true));
            this->pdnodebufhead = static_cast<fmapmem::journal::journal<D>*> (new fmapmem::journal::initialer<D>(dpath,datasize*sizeof(D)+sizeof(fmapmem::journal::journal_header),true));
            this->pheadmnode = this->pmnodebufhead->current_frame();
            memset(this->pheadmnode,0,topicsize*sizeof(M));
            this->pheaddata = this->pdnodebufhead->current_frame();
            memset(this->pheaddata,0,datasize*sizeof(D));


            // this->pmnodebufhead = static_cast<fmapmem::journal::journal<M>*> (new fmapmem::journal::initialer<M>(mpath,msize*sizeof(M)+sizeof(fmapmem::journal::journal_header),true));
            // this->pdnodebufhead = static_cast<fmapmem::journal::journal<D>*> (new fmapmem::journal::initialer<D>(dpath,dsize*sizeof(D)+sizeof(fmapmem::journal::journal_header),true));
            // this->pheadmnode = this->pmnodebufhead->current_frame();
            // memset(this->pheadmnode,0,msize*sizeof(M));
            // this->pheaddata = this->pdnodebufhead->current_frame();
            // memset(this->pheaddata,0,dsize*sizeof(D));

            uint32_t buffrom = 0;
            for (size_t i =0; i < msize;++ i){
                memcpy(this->pheadmnode+i,&(*initvc)[i],sizeof(M));
                CNB* pmnode =  (CNB*)(this->pheadmnode+i);
                //strncpy(pmnode->__name,(*initvc)[i].__name,32);
                //pmnode->__bufsize = (*initvc)[i].__bufsize;                
                pmnode->__mnodeid = i;
                pmnode->__buffrom = buffrom;
                buffrom += pmnode->__bufsize;
                //pmnode->pdnodehead = pcur;
                //pcur += pmnode->__bufsize;
                //mnodemap[pmhead->__name] = (*pmnodebufhead)[i];
            }
            auto header_ = this->pmnodebufhead->header();
            header_->version = version;
            header_->create_time = chrono::system_clock::to_time_t(chrono::system_clock::now());
            header_->flush_time = 0;
            header_->frame_count = msize;
        };

        virtual ~Initialer(){};
    };
};