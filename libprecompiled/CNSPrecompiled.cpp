/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file CNSPrecompiled.cpp
 *  @author chaychen
 *  @date 20181119
 */
#include "CNSPrecompiled.h"

#include <json_spirit/JsonSpiritHeaders.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libstorage/EntriesPrecompiled.h>
#include <libstorage/TableFactoryPrecompiled.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const char* const CNS_METHOD_INS_STR4 = "insert(string,string,string,string)";
const char* const CNS_METHOD_SLT_STR = "selectByName(string)";
const char* const CNS_METHOD_SLT_STR2 = "selectByNameAndVersion(string,string)";

CNSPrecompiled::CNSPrecompiled()
{
    name2Selector[CNS_METHOD_INS_STR4] = getFuncSelector(CNS_METHOD_INS_STR4);
    name2Selector[CNS_METHOD_SLT_STR] = getFuncSelector(CNS_METHOD_SLT_STR);
    name2Selector[CNS_METHOD_SLT_STR2] = getFuncSelector(CNS_METHOD_SLT_STR2);
}


std::string CNSPrecompiled::toString()
{
    return "CNS";
}

bytes CNSPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    if (func == name2Selector[CNS_METHOD_INS_STR4])
    {
        // insert(string,string,string,string)
        // insert(name, version, address, abi), 4 fields in table, the key of table is name field
        std::string contractName, contractVersion, contractAddress, contractAbi;
        abi.abiOut(data, contractName, contractVersion, contractAddress, contractAbi);
        Table::Ptr table = openTable(context, SYS_CNS);

        // check exist or not
        bool exist = false;
        auto entries = table->select(contractName, table->newCondition());
        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (!entry)
                    continue;
                if (entry->getField(SYS_CNS_FIELD_VERSION) == contractVersion)
                {
                    exist = true;
                    break;
                }
            }
        }
        int result = 0;
        if (exist)
        {
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address and version exist");
            result = CODE_ADDRESS_AND_VERSION_EXIST;
        }
        else
        {
            // do insert
            auto entry = table->newEntry();
            entry->setField(SYS_CNS_FIELD_NAME, contractName);
            entry->setField(SYS_CNS_FIELD_VERSION, contractVersion);
            entry->setField(SYS_CNS_FIELD_ADDRESS, contractAddress);
            entry->setField(SYS_CNS_FIELD_ABI, contractAbi);
            int count = table->insert(contractName, entry, std::make_shared<AccessOptions>(origin));
            if (count == storage::CODE_NO_AUTHORIZED)
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("CNSPrecompiled") << LOG_DESC("permission denied");
                result = storage::CODE_NO_AUTHORIZED;
            }
            else
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("CNSPrecompiled") << LOG_DESC("insert successfully");
                result = count;
            }
        }
        getErrorCodeOut(out, result);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR])
    {
        // selectByName(string) returns(string)
        // Cursor is not considered.
        std::string contractName;
        abi.abiOut(data, contractName);
        Table::Ptr table = openTable(context, SYS_CNS);

        json_spirit::Array CNSInfos;
        auto entries = table->select(contractName, table->newCondition());
        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (!entry)
                    continue;
                json_spirit::Object CNSInfo;
                CNSInfo.push_back(json_spirit::Pair(SYS_CNS_FIELD_NAME, contractName));
                CNSInfo.push_back(json_spirit::Pair(
                    SYS_CNS_FIELD_VERSION, entry->getField(SYS_CNS_FIELD_VERSION)));
                CNSInfo.push_back(json_spirit::Pair(
                    SYS_CNS_FIELD_ADDRESS, entry->getField(SYS_CNS_FIELD_ADDRESS)));
                CNSInfo.push_back(
                    json_spirit::Pair(SYS_CNS_FIELD_ABI, entry->getField(SYS_CNS_FIELD_ABI)));
                CNSInfos.push_back(CNSInfo);
            }
        }
        json_spirit::Value value(CNSInfos);
        std::string str = json_spirit::write_string(value, true);
        out = abi.abiIn("", str);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR2])
    {
        // selectByNameAndVersion(string,string) returns(string)
        std::string contractName, contractVersion;
        abi.abiOut(data, contractName, contractVersion);
        Table::Ptr table = openTable(context, SYS_CNS);

        json_spirit::Array CNSInfos;
        auto entries = table->select(contractName, table->newCondition());
        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (contractVersion == entry->getField(SYS_CNS_FIELD_VERSION))
                {
                    json_spirit::Object CNSInfo;
                    CNSInfo.push_back(json_spirit::Pair(SYS_CNS_FIELD_NAME, contractName));
                    CNSInfo.push_back(json_spirit::Pair(
                        SYS_CNS_FIELD_VERSION, entry->getField(SYS_CNS_FIELD_VERSION)));
                    CNSInfo.push_back(json_spirit::Pair(
                        SYS_CNS_FIELD_ADDRESS, entry->getField(SYS_CNS_FIELD_ADDRESS)));
                    CNSInfo.push_back(
                        json_spirit::Pair(SYS_CNS_FIELD_ABI, entry->getField(SYS_CNS_FIELD_ABI)));
                    CNSInfos.push_back(CNSInfo);
                    // Only one
                    break;
                }
            }
        }
        json_spirit::Value value(CNSInfos);
        std::string str = json_spirit::write_string(value, true);
        out = abi.abiIn("", str);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call undefined function")
                               << LOG_KV("func", func);
    }

    return out;
}
