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
/** @file TransferPrecompiled.cpp
 *  @author octopuswang
 *  @date 20190411
 */
#include "TransferPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libethcore/Exceptions.h>
#include <libstorage/EntriesPrecompiled.h>
#include <libstorage/TableFactoryPrecompiled.h>
#include <boost/algorithm/string.hpp>

using namespace dev;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// ================== USER TABLE BEGIN ==================================
// |----|------|--------------|--------|---------|-------|
// | ID | name | account_list | status | address | phone |
// |----|------|--------------|--------|---------|-------|
//  User status: CREATE  USABLE  CLOSE

//  fields:
const std::string BENCH_TRANSFER_USER = "_ext_tranfser_user_";
const std::string BENCH_TRANSFER_USER_FILED_ID = "ID";
const std::string BENCH_TRANSFER_USER_FILED_NAME = "name";
const std::string BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST = "account_list";
const std::string BENCH_TRANSFER_USER_FILED_STATUS = "status";
const std::string BENCH_TRANSFER_USER_FILED_ADDRESS = "address";
const std::string BENCH_TRANSFER_USER_FILED_PHONE = "phone";

// User status list:
const std::string BENCH_TRANSFER_USER_STATUS_CREATE = "create";
const std::string BENCH_TRANSFER_USER_STATUS_USABLE = "usable";
const std::string BENCH_TRANSFER_USER_STATUS_CLOSED = "closed";
// ================== USER TABLE END ====================================

// ================== ACCOUNT TABLE BEGIN ===============================
// |----|----------|--------|---------|------|------------|
// | ID |  userID  | status | balance | time | flow_count |
// |----|----------|--------|---------|------|------------|
//  Account status: CREATE  USABLE  FREEZE  CLOSE
//  fields:
const std::string BENCH_TRANSFER_ACCOUNT = "_ext_transfer_account_";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_ID = "ID";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_USER_ID = "userID";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_STATUS = "status";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_BALANCE = "balance";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT = "flow_count";

// status list:
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_CREATE = "create";
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_USABLE = "usable";
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE = "freeze";
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED = "closed";
// ================== ACCOUNT TABLE END =================================

// ================== ACCOUNT FLOW TABLE BEGIN ==========================
// |-------|----|------|-----|--------|------|
// | index | ID | from |  to | amount | time |
// |-------|----|------|-----|--------|------|
// fields:
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_PREFIX = "_ext_account_flow_";
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_INDEX = "index";
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_ID = "ID";
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_FROM = "from";
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TO = "to";
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_AMOUNT = "amount";
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME = "time";
// ================== ACCOUNT FLOW TABLE END ============================

/*
pragma solidity >=0.4.19 <0.7.0;
pragma experimental ABIEncoderV2;

contract Transfer
{
    function createUser(string memory userID, string memory userName) public
returns(int256) {}

    function createUser0(string memory userID, string memory userName) public
returns(int256) {}

    function createEnabledUser(string memory userID, string memory userName)
public returns(int256) {}

    function closeUser(string memory userID) public returns(int256) {}

    function enableUser(string memory userID) public returns(int256) {}

    function updateUserAddress(string memory userID, string memory userAddr) public
returns(int256) {}

    function updateUserPhone(string memory userID, string memory userPhone) public
returns(int256) {}

    function queryUserStatus(string memory userID) view public returns(int256, string memory) {}

    function queryUserState(string memory userID) view public returns(int256, string memory,string
memory,string memory,string memory) {}

    function createAccount(string memory accountID, string memory userID) public
returns(int256) {}

    function enableAccount(string memory accountID) public returns(int256) {}

    function freezeAccount(string memory accountID) public returns(int256) {}

    function unfreezeAccount(string memory accountID) public returns(int256) {}

    function closeAccount(string memory accountID) public returns(int256) {}

    function queryAccountStatus(string memory accountID) public view returns(int256, string
memory) {}

    function queryAccountState(string memory accountID) public view returns(int256, string
memory, string memory, string memory) {}

    function balance(string memory accountID) public view returns(int256, uint256) {}

    function deposit(string memory accountID, uint256 amount) public returns(int256) {}

    function withDraw(string memory accountID, uint256 amount) public returns(int256) {}

    function transfer(string memory fromAccountID, string memory toAccountID,uint256 amount, string
memory flowID, string memory time) public returns(int256) {}

    function queryFlow(string memory accountID, uint256 index) public
returns(int256, string memory) {}

    function queryFlow(string memory accountID, string memory start, string
memory end) public returns(int256, string[] memory) {}
}
*/

const char* const BENCH_METHOD_CREATE_USER_STR3 = "createUser(string,string)";
const char* const BENCH_METHOD_CREATE0_USER_STR3 = "createUser0(string,string)";
const char* const BENCH_METHOD_CREATE_ENABLED_USER_STR3 = "createEnabledUser(string,string)";
const char* const BENCH_METHOD_CLOSE_USER_STR2 = "closeUser(string)";
const char* const BENCH_METHOD_ENABLE_USER_STR2 = "enableUser(string)";
const char* const BENCH_METHOD_UPDATE_ADDR_USER_STR3 = "updateUserAddress(string,string)";
const char* const BENCH_METHOD_UPDATE_PHONE_USR_STR3 = "updateUserPhone(string,string)";
const char* const BENCH_METHOD_QUERY_USER_STATUS_STR = "queryUserStatus(string)";
const char* const BENCH_METHOD_QUERY_USER_STATE = "queryUserState(string)";

const char* const BENCH_METHOD_CREATE_ACCOUNT_STR3 = "createAccount(string,string)";
const char* const BENCH_METHOD_ENABLE_ACCOUNT_STR2 = "enableAccount(string)";
const char* const BENCH_METHOD_FREEZE_ACCOUNT_STR2 = "freezeAccount(string)";
const char* const BENCH_METHOD_UNFREEZE_ACCOUNT_STR3 = "unfreezeAccount(string)";
const char* const BENCH_METHOD_CLOSE_ACCOUNT_STR2 = "closeAccount(string)";
const char* const BENCH_METHOD_QUERY_ACCOUNT_STATUS_STR = "queryAccountStatus(string)";
const char* const BENCH_METHOD_QUERY_ACCOUNT_STATE_STR = "queryAccountState(string)";
const char* const BENCH_METHOD_BALANCE_ACCOUNT_STR = "balance(string)";
const char* const BENCH_METHOD_DEPOSIT_ACCOUNT_STR3 = "deposit(string,uint256)";
const char* const BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3 = "withDraw(string,uint256)";
const char* const BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2 =
    "transfer(string,string,uint256,string,string)";
const char* const BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT = "queryFlow(string,uint256)";
const char* const BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3 = "queryFlow(string,string,string)";

// common
const static int CODE_TRANFSER_INVALID_UNKOWN_FUNC_CALL = 51500;
const static int CODE_TRANFSER_INVALID_INVALID_PARAMS = 51501;

// table operation
const static int CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED = 51601;
const static int CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED = 51602;
const static int CODE_TRANFSER_INVALID_OPEN_FLOW_TABLE_FAILED = 51603;
const static int CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED = 51604;
const static int CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED = 51605;
const static int CODE_TRANFSER_INVALID_FLOW_TABLE_NO_AUTHORIZED = 51606;

// user table
const static int CODE_TRANFSER_INVALID_USER_EXIST = 51701;
const static int CODE_TRANFSER_INVALID_USER_NT_EXIST = 51702;
const static int CODE_TRANFSER_INVALID_USER_INVALID_STATUS = 51703;
const static int CODE_TRANFSER_INVALID_USER_ACCOUNT_INVALID_STATUS = 51704;

// account table
const static int CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST = 51801;
const static int CODE_TRANFSER_INVALID_ACCOUNT_EXIST = 51802;
const static int CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS = 51803;
// const static int CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_NOT_ZERO = 51804;
const static int CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_OVERFLOW = 51805;
const static int CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_INSUFFICIENT = 51806;
const static int CODE_TRANFSER_INVALID_ACCOUNT_FLOW_NOT_EXIST = 51807;

TransferPrecompiled::TransferPrecompiled()
{
    name2Selector[BENCH_METHOD_CREATE_USER_STR3] = getFuncSelector(BENCH_METHOD_CREATE_USER_STR3);
    name2Selector[BENCH_METHOD_CREATE0_USER_STR3] = getFuncSelector(BENCH_METHOD_CREATE0_USER_STR3);
    name2Selector[BENCH_METHOD_CREATE_ENABLED_USER_STR3] =
        getFuncSelector(BENCH_METHOD_CREATE_ENABLED_USER_STR3);
    name2Selector[BENCH_METHOD_CLOSE_USER_STR2] = getFuncSelector(BENCH_METHOD_CLOSE_USER_STR2);
    name2Selector[BENCH_METHOD_ENABLE_USER_STR2] = getFuncSelector(BENCH_METHOD_ENABLE_USER_STR2);
    name2Selector[BENCH_METHOD_UPDATE_ADDR_USER_STR3] =
        getFuncSelector(BENCH_METHOD_UPDATE_ADDR_USER_STR3);
    name2Selector[BENCH_METHOD_UPDATE_PHONE_USR_STR3] =
        getFuncSelector(BENCH_METHOD_UPDATE_PHONE_USR_STR3);
    name2Selector[BENCH_METHOD_QUERY_USER_STATUS_STR] =
        getFuncSelector(BENCH_METHOD_QUERY_USER_STATUS_STR);
    name2Selector[BENCH_METHOD_QUERY_USER_STATE] = getFuncSelector(BENCH_METHOD_QUERY_USER_STATE);
    name2Selector[BENCH_METHOD_CREATE_ACCOUNT_STR3] =
        getFuncSelector(BENCH_METHOD_CREATE_ACCOUNT_STR3);
    name2Selector[BENCH_METHOD_ENABLE_ACCOUNT_STR2] =
        getFuncSelector(BENCH_METHOD_ENABLE_ACCOUNT_STR2);
    name2Selector[BENCH_METHOD_FREEZE_ACCOUNT_STR2] =
        getFuncSelector(BENCH_METHOD_FREEZE_ACCOUNT_STR2);
    name2Selector[BENCH_METHOD_UNFREEZE_ACCOUNT_STR3] =
        getFuncSelector(BENCH_METHOD_UNFREEZE_ACCOUNT_STR3);
    name2Selector[BENCH_METHOD_CLOSE_ACCOUNT_STR2] =
        getFuncSelector(BENCH_METHOD_CLOSE_ACCOUNT_STR2);
    name2Selector[BENCH_METHOD_QUERY_ACCOUNT_STATUS_STR] =
        getFuncSelector(BENCH_METHOD_QUERY_ACCOUNT_STATUS_STR);
    name2Selector[BENCH_METHOD_QUERY_ACCOUNT_STATE_STR] =
        getFuncSelector(BENCH_METHOD_QUERY_ACCOUNT_STATE_STR);
    name2Selector[BENCH_METHOD_BALANCE_ACCOUNT_STR] =
        getFuncSelector(BENCH_METHOD_BALANCE_ACCOUNT_STR);
    name2Selector[BENCH_METHOD_DEPOSIT_ACCOUNT_STR3] =
        getFuncSelector(BENCH_METHOD_DEPOSIT_ACCOUNT_STR3);
    name2Selector[BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3] =
        getFuncSelector(BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3);
    name2Selector[BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2] =
        getFuncSelector(BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2);
    name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3] =
        getFuncSelector(BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3);
    name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT] =
        getFuncSelector(BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT);
}

std::string TransferPrecompiled::toString()
{
    return "TransferPrecompiled";
}

bytes TransferPrecompiled::call(dev::blockverifier::ExecutiveContext::Ptr _context,
    bytesConstRef _param, Address const& _origin)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    bytes out;
    if (func == name2Selector[BENCH_METHOD_CREATE_USER_STR3])
    {
        out = createUser(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_CREATE0_USER_STR3])
    {
        out = createUser0(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_CREATE_ENABLED_USER_STR3])
    {
        out = createEnabledUser(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_CLOSE_USER_STR2])
    {
        out = closeUser(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_ENABLE_USER_STR2])
    {
        out = enableUser(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_UPDATE_ADDR_USER_STR3])
    {
        out = updateUserAddress(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_UPDATE_PHONE_USR_STR3])
    {
        out = updateUserPhone(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_USER_STATUS_STR])
    {
        out = queryUserStatus(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_USER_STATE])
    {
        out = queryUserState(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_CREATE_ACCOUNT_STR3])
    {
        out = createAccount(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_ENABLE_ACCOUNT_STR2])
    {
        out = enableAccount(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_FREEZE_ACCOUNT_STR2])
    {
        out = freezeAccount(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_UNFREEZE_ACCOUNT_STR3])
    {
        out = unfreezeAccount(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_CLOSE_ACCOUNT_STR2])
    {
        out = closeAccount(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_ACCOUNT_STATE_STR])
    {
        out = queryAccountState(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_ACCOUNT_STATUS_STR])
    {
        out = queryAccountStatus(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_BALANCE_ACCOUNT_STR])
    {
        out = balance(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_DEPOSIT_ACCOUNT_STR3])
    {
        out = deposit(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3])
    {
        out = withDraw(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2])
    {
        out = transfer1to1(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3])
    {
        out = queryAccountFlow(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT])
    {
        out = queryAccountFlowByIndex(_context, data, _origin);
    }
    else
    {  // unkown function call
        dev::eth::ContractABI abi;
        out = abi.abiIn("", CODE_TRANFSER_INVALID_UNKOWN_FUNC_CALL);
    }

    return out;
}

// remove the white space characters on both sides
void TransferPrecompiled::trim(std::string& _s)
{
    _s.erase(0, _s.find_first_not_of(" "));
    _s.erase(_s.find_last_not_of(" ") + 1);
}

bool TransferPrecompiled::validTime(const std::string& _s)
{  // _s => 2019-04-11 03:25:01

    return !_s.empty();
}

std::pair<bool, int64_t> TransferPrecompiled::stringTime2TimeT(const std::string& _s)
{
    // _s => 2019-04-11 03:25:01
    struct tm t;

    if (strptime(_s.c_str(), "%Y-%m-%d %H:%M:%S", &t) == NULL)
        return std::make_pair(false, 0);

    time_t tt = mktime(&t);

    PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_DESC("create table")
                          << LOG_KV("time", _s) << LOG_KV("tt", tt);

    return std::make_pair(true, tt);
}

bool TransferPrecompiled::validUserStatus(const std::string& _s)
{
    return (_s == BENCH_TRANSFER_USER_STATUS_CREATE) || (_s == BENCH_TRANSFER_USER_STATUS_USABLE) ||
           (_s == BENCH_TRANSFER_USER_STATUS_CLOSED);
}

bool TransferPrecompiled::validPhone(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPrecompiled::validAddress(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPrecompiled::validAccountStatus(const std::string& _s)
{
    return (_s == BENCH_TRANSFER_ACCOUNT_STATUS_CREATE) ||
           (_s == BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE) ||
           (_s == BENCH_TRANSFER_ACCOUNT_STATUS_USABLE) ||
           (_s == BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED);
}

bool TransferPrecompiled::validUserID(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPrecompiled::validAccountID(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPrecompiled::validFlowID(const std::string& _s)
{
    return !_s.empty();
}

Table::Ptr TransferPrecompiled::openTable(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, TransferTable _t, const std::string& _id)
{
    std::string tableName;
    std::string tableKey;
    std::string tableFields;

    switch (_t)
    {
    case TransferTable::User:
        // user table =>
        // |----|------|--------------|--------|---------|-------|
        // | ID | name | account_list | status | address | phone |
        // |----|------|--------------|--------|---------|-------|
        //  User status: CREATE  USABLE  CLOSE
        //  const std::string BENCH_TRANSFER_USER = "_ext_tranfser_user_";
        tableName = BENCH_TRANSFER_USER;
        tableKey = "ID";
        tableFields = "name,account_list,status,address,phone";
        break;
    case TransferTable::Account:
        // account table =>
        // |----|----------|--------|---------|-----------|
        // | ID |  userID  | status | balance | flow_count|
        // |----|----------|--------|---------|-----------|
        //  Account status: CREATE  USABLE  FREEZE  CLOSE
        //  const std::string BENCH_TRANSFER_ACCOUNT = "_ext_transfer_account_";
        tableName = BENCH_TRANSFER_ACCOUNT;
        tableKey = "ID";
        tableFields = "userID,status,balance,flow_count";
        break;
    case TransferTable::Flow:
        // account flow table =>
        // |-------|-------|----|------|-----|--------|
        // | field | index | ID | from |  to | amount |
        // |-------|-------|----|------|-----|--------|
        //  const std::string BENCH_TRANSFER_ACCOUNT_FLOW_PREFIX =
        //  "_ext_transfer_account_flow_";
        tableName = BENCH_TRANSFER_ACCOUNT_FLOW_PREFIX + _id;
        tableKey = "index";
        tableFields = "ID,from,to,amount,time";
        break;
    default:
    {  // unkown table type
        return nullptr;
    }
    };

    auto table = Precompiled::openTable(_context, tableName);
    if (table)
    {
        return table;
    }

    // create it first.
    table = createTable(_context, tableName, tableKey, tableFields, _origin);
    if (table)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_DESC("create table")
                              << LOG_KV("table", tableName) << LOG_KV("key", tableKey)
                              << LOG_KV("fields", tableFields);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled")
                               << LOG_DESC("create table failed") << LOG_KV("table", tableName)
                               << LOG_KV("key", tableKey) << LOG_KV("fields", tableFields);
    }

    return table;
}

bytes TransferPrecompiled::createUser(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, bytesConstRef _data, const std::string& _status, bool errorRet)
{
    dev::eth::ContractABI abi;

    std::string userID;
    std::string userName;

    int retCode = 0;

    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, userName);

        // paramters check
        if (!validUserID(userID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_EXIST;
            break;
        }

        // insert user
        auto entry = table->newEntry();
        entry->setField(BENCH_TRANSFER_USER_FILED_ID, userID);
        entry->setField(BENCH_TRANSFER_USER_FILED_NAME, userName);
        entry->setField(BENCH_TRANSFER_USER_FILED_STATUS, _status);

        auto count = table->insert(userID, entry, std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // insert successfully

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("createUser")
                              << LOG_DESC("success") << LOG_KV("userID", userID)
                              << LOG_KV("userName", userName) << LOG_KV("status", _status)
                              << LOG_KV("errorRet", errorRet);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("createUser")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID)
                               << LOG_KV("userName", userName) << LOG_KV("status", _status)
                               << LOG_KV("errorRet", errorRet);
    }

    if (errorRet)
    {  // 正确情况下返回无权限，错误情况下返回0，修改返回结果
        return abi.abiIn("", (retCode == 0 ? CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED : 0));
    }
    else
    {
        return abi.abiIn("", retCode);
    }
}

bytes TransferPrecompiled::createUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function createUser(string memory userID, string memory userName)
    // public returns(int256);

    return createUser(_context, _origin, _data, BENCH_TRANSFER_USER_STATUS_CREATE);
}

bytes TransferPrecompiled::createUser0(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function createUser(string memory userID, string memory userName)
    // public returns(int256);

    return createUser(_context, _origin, _data, BENCH_TRANSFER_USER_STATUS_CREATE, true);
}

bytes TransferPrecompiled::createEnabledUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function createEnabledUser(string memory userID, string memory userName)
    // public returns(int256);

    return createUser(_context, _origin, _data, BENCH_TRANSFER_USER_STATUS_USABLE);
}

bytes TransferPrecompiled::closeUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    dev::eth::ContractABI abi;

    // function closeUser(string memory userID) public returns(int256);
    int retCode = 0;
    std::string userID;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID);

        trim(userID);

        // paramters check
        if (!validUserID(userID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        auto accountList = entry->getField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST);
        auto status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);

        // check user status
        if (status == BENCH_TRANSFER_USER_STATUS_CLOSED)
        {  // user already closed
            retCode = CODE_TRANFSER_INVALID_USER_INVALID_STATUS;
            break;
        }

        // check all accounts belong to user status
        std::vector<std::string> accounts;
        boost::split(accounts, accountList, boost::is_any_of(","));

        auto accountTable = openTable(_context, _origin, TransferTable::Account);
        if (!accountTable)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        std::size_t errorCount = 0;
        for (const auto& account : accounts)
        {
            // check if account id already exist
            auto entries = table->select(account, table->newCondition());
            if (!entries.get() || (0u == entries->size()))
            {
                continue;
            }

            auto entry = entries->get(0);
            auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

            if (status != BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED)
            {
                errorCount++;
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("closeUser")
                    << LOG_DESC("account not closed status") << LOG_KV("userID", userID)
                    << LOG_KV("accountID", account) << LOG_KV("status", status);
                break;
            }
        }

        // check accounts which belong to this user failed.
        if (errorCount > 0)
        {
            retCode = CODE_TRANFSER_INVALID_USER_ACCOUNT_INVALID_STATUS;
            break;
        }

        auto newEntry = table->newEntry();
        // set user create status
        newEntry->setField(BENCH_TRANSFER_USER_FILED_STATUS, BENCH_TRANSFER_USER_STATUS_CLOSED);

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("closeUser")
                              << LOG_KV("userID", userID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("closeUser")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::enableUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    dev::eth::ContractABI abi;

    // function enableUser(string memory userID) public returns(int256);
    int retCode = 0;
    std::string userID;
    std::string status;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID);

        trim(userID);

        // paramters check
        if (!validUserID(userID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_NT_EXIST;
            break;
        }

        // insert user
        auto entry = entries->get(0);
        status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if ((status == BENCH_TRANSFER_USER_STATUS_CLOSED) ||
            (status == BENCH_TRANSFER_USER_STATUS_USABLE))
        {
            retCode = CODE_TRANFSER_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("enableUser")
                                     << LOG_DESC("user not create status")
                                     << LOG_KV("userID", userID) << LOG_KV("status", status);
            break;
        }

        auto newEntry = table->newEntry();
        // set user create status
        newEntry->setField(BENCH_TRANSFER_USER_FILED_STATUS, BENCH_TRANSFER_USER_STATUS_USABLE);

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("enableUser")
                              << LOG_KV("userID", userID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("enableUser")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::updateUserPhone(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    int retCode = 0;
    // function updateUserPhone(string memory userID, string memory phone)
    // public  returns(int256);
    dev::eth::ContractABI abi;
    std::string userID;
    std::string userPhone;

    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, userPhone);

        trim(userID);
        trim(userPhone);

        // paramters check
        if (!validUserID(userID) || !validPhone(userPhone))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_NT_EXIST;
            break;
        }

        // check user status
        auto entry = entries->get(0);
        auto status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if (status != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_USER_INVALID_STATUS;
            break;
        }

        auto newEntry = table->newEntry();
        // set user phone
        newEntry->setField(BENCH_TRANSFER_USER_FILED_PHONE, userPhone);

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("updateUserPhone")
                              << LOG_KV("userID", userID) << LOG_KV("phone", userPhone);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("updateUserPhone")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID)
                               << LOG_KV("phone", userPhone);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::updateUserAddress(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    int retCode = 0;
    //    function updateUserAddress(string memory userID, string memory addr) public
    //    returns(int256);
    dev::eth::ContractABI abi;
    std::string userID;
    std::string userAddr;

    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, userAddr);

        trim(userID);
        trim(userAddr);

        // paramters check
        if (!validUserID(userID) || !validAddress(userAddr))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_NT_EXIST;
            break;
        }

        // check user status
        auto entry = entries->get(0);
        auto status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if (status != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_USER_INVALID_STATUS;
            break;
        }

        auto newEntry = table->newEntry();
        // set user phone
        newEntry->setField(BENCH_TRANSFER_USER_FILED_ADDRESS, userAddr);

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("updateUserAddress")
                              << LOG_KV("userID", userID) << LOG_KV("address", userAddr);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("updateUserAddress")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID)
                               << LOG_KV("address", userAddr);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::queryUserStatus(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    int retCode = 0;
    std::string userID;
    // function queryUserStatus(string memory userID) view public returns(int256, string
    // memory);
    std::string status;
    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID);

        trim(userID);

        // paramters check
        if (!validUserID(userID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryUserStatus")
                              << LOG_KV("userID", userID) << LOG_KV("status", status);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryUserStatus")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID)
                               << LOG_KV("status", status);
    }


    return abi.abiIn("", retCode, status);
}

bytes TransferPrecompiled::queryUserState(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    //         function queryUserState(string memory userID) view public returns(string
    //         memory,string memory,string memory,string memory); //address phone
    //         status accounts

    int retCode = 0;
    std::string userID;
    std::string userAddr;
    std::string userPhone;
    std::string status;
    std::string userAccouts;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID);

        trim(userID);

        // paramters check
        if (!validUserID(userID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if (status != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryUserState")
                << LOG_DESC("user not usable status") << LOG_KV("userID", userID)
                << LOG_KV("status", status);
            break;
        }

        // get user address
        userAddr = entry->getField(BENCH_TRANSFER_USER_FILED_ADDRESS);
        userPhone = entry->getField(BENCH_TRANSFER_USER_FILED_PHONE);
        userAccouts = entry->getField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryUserState")
                              << LOG_KV("userID", userID) << LOG_KV("address", userAddr)
                              << LOG_KV("phone", userPhone) << LOG_KV("status", status)
                              << LOG_KV("accounts", userAccouts);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryUserState")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID);
    }

    return abi.abiIn("", retCode, userAddr, userPhone, status, userAccouts);
}

// account table operation
bytes TransferPrecompiled::createAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function createAccount(string memory accountID, string memory userID)
    // public returns(int256);

    int retCode = 0;

    std::string userID;
    std::string accountID;

    dev::eth::ContractABI abi;

    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID, userID);

        trim(userID);
        trim(accountID);

        // paramters check
        if (!validUserID(userID) || !validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto accountTable = openTable(_context, _origin, TransferTable::Account);
        if (!accountTable)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if account already exist
        auto entries = accountTable->select(accountID, accountTable->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_EXIST;
            break;
        }

        // check if user exist
        auto userTable = openTable(_context, _origin, TransferTable::User);
        if (!userTable)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        entries = userTable->select(userID, userTable->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        // check if user status available
        if (status != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("createAccount")
                << LOG_DESC("user not usable status") << LOG_KV("userID", userID)
                << LOG_KV("status", status);
            break;
        }

        // update user accountList filed
        std::string accounts = entry->getField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST);
        accounts = (accounts.empty() ? accountID : accounts + "," + accountID);

        auto nEntry = userTable->newEntry();
        nEntry->setField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST, accounts);

        auto count = userTable->update(
            userID, nEntry, userTable->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // insert account list
        auto newAccountEntry = accountTable->newEntry();
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_ID, accountID);
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, u256(10000000).str());
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_USER_ID, userID);
        newAccountEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_CREATE);
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT, u256(0).str());

        count = accountTable->insert(
            accountID, newAccountEntry, std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            // throw Exception for rollback
            // BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        // insert sucessfully

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("createAccount")
                              << LOG_KV("userID", userID) << LOG_KV("accountID", accountID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("createAccount")
                               << LOG_KV("retCode", retCode) << LOG_KV("userID", userID)
                               << LOG_KV("accountID", accountID);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::enableAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    //    function enableAccount(string memory accountID) public
    //    returns(int256);

    int retCode = 0;

    std::string accountID;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID, strTime);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_CREATE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("enableAccount")
                << LOG_DESC("account not create status") << LOG_KV("accountID", accountID)
                << LOG_KV("status", status);
            break;
        }

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_USABLE);

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("enableAccount")
                              << LOG_KV("accountID", accountID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("enableAccount")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::freezeAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function freezeAccount(string memory accountID) public
    // returns(int256);
    int retCode = 0;

    std::string accountID;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("freezeAccount")
                << LOG_DESC("account not usable status") << LOG_KV("accountID", accountID)
                << LOG_KV("status", status);
            break;
        }

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE);

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("freezeAccount")
                              << LOG_KV("accountID", accountID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("freezeAccount")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID);
    }
    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::unfreezeAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function unfreezeAccount(string memory accountID) public
    // returns(int256);
    int retCode = 0;

    std::string accountID;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("unfreezeAccount")
                << LOG_DESC("account not freeze status") << LOG_KV("accountID", accountID)
                << LOG_KV("status", status);
            break;
        }

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_USABLE);

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("unfreezeAccount")
                              << LOG_KV("accountID", accountID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("unfreezeAccount")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID);
    }
    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::closeAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function closeAccount(string memory accountID) public
    // returns(int256);
    int retCode = 0;

    std::string accountID;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status == BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("closeAccount")
                << LOG_DESC("account close status") << LOG_KV("accountID", accountID)
                << LOG_KV("status", status);
            break;
        }

        // if balance of the account is not zero, the account cannot be closed
        /*auto balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        if (balance > 0)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_NOT_ZERO;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("closeAccount") << LOG_DESC("account balance zero")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status)
                << LOG_KV("balance", balance);
            break;
        }*/

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED);

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }
    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("closeAccount")
                              << LOG_KV("accountID", accountID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("closeAccount")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID);
    }
    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::queryAccountStatus(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function queryAccountStatus(string memory accountID) public view returns(int256, string
    // memory);
    int retCode = 0;
    std::string accountID;
    std::string status;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get acount status
        status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountStatus")
                              << LOG_KV("accountID", accountID);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled")
                               << LOG_BADGE("queryAccountStatus") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID);
    }

    return abi.abiIn("", retCode, status);
}


bytes TransferPrecompiled::queryAccountState(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // userID,status,time,uint256,uint256
    // function queryAccountState(string memory accountID) public view returns(int256, string
    // memory, string memory, string memory);
    int retCode = 0;
    std::string accountID;
    std::string userID;
    std::string status;
    std::string flowCount;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get acount status
        status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status == BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED)
        {  // account has been closed
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            break;
        }

        userID = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_USER_ID);
        flowCount = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountState")
                              << LOG_KV("accountID", accountID) << LOG_KV("userID", userID)
                              << LOG_KV("status", status) << LOG_KV("flowCount", flowCount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountState")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID);
    }

    return abi.abiIn("", retCode, userID, status, flowCount);
}

bytes TransferPrecompiled::balance(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function balance(string memory accountID) public view returns(int256, uint256);
    int retCode = 0;

    std::string accountID;
    u256 balance;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            break;
        }

        balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("balance")
                              << LOG_KV("accountID", accountID) << LOG_KV("balance", balance);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("balance")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID);
    }

    return abi.abiIn("", retCode, balance);
}

bytes TransferPrecompiled::deposit(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function deposit(string memory accountID, uint256 amount) public returns(int256);

    int retCode = 0;

    std::string accountID;
    u256 amount;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID, amount);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID) || (amount == 0))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            break;
        }

        auto balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        // auto flowCount = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        if ((balance + amount) < balance)
        {  // overflow
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_OVERFLOW;
            break;
        }

        // auto index = flowCount;
        balance += amount;
        // flowCount += 1;

        auto newEntry = table->newEntry();
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, balance.str());
        // thrownewEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT, flowCount.str());
        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

        // add flow
        // retCode =
        // addFlow(
        //    _context, _origin, index, index, flowID, std::string(""), accountID, amount, strTime);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("deposit")
                              << LOG_KV("accountID", accountID) << LOG_KV("amount", amount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("deposit")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID)
                               << LOG_KV("amount", amount);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::withDraw(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function withDraw(string memory accountID, uint256 amount) public returns(int256);

    int retCode = 0;

    std::string accountID;
    u256 amount;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID, amount);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            break;
        }

        auto balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        // auto flowCount = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        if (balance < amount)
        {  // overflow
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_INSUFFICIENT;
            break;
        }

        // auto index = flowCount;
        balance -= amount;
        // flowCount += 1;

        auto newEntry = table->newEntry();
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, balance.str());
        // newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT, flowCount.str());

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

        // add flow
        // retCode =
        // addFlow(
        //    _context, _origin, index, index, flowID, accountID, std::string(""), amount, strTime);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("withDraw")
                              << LOG_KV("accountID", accountID) << LOG_KV("amount", amount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("withDraw")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID)
                               << LOG_KV("amount", amount);
    }

    return abi.abiIn("", retCode);
}

int32_t TransferPrecompiled::doTransfer(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, const std::string& _from, const std::string& _to, const u256& _amount,
    const std::string& _flowID, const std::string& _strTime)
{
    int retCode = 0;
    do
    {
        if (!validAccountID(_from) || !validAccountID(_to) || !validTime(_strTime) ||
            (_amount == 0) || !validFlowID(_flowID) || (_from == _to))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if _from already exist
        auto entries = table->select(_from, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto fromEntry = entries->get(0);
        auto fromStatus = fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        // check if _to already exist
        entries = table->select(_to, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto toEntry = entries->get(0);
        auto toStatus = toEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        // check _from status and _to status
        if (fromStatus != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE ||
            toStatus != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            break;
        }

        auto fromBalance = u256(fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        auto fromFlowCount = u256(fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        auto toBalance = u256(toEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        auto toFlowCount = u256(fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));

        // to account overflow
        if (toBalance + _amount < toBalance)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_OVERFLOW;
            break;
        }

        // to account overflow
        if (fromBalance < _amount)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_BALANCE_INSUFFICIENT;
            break;
        }

        fromBalance -= _amount;
        toBalance += _amount;

        auto fromIndex = fromFlowCount;
        auto toIndex = toFlowCount;

        fromFlowCount += 1;
        toFlowCount += 1;

        auto newFromEntry = table->newEntry();
        auto newToEntry = table->newEntry();

        // transfer operation
        newFromEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, fromBalance.str());
        newFromEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT, fromFlowCount.str());

        newToEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, toBalance.str());
        newToEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT, toFlowCount.str());

        auto count = table->update(
            _from, newFromEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            // throw Exception for rollback
            // BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        count = table->update(
            _to, newToEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            // throw Exception for rollback
            // BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        addFlow(_context, _origin, fromIndex, toIndex, _flowID, _from, _to, _amount, _strTime);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("transfer")
                              << LOG_KV("from", _from) << LOG_KV("to", _to)
                              << LOG_KV("amount", _amount) << LOG_KV("flowID", _flowID)
                              << LOG_KV("time", _strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("transfer")
                               << LOG_KV("retCode", retCode) << LOG_KV("from", _from)
                               << LOG_KV("to", _to) << LOG_KV("amount", _amount)
                               << LOG_KV("flowID", _flowID) << LOG_KV("time", _strTime);
    }

    return retCode;
}

bytes TransferPrecompiled::transfer1to1(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function transfer(string memory from, string memory to,uint256 amount,
    // string memory flowID, string memory time) public returns(int256);

    dev::eth::ContractABI abi;

    std::string from;
    std::string to;
    u256 amount;
    std::string flowID;
    std::string strTime;

    // analytical parameters
    abi.abiOut(_data, from, to, amount, flowID, strTime);
    // transfer operation
    int32_t retCode = doTransfer(_context, _origin, from, to, amount, flowID, strTime);


    return abi.abiIn("", retCode);
}

bytes TransferPrecompiled::queryAccountFlowByIndex(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    //    function queryAccountFlowByIndex(string memory accountID, string memory index) public
    //    returns(int256, string memory) {}
    int retCode = 0;
    std::string accountID;
    u256 index;
    std::string result;

    dev::eth::ContractABI abi;

    do
    {
        abi.abiOut(_data, accountID, index);
        trim(accountID);

        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto table = openTable(_context, _origin, TransferTable::Flow, accountID);
        if (!table)
        {  // open table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_FLOW_TABLE_FAILED;
            break;
        }

        auto entries = table->select(index.str(), table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_FLOW_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        auto id = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_ID);
        auto from = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_FROM);
        auto to = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TO);
        auto amount = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_AMOUNT);
        auto time = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME);

        result = id + "|" + from + "|" + to + "|" + amount + "|" + time;

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled")
                              << LOG_BADGE("queryAccountFlowByIndex")
                              << LOG_KV("accountID", accountID) << LOG_KV("index", index)
                              << LOG_KV("result", result);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled")
                               << LOG_BADGE("queryAccountFlowByIndex") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("index", index);
    }

    return abi.abiIn("", retCode, result);
}

bytes TransferPrecompiled::queryAccountFlow(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function queryAccountFlow(string memory accountID, string memory start, string memory
    // end) public returns(int256, string[] memory);
    int retCode = 0;

    std::string accountID;
    std::string start;
    std::string end;

    std::vector<std::string> results;
    u256 count = 0;
    dev::eth::ContractABI abi;

    do
    {
        abi.abiOut(_data, accountID, start, end);
        trim(accountID);
        trim(start);
        trim(end);

        if (!validAccountID(accountID))
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        u256 s = u256(start);
        u256 e = u256(end);

        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_KV("start", s)
                              << LOG_KV("end", e) << LOG_KV("bool", (s >= e));
        if (s >= e)
        {
            retCode = CODE_TRANFSER_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto accountTable = openTable(_context, _origin, TransferTable::Account);
        if (!accountTable)
        {  // open table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = accountTable->select(accountID, accountTable->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get account status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_TRANFSER_INVALID_ACCOUNT_INVALID_STATUS;
            break;
        }

        count = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        if (count == 0)
        {  // account flow null
            break;
        }

        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountFlow")
                              << LOG_KV("accountID", accountID) << LOG_KV("flowCount", count)
                              << LOG_KV("start", s) << LOG_KV("end", e);

        // check if account exist
        auto flowTable = openTable(_context, _origin, TransferTable::Flow, accountID);
        if (!flowTable)
        {  // open table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_FLOW_TABLE_FAILED;
            break;
        }

        // binary search
        u256 left = 0;
        u256 right = count - 1;
        u256 startFlowIndex = 0;
        u256 endFlowInex = 0;

        while (left <= right)
        {
            u256 mid = left + (right - left) / 2;
            auto entries = flowTable->select(mid.str(), flowTable->newCondition());
            if (!entries.get() || (0u == entries->size()))
            {
                retCode = CODE_TRANFSER_INVALID_ACCOUNT_FLOW_NOT_EXIST;
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountFlow")
                    << LOG_KV("accountID", accountID) << LOG_KV("flowCount", count)
                    << LOG_KV("mid", mid);
                break;
            }

            auto entry = entries->get(0);
            auto flowT = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME));

            PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled")
                                  << LOG_BADGE("queryAccountFlow0")
                                  << LOG_KV("accountID", accountID) << LOG_KV("index", mid)
                                  << LOG_KV("time", flowT) << LOG_KV("start", s);

            // time compare
            if (flowT >= s)
            {
                right = mid - 1;
                if (mid == 0)
                {
                    break;
                }
            }
            else
            {
                left = mid + 1;
                if (mid > count - 1)
                {
                    break;
                }
            }
        }

        if (!(retCode == 0))
        {
            break;
        }

        if (left > count - 1)
        {  // not exist
            break;
        }

        startFlowIndex = left;

        // left = 0;
        right = count - 1;

        while (left <= right)
        {
            u256 mid = left + (right - left) / 2;
            auto entries = flowTable->select(mid.str(), flowTable->newCondition());
            if (!entries.get() || (0u == entries->size()))
            {
                retCode = CODE_TRANFSER_INVALID_ACCOUNT_FLOW_NOT_EXIST;
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountFlow")
                    << LOG_KV("accountID", accountID) << LOG_KV("flowCount", count)
                    << LOG_KV("mid1", mid);
                break;
            }

            auto entry = entries->get(0);
            auto flowT = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME));

            PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled")
                                  << LOG_BADGE("queryAccountFlow1")
                                  << LOG_KV("accountID", accountID) << LOG_KV("index", mid)
                                  << LOG_KV("time", flowT) << LOG_KV("end", e);

            if (flowT < e)
            {
                left = mid + 1;
                if (mid > count - 1)
                {
                    break;
                }
            }
            else
            {
                right = mid - 1;
                if (mid == 0)
                {
                    break;
                }
            }
        }

        if (!(retCode == 0))
        {
            break;
        }

        if (right == (u256(0) - 1))
        {  // not exist
            break;
        }

        endFlowInex = right;

        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountFlow")
                              << LOG_KV("startFlowIndex", startFlowIndex)
                              << LOG_KV("endFlowInex", endFlowInex);

        count = (endFlowInex - startFlowIndex + 1);

        for (u256 i = startFlowIndex; i <= endFlowInex; ++i)
        {
            auto entries = flowTable->select(i.str(), flowTable->newCondition());
            if (!entries.get() || (0u == entries->size()))
            {
                continue;
            }

            auto entry = entries->get(0);

            auto id = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_ID);
            auto from = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_FROM);
            auto to = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TO);
            auto amount = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_AMOUNT);
            auto time = entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME);

            results.push_back(
                i.str() + "|" + id + "|" + from + "|" + to + "|" + amount + "|" + time);
        }

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountFlow")
                              << LOG_KV("accountID", accountID) << LOG_KV("start", start)
                              << LOG_KV("end", end) << LOG_KV("count", count);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPrecompiled") << LOG_BADGE("queryAccountFlow")
                               << LOG_KV("retCode", retCode) << LOG_KV("accountID", accountID)
                               << LOG_KV("start", start) << LOG_KV("end", end);
    }

    return abi.abiIn("", retCode, results);
}

int TransferPrecompiled::addFlow(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, const u256& _fromIndex, const u256& _toIndex,
    const std::string& _flowID, const std::string& _from, const std::string& _to,
    const u256& _amount, const std::string& _strTime)
{
    PRECOMPILED_LOG(INFO) << LOG_BADGE("addFlow") << LOG_KV("fromIndex", _fromIndex)
                          << LOG_KV("toIndex", _toIndex) << LOG_KV("from", _from)
                          << LOG_KV("to", _to) << LOG_KV("flowID", _flowID)
                          << LOG_KV("amount", _amount) << LOG_KV("time", _strTime);
    int retCode = 0;
    // time check
    do
    {
        auto fromTable = openTable(_context, _origin, TransferTable::Flow, _from);
        if (!fromTable)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_FLOW_TABLE_FAILED;
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("addFlow") << LOG_DESC("openTable failed")
                                   << LOG_KV("from", _from) << LOG_KV("origin", _origin.hex());
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }
        auto toTable = openTable(_context, _origin, TransferTable::Flow, _to);
        if (!toTable)
        {  // create table failed , unexpected error
            retCode = CODE_TRANFSER_INVALID_OPEN_FLOW_TABLE_FAILED;
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("addFlow") << LOG_DESC("openTable failed")
                                   << LOG_KV("to", _to) << LOG_KV("origin", _origin.hex());
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        auto fromEntry = fromTable->newEntry();
        fromEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_INDEX, _fromIndex.str());
        fromEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_ID, _flowID);
        fromEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_FROM, _from);
        fromEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TO, _to);
        fromEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_AMOUNT, _amount.str());
        fromEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME, _strTime);
        auto count = fromTable->insert(
            _fromIndex.str(), fromEntry, std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_FLOW_TABLE_NO_AUTHORIZED;

            PRECOMPILED_LOG(ERROR) << LOG_BADGE("addFlow") << LOG_DESC("insert from failed")
                                   << LOG_KV("origin", _origin.hex());
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        auto toEntry = fromTable->newEntry();
        toEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_INDEX, _toIndex.str());
        toEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_ID, _flowID);
        toEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_FROM, _from);
        toEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TO, _to);
        toEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_AMOUNT, _amount.str());
        toEntry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME, _strTime);
        count = toTable->insert(_toIndex.str(), toEntry, std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_TRANFSER_INVALID_FLOW_TABLE_NO_AUTHORIZED;
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("addFlow") << LOG_DESC("insert to failed")
                                   << LOG_KV("origin", _origin.hex());
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }
        // insert successfully
    } while (0);

    return retCode;
}