#pragma once
#include <iostream>

namespace sp
{
    const uint32_t COL_SIZE = 500;
    const uint32_t ROW_SIZE = 500;

    const uint32_t COL_NAME_LEN = 128;
    const uint32_t KEY_NAME_LEN = 60;

     struct RowNodeBase{
        uint64_t __updatestamp{0};
        uint32_t __mnodeid{0};
        volatile char __wflag{'F'};
        char __reserve[7];
#ifndef _WIN32
    } __attribute__((packed));
#else
    };
#pragma pack(pop)
#endif

    struct ColumnNodeBase{
        char __name[COL_NAME_LEN];
        uint32_t __bufsize{0};
        uint32_t __buffrom{0};
        uint32_t __mnodeid{0};
        //void* pdnodehead{nullptr};
        volatile uint64_t __datacount{0};
#ifndef _WIN32
    } __attribute__((packed));
#else
    };
#pragma pack(pop)
#endif

    typedef RowNodeBase RNB;
    typedef ColumnNodeBase CNB;

}