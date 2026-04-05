/////////////////////////////////////////////////////////////////////////////
// IGTrade Trading System
// Copyright 2021 by IGTrade
// All rights reserved.
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <exception>
#include <iostream>
#include <cstring>
using namespace std;

namespace ig{

    class IGException
        : public exception
    {
    public:
        const static size_t MSGSIZE = 1024;
        IGException(const char* msg,int32_t errorcode = 0,int32_t errortype = 0)
            : exception()
        {
            this->errortype = errortype;
            this->errorcode = errorcode;
            strncpy(this->msg,msg,MSGSIZE);
        }

        const char* what() const noexcept override
        {
            return msg;
        }

        const int32_t error_type() const noexcept
        {
            return errortype;
        }

        const int32_t error_code() const noexcept
        {
            return errorcode;
        }

    private:
        char msg[MSGSIZE];
        int32_t errortype{0};
        int32_t errorcode{0};
    };

}