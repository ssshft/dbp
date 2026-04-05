#pragma once
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
#include <shmpool/include.h>
#include <../include/data_struct.h>

namespace dbs
{
    const uint16_t MAX_DEPTH1_BUF = 256*2*2;

    struct DbsData : sp::RNB{
        // md::MDMsgHeader h;
        char data[MAX_DEPTH1_BUF];
    };

    struct DbsTopic : sp::CNB{
        ExchangeType exchangeTypeEnum;
        // md::SubMarketType subMarketType;
    };
}    