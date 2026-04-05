#ifndef FMAPMEM_JOURNAL_H
#define FMAPMEM_JOURNAL_H

#include <utility>
#include <mutex>
#include <atomic>
#include <fmapmem/os.h>


namespace fmapmem
{
    namespace journal
    {
        struct journal_header
        {
            uint32_t version{1};
            uint32_t header_length{sizeof(journal_header)};
            uint32_t frame_length{0};
            int64_t create_time{0};
            int64_t flush_time{0};
            char user_reserve[8];
            volatile uint64_t frame_count{0};
    #ifndef _WIN32
        } __attribute__((packed));
    #else
        };
    #pragma pack(pop)
    #endif

        /**
         * Journal class, the abstraction of continuous memory access
         */
        template<typename T>
        class journal
        {
        public:
            journal(const std::string &path, size_t size,bool has_header = false,bool is_writing = false, bool lazy = true);

            virtual ~journal();

            journal_header* header()
            { return header_; }

            T* current_frame()
            { return cur_frame_; }

            T* next_frame();
            /**
             * move current frame to the next available one
             */
            void next();

            bool last(){return (frame_nb_ >= max_frame_nb_);}
            
            size_t max_frame_len() {return max_frame_nb_+1 ;}

            /**  move current frame to frame_nb */
            void moveto(size_t frame_nb);

            T* operator [](size_t frame_nb)
            {
                auto nb = (frame_nb >= max_frame_nb_)? max_frame_nb_:frame_nb;
                return &head_frame_[nb];
            }


        private:
            journal_header* header_;

            /** pointer of the journal */
            uintptr_t root_;
            T* cur_frame_;
            T* head_frame_;
            size_t frame_nb_;
            size_t max_frame_nb_;
            const bool lazy_;
            const size_t size_;
            const bool is_writing_;
            const bool has_header_;
        };

        template<typename T>
        journal<T>::journal(const std::string &path, size_t size,bool has_header,bool is_writing, bool lazy):
                size_(size),is_writing_(is_writing),lazy_(lazy),
                frame_nb_(0),has_header_(has_header)
        {
            root_ = load_mmap_buffer(path,size_,is_writing_,lazy_);
            auto header_buf_len = 0;
            if (has_header_)
            {
                header_ = reinterpret_cast<journal_header*>(root_);
                header_buf_len = sizeof(journal_header);
            }
            else
            {
                header_ = new journal_header();
            }
            auto frame_length = sizeof(T);
            max_frame_nb_ = (size_ - header_buf_len) / frame_length - 1;
            head_frame_ = reinterpret_cast<T*>(root_ + header_buf_len);
            cur_frame_ = head_frame_;
            frame_nb_ = 0;
        }

        template<typename T>
        journal<T>::~journal()
        {
            release_mmap_buffer(root_,size_,lazy_);
        }

        template<typename T>
        void journal<T>::next()
        {
            if (last())
                return ;
            ++ cur_frame_;
            ++ frame_nb_;
        }

        /**no judge */
        template<typename T>
        T* journal<T>::next_frame()
        {
            ++ frame_nb_;
            return ++ cur_frame_;
        }

        template<typename T>
        void journal<T>::moveto(size_t frame_nb)
        {
            auto nb = (frame_nb >= max_frame_nb_)? max_frame_nb_:frame_nb;
            frame_nb_ = nb;
            cur_frame_ = &head_frame_[frame_nb_];
        }        

        template<typename T>
        class reader : public journal<T>
        {
        public:
            explicit reader(const std::string &path, size_t size,bool has_header = true):
            journal<T>(path, size, has_header)
            {};

            ~reader(){};
        };

        template<typename T>
        class single_writer : public journal<T>
        {
        public:
            explicit single_writer(const std::string &path, size_t size,bool has_header = true):
            journal<T>(path,size,has_header,true)
            {};

            ~single_writer(){};
        };

        template<typename T>
        class initialer : public journal<T>
        {
        public:
            explicit initialer(const std::string &path, size_t size,bool has_header = true);

            ~initialer(){};
        };

        template<typename T>
        initialer<T>::initialer(const std::string &path, size_t size,bool has_header):
            journal<T>(path,size,has_header,true)
        {
            auto header_ = this->header();
            header_->version = 1;
            header_->header_length = sizeof(journal_header);
            header_->frame_length = sizeof(T);
            header_->create_time = 0;
            header_->flush_time = 0;
            header_->frame_count = 0;
        }
    }
}
#endif //FMAPMEM_JOURNAL_H
