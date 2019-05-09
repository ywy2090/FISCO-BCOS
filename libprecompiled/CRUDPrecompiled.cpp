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
/** @file CRUDPrecompiled.cpp
 *  @author ancelmo
 *  @date 20180921
 */

#include "CRUDPrecompiled.h"

#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/TableFactoryPrecompiled.h"
#include <json/json.h>
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const char* const CRUD_METHOD_INSERT_STR = "insert(string,string,string,string)";
const char* const CRUD_METHOD_REMOVE_STR = "remove(string,string,string,string)";
const char* const CRUD_METHOD_UPDATE_STR = "update(string,string,string,string,string)";
const char* const CRUD_METHOD_SELECT_STR = "select(string,string,string,string)";

CRUDPrecompiled::CRUDPrecompiled()
{
    name2Selector[CRUD_METHOD_INSERT_STR] = getFuncSelector(CRUD_METHOD_INSERT_STR);
    name2Selector[CRUD_METHOD_REMOVE_STR] = getFuncSelector(CRUD_METHOD_REMOVE_STR);
    name2Selector[CRUD_METHOD_UPDATE_STR] = getFuncSelector(CRUD_METHOD_UPDATE_STR);
    name2Selector[CRUD_METHOD_SELECT_STR] = getFuncSelector(CRUD_METHOD_SELECT_STR);
}

std::string CRUDPrecompiled::toString()
{
    return "CRUD";
}

bytes CRUDPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    if (func == name2Selector[CRUD_METHOD_INSERT_STR])
    {  // insert(string tableName, string key, string entry, string optional)
        std::string tableName, key, entryStr, optional;
        abi.abiOut(data, tableName, key, entryStr, optional);
        tableName = storage::USER_TABLE_PREFIX + tableName;
        Table::Ptr table = openTable(context, tableName);
        if (table)
        {
            Entry::Ptr entry = table->newEntry();
            int parseEntryResult = parseEntry(entryStr, entry);
            if (parseEntryResult != CODE_SUCCESS)
            {
                out = abi.abiIn("", u256(parseEntryResult));
                return out;
            }
            int result = table->insert(key, entry, std::make_shared<AccessOptions>(origin));
            out = abi.abiIn("", u256(result));
        }
        else
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                                   << LOG_KV("tableName", tableName);
            out = abi.abiIn("", u256(CODE_TABLE_NOT_EXIST));
        }

        return out;
    }
    if (func == name2Selector[CRUD_METHOD_UPDATE_STR])
    {  // update(string tableName, string key, string entry, string condition, string optional)
        std::string tableName, key, entryStr, conditionStr, optional;
        abi.abiOut(data, tableName, key, entryStr, conditionStr, optional);
        tableName = storage::USER_TABLE_PREFIX + tableName;
        Table::Ptr table = openTable(context, tableName);
        if (table)
        {
            Entry::Ptr entry = table->newEntry();
            int parseEntryResult = parseEntry(entryStr, entry);
            if (parseEntryResult != CODE_SUCCESS)
            {
                out = abi.abiIn("", u256(parseEntryResult));
                return out;
            }
            Condition::Ptr condition = table->newCondition();
            int parseConditionResult = parseCondition(conditionStr, condition);
            if (parseConditionResult != CODE_SUCCESS)
            {
                out = abi.abiIn("", u256(parseConditionResult));
                return out;
            }
            int result =
                table->update(key, entry, condition, std::make_shared<AccessOptions>(origin));
            out = abi.abiIn("", u256(result));
        }
        else
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                                   << LOG_KV("tableName", tableName);
            out = abi.abiIn("", u256(CODE_TABLE_NOT_EXIST));
        }

        return out;
    }
    if (func == name2Selector[CRUD_METHOD_REMOVE_STR])
    {  // remove(string tableName, string key, string condition, string optional)
        std::string tableName, key, conditionStr, optional;
        abi.abiOut(data, tableName, key, conditionStr, optional);
        tableName = storage::USER_TABLE_PREFIX + tableName;
        Table::Ptr table = openTable(context, tableName);
        if (table)
        {
            Condition::Ptr condition = table->newCondition();
            int parseConditionResult = parseCondition(conditionStr, condition);
            if (parseConditionResult != CODE_SUCCESS)
            {
                out = abi.abiIn("", u256(parseConditionResult));
                return out;
            }
            int result = table->remove(key, condition, std::make_shared<AccessOptions>(origin));
            out = abi.abiIn("", u256(result));
        }
        else
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                                   << LOG_KV("tableName", tableName);
            out = abi.abiIn("", u256(CODE_TABLE_NOT_EXIST));
        }

        return out;
    }
    if (func == name2Selector[CRUD_METHOD_SELECT_STR])
    {  // select(string tableName, string key, string condition, string optional)
        std::string tableName, key, conditionStr, optional;
        abi.abiOut(data, tableName, key, conditionStr, optional);
        if (tableName != storage::SYS_TABLES)
        {
            tableName = storage::USER_TABLE_PREFIX + tableName;
        }
        Table::Ptr table = openTable(context, tableName);
        if (table)
        {
            Condition::Ptr condition = table->newCondition();
            int parseConditionResult = parseCondition(conditionStr, condition);
            if (parseConditionResult != CODE_SUCCESS)
            {
                out = abi.abiIn("", u256(parseConditionResult));
                return out;
            }
            auto entries = table->select(key, condition);
            json_spirit::Array records;
            if (entries)
            {
                for (size_t i = 0; i < entries->size(); i++)
                {
                    auto entry = entries->get(i);
                    auto fields = entry->fields();
                    json_spirit::Object record;
                    for (auto iter = fields->begin(); iter != fields->end(); iter++)
                    {
                        record.push_back(json_spirit::Pair(iter->first, iter->second));
                    }
                    records.push_back(record);
                }
            }
            json_spirit::Value value(records);
            std::string str = json_spirit::write_string(value, true);
            out = abi.abiIn("", str);
        }
        else
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("table open error")
                                   << LOG_KV("tableName", tableName);
            out = abi.abiIn("", u256(CODE_TABLE_NOT_EXIST));
        }

        return out;
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        out = abi.abiIn("", u256(CODE_FUNCTION_NOT_EXIST));

        return out;
    }
}

int CRUDPrecompiled::parseCondition(const std::string& conditionStr, Condition::Ptr& condition)
{
    Json::Reader reader;
    Json::Value conditionJson;
    if (!reader.parse(conditionStr, conditionJson))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled")
                               << LOG_DESC("condition json parse error")
                               << LOG_KV("condition", conditionStr);

        return CODE_PARSE_CONDITION_ERROR;
    }
    else
    {
        auto members = conditionJson.getMemberNames();
        Json::Value OPJson;
        for (auto iter = members.begin(); iter != members.end(); iter++)
        {
            OPJson = conditionJson[*iter];
            auto op = OPJson.getMemberNames();
            for (auto it = op.begin(); it != op.end(); it++)
            {
                if (*it == "eq")
                {
                    condition->EQ(*iter, OPJson[*it].asString());
                }
                else if (*it == "ne")
                {
                    condition->NE(*iter, OPJson[*it].asString());
                }
                else if (*it == "gt")
                {
                    condition->GT(*iter, OPJson[*it].asString());
                }
                else if (*it == "ge")
                {
                    condition->GE(*iter, OPJson[*it].asString());
                }
                else if (*it == "lt")
                {
                    condition->LT(*iter, OPJson[*it].asString());
                }
                else if (*it == "le")
                {
                    condition->LE(*iter, OPJson[*it].asString());
                }
                else if (*it == "limit")
                {
                    std::string offsetCount = OPJson[*it].asString();
                    std::vector<std::string> offsetCountList;
                    boost::split(offsetCountList, offsetCount, boost::is_any_of(","));
                    int offset = boost::lexical_cast<int>(offsetCountList[0]);
                    int count = boost::lexical_cast<int>(offsetCountList[1]);
                    condition->limit(offset, count);
                }
                else
                {
                    PRECOMPILED_LOG(ERROR)
                        << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("condition operation undefined")
                        << LOG_KV("operation", *it);

                    return CODE_CONDITION_OPERATION_UNDEFINED;
                }
            }
        }
    }

    return CODE_SUCCESS;
}

int CRUDPrecompiled::parseEntry(const std::string& entryStr, Entry::Ptr& entry)
{
    Json::Value entryJson;
    Json::Reader reader;
    if (!reader.parse(entryStr, entryJson))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("entry json parse error")
                               << LOG_KV("entry", entryStr);

        return CODE_PARSE_ENTRY_ERROR;
    }
    else
    {
        auto memebers = entryJson.getMemberNames();
        for (auto iter = memebers.begin(); iter != memebers.end(); iter++)
        {
            entry->setField(*iter, entryJson[*iter].asString());
        }

        return CODE_SUCCESS;
    }
}
