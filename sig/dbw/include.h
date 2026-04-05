#pragma once
#include <shmpool/include.h>
#include <../include/data_struct.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>

namespace dbw
{
    const uint32_t DB_COL_SIZE = 3000;
    struct DBdata : sp::RNB{

        md::CryptoMarketData d;
    };

    struct DBTopic : sp::CNB{
        md::MarketType t;
        md::CryptoMarketData d;
        md::InstrumentInfo i;
    };

    extern string MakeDBPSymbolKey(const string& exchId,const string& instType,const char* instId);
}    