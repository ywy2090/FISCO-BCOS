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
/** @file TransferPerfPrecompiled.cpp
 *  @author octopuswang
 *  @date 20190411
 */
#include "TransferPerfPrecompiled.h"
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
// |----|------|-------------|------|--------|---------|-------|------------|
// | ID | name | accountList | time | status | address | phone | modifyCount|
// |----|------|-------------|------|--------|---------|-------|------------|
//  User status: CREATE  USABLE  CLOSE
//  fields:
const std::string BENCH_TRANSFER_USER = "_ext_bench_tranfser_user_";
const std::string BENCH_TRANSFER_USER_FILED_ID = "ID";
const std::string BENCH_TRANSFER_USER_FILED_NAME = "name";
const std::string BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST = "accountList";
const std::string BENCH_TRANSFER_USER_FILED_TIME = "time";
const std::string BENCH_TRANSFER_USER_FILED_STATUS = "status";
const std::string BENCH_TRANSFER_USER_FILED_ADDRESS = "address";
const std::string BENCH_TRANSFER_USER_FILED_PHONE = "phone";
const std::string BENCH_TRANSFER_USER_FILED_MODIFY_COUNT = "modifyCount";
// User status list:
const std::string BENCH_TRANSFER_USER_STATUS_CREATE = "create";
const std::string BENCH_TRANSFER_USER_STATUS_USABLE = "usable";
const std::string BENCH_TRANSFER_USER_STATUS_CLOSED = "closed";
// ================== USER TABLE END ====================================

// ================== ACCOUNT TABLE BEGIN ===============================
// |----|----------|--------|---------|------|-----------|-------------|
// | ID |  userID  | status | balance | time | flowCount | modifyCount |
// |----|----------|--------|---------|------|-----------|-------------|
//  Account status: CREATE  USABLE  FREEZE  CLOSE
//  fields:
const std::string BENCH_TRANSFER_ACCOUNT = "_ext_bench_transfer_account_";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_ID = "ID";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_USER_ID = "userID";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_STATUS = "status";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_BALANCE = "balance";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_TIME = "time";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT = "flowCount";
const std::string BENCH_TRANSFER_ACCOUNT_FILED_MODIFY_COUNT = "modifyCount";

// status list:
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_CREATE = "create";
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_USABLE = "usable";
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE = "freeze";
const std::string BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED = "closed";
// ================== ACCOUNT TABLE END =================================

// ================== USER/ACCOUNT STATE MODIFY TABLE BEGIN =============
// |-------|-------|-----|-----|------|
// | index | field | old | new | time |
// |-------|-------|-----|-----|------|
// fields:
const std::string BENCH_TRANSFER_USER_STATE_MODIFY_PREFIX =
    "_ext_bench_transfer_user_state_modify_";
const std::string BENCH_TRANSFER_ACCOUNT_STATE_MODIFY_PREFIX =
    "_ext_bench_transfer_account_state_modify_";
const std::string BENCH_TRANSFER_STATE_MODIFY_FIELD_INDEX = "index";
const std::string BENCH_TRANSFER_STATE_MODIFY_FIELD_FIELD = "field";
const std::string BENCH_TRANSFER_STATE_MODIFY_FIELD_OLD = "old";
const std::string BENCH_TRANSFER_STATE_MODIFY_FIELD_NEW = "new";
const std::string BENCH_TRANSFER_STATE_MODIFY_FIELD_TIME = "time";
// ================== USER/ACCOUNT STATE MODIFY TABLE END ===============

// ================== ACCOUNT FLOW TABLE BEGIN ==========================
// |-------|----|------|-----|--------|------|
// | index | ID | from |  to | amount | time |
// |-------|----|------|-----|--------|------|
// fields:
const std::string BENCH_TRANSFER_ACCOUNT_FLOW_PREFIX = "_ext_bench_transfer_account_flow_";
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

contract TransferPerf
{
    function createUser(string memory userID, string memory userName, string memory time) public
returns(int256);

    function createEnabledUser(string memory userID, string memory userName, string memory time)
public returns(int256);

    function closeUser(string memory userID, string memory time) public returns(int256);

    function enableUser(string memory userID, string memory time) public returns(int256);

    function updateUserAddress(string memory userID, string memory addr, string memory time) public
returns(int256);

    function updateUserPhone(string memory userID, string memory phone, string memory time) public
returns(int256);

    function queryUserStatus(string memory userID) view public returns(int256, string memory);

    function queryUserState(string memory userID) view public returns(int256, string memory,string
memory,string memory,string memory,string memory);

    function createAccount(string memory accountID, string memory userID, string memory time) public
returns(int256);

    function createEnabledAccount(string memory accountID, string memory userID, string memory time,
uint256 amount) public returns(int256);

    function enableAccount(string memory accountID, string memory time) public returns(int256);

    function freezeAccount(string memory accountID, string memory time) public returns(int256);

    function unfreezeAccount(string memory accountID,string memory time) public returns(int256);

    function closeAccount(string memory accountID, string memory time) public returns(int256);

    function queryAccountStatus(string memory accountID) public view returns(int256, string
memory);

    function queryAccountState(string memory accountID) public view returns(int256, string
memory, string memory, string memory,uint256,uint256);

    function balance(string memory accountID) public view returns(int256, uint256);

    function deposit(string memory accountID, uint256 amount, string memory flowID, string memory
time) public returns(int256);

    function withDraw(string memory accountID, uint256 amount, string memory flowID, string memory
time) public returns(int256);

    function transfer(string memory fromAccountID, string memory toAccountID,uint256 amount, string
memory flowID, string memory time) public returns(int256);

    function transfer(string memory fromAccountID, string[] memory toAccountID,uint256[] memory
amount, string[] memory flowID, string memory time) public returns(int256);

    function queryAccountFlow(string memory accountID, string memory index) public returns(int256,
string memory) {};

    function queryAccountFlow(string memory accountID, string memory start, string
memory end, uint256 page, uint256 limit) public returns(int256, uint256, string[] memory);
}
*/

const char* const BENCH_METHOD_CREATE_USER_STR3 = "createUser(string,string,string)";
const char* const BENCH_METHOD_CREATE_ENABLED_USER_STR3 = "createEnabledUser(string,string,string)";
const char* const BENCH_METHOD_CLOSE_USER_STR2 = "closeUser(string,string)";
const char* const BENCH_METHOD_ENABLE_USER_STR2 = "enableUser(string,string)";
const char* const BENCH_METHOD_UPDATE_ADDR_USER_STR3 = "updateUserAddress(string,string,string)";
const char* const BENCH_METHOD_UPDATE_PHONE_USR_STR3 = "updateUserPhone(string,string,string)";
const char* const BENCH_METHOD_QUERY_USER_STATUS_STR = "queryUserStatus(string)";
const char* const BENCH_METHOD_QUERY_USER_STATE = "queryUserState(string)";

const char* const BENCH_METHOD_CREATE_ACCOUNT_STR3 = "createAccount(string,string,string)";
const char* const BENCH_METHOD_CREATE_ENABLED_ACCOUNT_STR3_UINT =
    "createEnabledAccount(string,string,string,uint256)";
const char* const BENCH_METHOD_ENABLE_ACCOUNT_STR2 = "enableAccount(string,string)";
const char* const BENCH_METHOD_FREEZE_ACCOUNT_STR2 = "freezeAccount(string,string)";
const char* const BENCH_METHOD_UNFREEZE_ACCOUNT_STR3 = "unfreezeAccount(string,string)";
const char* const BENCH_METHOD_CLOSE_ACCOUNT_STR2 = "closeAccount(string,string)";
const char* const BENCH_METHOD_QUERY_ACCOUNT_STATUS_STR = "queryAccountStatus(string)";
const char* const BENCH_METHOD_QUERY_ACCOUNT_STATE_STR = "queryAccountState(string)";
const char* const BENCH_METHOD_BALANCE_ACCOUNT_STR = "balance(string)";
const char* const BENCH_METHOD_DEPOSIT_ACCOUNT_STR3 = "deposit(string,string,string,string)";
const char* const BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3 = "withDraw(string,string,string,string)";
const char* const BENCH_METHOD_TRANSFER_ACCOUNT_STR_ARR3_STR =
    "transfer(string,string[],uint256[],string[],string)";
const char* const BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2 =
    "transfer(string,string,uint256,string,string)";
const char* const BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT = "queryAccountFlow(string,uint256)";
const char* const BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3_UINT2 =
    "queryAccountFlow(string,string,string,uint256,uint256)";

// common
const static int CODE_BT_INVALID_UNKOWN_FUNC_CALL = 51500;
const static int CODE_BT_INVALID_INVALID_PARAMS = 51501;

// table operation
const static int CODE_BT_INVALID_OPEN_USER_TABLE_FAILED = 51601;
const static int CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED = 51602;
const static int CODE_BT_INVALID_OPEN_FLOW_TABLE_FAILED = 51603;
const static int CODE_BT_INVALID_OPEN_USER_STATE_TABLE_FAILED = 51604;
const static int CODE_BT_INVALID_OPEN_ACCOUNT_STATE_TABLE_FAILED = 51605;
const static int CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED = 51606;
const static int CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED = 51607;
const static int CODE_BT_INVALID_FLOW_TABLE_NO_AUTHORIZED = 51608;
const static int CODE_BT_INVALID_USER_STATE_CHANGE_TABLE_NO_AUTHORIZED = 51609;
const static int CODE_BT_INVALID_ACCOUNT_STATE_CHANGE_NO_AUTHORIZED = 51610;

// user table
const static int CODE_BT_INVALID_USER_EXIST = 51701;
const static int CODE_BT_INVALID_USER_NT_EXIST = 51702;
const static int CODE_BT_INVALID_USER_INVALID_STATUS = 51703;
const static int CODE_BT_INVALID_USER_ACCOUNT_INVALID_STATUS = 51704;

// account table
const static int CODE_BT_INVALID_ACCOUNT_NOT_EXIST = 51801;
const static int CODE_BT_INVALID_ACCOUNT_EXIST = 51802;
const static int CODE_BT_INVALID_ACCOUNT_INVALID_STATUS = 51803;
const static int CODE_BT_INVALID_ACCOUNT_BALANCE_NOT_ZERO = 51804;
const static int CODE_BT_INVALID_ACCOUNT_BALANCE_OVERFLOW = 51805;
const static int CODE_BT_INVALID_ACCOUNT_BALANCE_INSUFFICIENT = 51806;
const static int CODE_BT_INVALID_ACCOUNT_CLOSED_STATUS = 51807;
const static int CODE_BT_INVALID_ACCOUNT_NOT_USABEL_STATUS = 51808;
const static int CODE_BT_INVALID_ACCOUNT_FLOW_NOT_EXIST = 51809;

TransferPerfPrecompiled::TransferPerfPrecompiled()
{
    name2Selector[BENCH_METHOD_CREATE_USER_STR3] = getFuncSelector(BENCH_METHOD_CREATE_USER_STR3);
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
    name2Selector[BENCH_METHOD_CREATE_ENABLED_ACCOUNT_STR3_UINT] =
        getFuncSelector(BENCH_METHOD_CREATE_ENABLED_ACCOUNT_STR3_UINT);
    name2Selector[BENCH_METHOD_ENABLE_ACCOUNT_STR2] =
        getFuncSelector(BENCH_METHOD_ENABLE_ACCOUNT_STR2);
    name2Selector[BENCH_METHOD_FREEZE_ACCOUNT_STR2] =
        getFuncSelector(BENCH_METHOD_FREEZE_ACCOUNT_STR2);
    name2Selector[BENCH_METHOD_UNFREEZE_ACCOUNT_STR3] =
        getFuncSelector(BENCH_METHOD_UNFREEZE_ACCOUNT_STR3);
    name2Selector[BENCH_METHOD_CLOSE_ACCOUNT_STR2] =
        getFuncSelector(BENCH_METHOD_CLOSE_ACCOUNT_STR2);
    name2Selector[BENCH_METHOD_QUERY_ACCOUNT_STATE_STR] =
        getFuncSelector(BENCH_METHOD_QUERY_ACCOUNT_STATE_STR);
    name2Selector[BENCH_METHOD_BALANCE_ACCOUNT_STR] =
        getFuncSelector(BENCH_METHOD_BALANCE_ACCOUNT_STR);
    name2Selector[BENCH_METHOD_DEPOSIT_ACCOUNT_STR3] =
        getFuncSelector(BENCH_METHOD_DEPOSIT_ACCOUNT_STR3);
    name2Selector[BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3] =
        getFuncSelector(BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3);
    name2Selector[BENCH_METHOD_TRANSFER_ACCOUNT_STR_ARR3_STR] =
        getFuncSelector(BENCH_METHOD_TRANSFER_ACCOUNT_STR_ARR3_STR);
    name2Selector[BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2] =
        getFuncSelector(BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2);
    name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3_UINT2] =
        getFuncSelector(BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3_UINT2);
    name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT] =
        getFuncSelector(BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT);
}

std::string TransferPerfPrecompiled::toString()
{
    return "TransferPerfPrecompiled";
}

std::vector<std::string> TransferPerfPrecompiled::getParallelTag(bytesConstRef _param)
{
    bytesConstRef data = getParamData(_param);
    uint32_t func = getParamFunc(_param);

    dev::eth::ContractABI abi;
    std::vector<std::string> paralleTag;

    if (func == name2Selector[BENCH_METHOD_CREATE_USER_STR3])
    {
        // function createUser(string memory userID, string memory userName, string memory time)
        // public returns(int256);
        std::string userID;
        std::string userName;
        std::string strTime;
        // analytical parameters
        abi.abiOut(data, userID, userName, strTime);

        trim(userID);
        trim(userName);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validTime(strTime))
        {
            paralleTag.push_back(userID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_CREATE_ENABLED_USER_STR3])
    {
        // function createEnabledUser(string memory userID, string memory userName, string memory
        // time) public returns(int256);
        std::string userID;
        std::string userName;
        std::string strTime;
        // analytical parameters
        abi.abiOut(data, userID, userName, strTime);

        trim(userID);
        trim(userName);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validTime(strTime))
        {
            paralleTag.push_back(userID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_CLOSE_USER_STR2])
    {  // function closeUser(string memory userID, string memory time) public returns(int256);
        std::string userID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, userID, strTime);

        trim(userID);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validTime(strTime))
        {
            paralleTag.push_back(userID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_ENABLE_USER_STR2])
    {
        // function enableUser(string memory userID, string memory time) public returns(int256);
        std::string userID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, userID, strTime);

        trim(userID);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validTime(strTime))
        {
            paralleTag.push_back(userID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_UPDATE_ADDR_USER_STR3])
    {
        //    function updateUserAddress(string memory userID, string memory addr, string memory
        //    time) public returns(int256);
        std::string userID;
        std::string userAddr;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, userID, userAddr, strTime);

        trim(userID);
        trim(userAddr);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validAddress(userAddr) && validTime(strTime))
        {
            paralleTag.push_back(userID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_UPDATE_PHONE_USR_STR3])
    {
        // function updateUserPhone(string memory userID, string memory phone, string memory time)
        // public  returns(int256);
        std::string userID;
        std::string userPhone;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, userID, userPhone, strTime);

        trim(userID);
        trim(userPhone);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validPhone(userPhone) && validTime(strTime))
        {
            paralleTag.push_back(userID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_USER_STATUS_STR])
    {
        // function queryUserStatus(string memory userID) view public returns(int256, string
        // memory);

        // do nothing
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_USER_STATE])
    {
        //         function queryUserState(string memory userID) view public returns(string
        //         memory,string memory,string memory,string memory,string memory); //address phone
        //         status accounts time

        // do nothing
    }
    else if (func == name2Selector[BENCH_METHOD_CREATE_ACCOUNT_STR3])
    {
        // function createAccount(string memory accountID, string memory userID, string memory time)
        // public returns(int256);
        std::string userID;
        std::string accountID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, userID, accountID, strTime);

        trim(userID);
        trim(accountID);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validAccountID(accountID) && validTime(strTime))
        {
            paralleTag.push_back(userID);
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_CREATE_ENABLED_ACCOUNT_STR3_UINT])
    {
        // function createEnabledAccount(string memory accountID, string memory userID, string
        // memory time, uint256 amount) public returns(int256);
        std::string userID;
        std::string accountID;
        std::string strTime;
        u256 amount;

        // analytical parameters
        abi.abiOut(data, userID, accountID, strTime, amount);

        trim(userID);
        trim(accountID);
        trim(strTime);

        // paramters check
        if (validUserID(userID) && validAccountID(accountID) && validTime(strTime))
        {
            paralleTag.push_back(userID);
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_ENABLE_ACCOUNT_STR2])
    {
        // function enableAccount(string memory accountID, string memory time) public
        // returns(int256);
        std::string accountID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (validAccountID(accountID) && validTime(strTime))
        {
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_FREEZE_ACCOUNT_STR2])
    {
        // function freezeAccount(string memory accountID, string memory time) public
        // returns(int256);
        std::string accountID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (validAccountID(accountID) && validTime(strTime))
        {
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_UNFREEZE_ACCOUNT_STR3])
    {
        // function unfreezeAccount(string memory accountID,string memory time) public
        // returns(int256);
        std::string accountID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (validAccountID(accountID) && validTime(strTime))
        {
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_CLOSE_ACCOUNT_STR2])
    {
        // function closeAccount(string memory accountID, string memory time) public
        // returns(int256);
        std::string accountID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (validAccountID(accountID) && validTime(strTime))
        {
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_ACCOUNT_STATE_STR])
    {
        // userID,status,time,uint256,uint256
        // function queryAccountState(string memory accountID) public view returns(int256, string
        // memory, string memory, string memory,uint256,uint256);

        // do nothing
    }
    else if (func == name2Selector[BENCH_METHOD_QUERY_ACCOUNT_STATUS_STR])
    {
        // userID,status,time,uint256,uint256
        // function queryAccountStatus(string memory accountID) public view returns(int256, string
        // memory, string memory, string memory,uint256,uint256);

        // do nothing
    }
    else if (func == name2Selector[BENCH_METHOD_BALANCE_ACCOUNT_STR])
    {
        // function balance(string memory accountID) public view returns(int256, uint256);

        // do nothing
    }
    else if (func == name2Selector[BENCH_METHOD_DEPOSIT_ACCOUNT_STR3])
    {
        // function deposit(string memory accountID, uint256 amount, string memory flowID, string
        // memory time) public returns(int256);
        std::string accountID;
        u256 amount;
        std::string flowID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, accountID, amount, flowID, strTime);

        trim(accountID);
        trim(flowID);
        trim(strTime);

        // paramters check
        if (validAccountID(accountID) && validFlowID(flowID) && validTime(strTime))
        {
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_WITH_DRAW_ACCOUNT_STR3])
    {
        // function withDraw(string memory accountID, uint256 amount, string memory flowID, string
        // memory time) public returns(int256);
        std::string accountID;
        u256 amount;
        std::string flowID;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, accountID, amount, flowID, strTime);

        trim(accountID);
        trim(flowID);
        trim(strTime);

        // paramters check
        if (validAccountID(accountID) && validFlowID(flowID) && validTime(strTime))
        {
            paralleTag.push_back(accountID);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_TRANSFER_ACCOUNT_STR_ARR3_STR])
    {
        // function transfer(string memory fromAccountID, string[] memory toAccountID,uint256[]
        // memory amount, string[] memory flowID, string memory time) public returns(int256);

        std::string from;
        std::vector<std::string> tos;
        std::vector<u256> amounts;
        std::vector<std::string> flowIDs;
        std::string strTime;

        // analytical parameters
        abi.abiOut(data, from, tos, amounts, flowIDs, strTime);

        // parameters check
        if (validAccountID(from) && !tos.empty() && (tos.size() == amounts.size()) &&
            (tos.size() == flowIDs.size()) && validTime(strTime))
        {
            std::size_t errorCount = 0;
            for (std::size_t index = 0; index < tos.size(); ++index)
            {
                if (!validAccountID(tos[index]) || !validFlowID(flowIDs[index]))
                {
                    errorCount++;
                    break;
                }
            }

            if (errorCount == 0)
            {
                paralleTag.push_back(from);
                paralleTag.insert(paralleTag.begin(), tos.begin(), tos.end());
            }
        }
    }
    else if (func == name2Selector[BENCH_METHOD_TRANSFER_ACCOUNT_STR_STR2_UINT_STR2])
    {
        // function transfer(string memory fromAccountID, string memory toAccountID,uint256 amount,
        // string memory flowID, string memory time) public returns(int256);

        std::string from;
        std::string to;
        u256 amount;
        std::string flowID;
        std::string strTime;
        // analytical parameters
        abi.abiOut(data, from, to, amount, flowID, strTime);

        if (validAccountID(from) && validAccountID(to) && validFlowID(flowID) &&
            validTime(strTime) && (amount > 0))
        {
            paralleTag.push_back(from);
            paralleTag.push_back(to);
        }
    }
    else if (func == name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3_UINT2])
    {
        // function queryAccountFlow(string memory accountID, string memory start, string memory
        // end, uint256 page, uint256 limit) public returns(int256, uint256, string[] memory);

        // do nothing
    }
    else if (func == name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR_UINT])
    {
        // function queryAccountFlow(string memory accountID, uint256 index) public returns(int256,
        // string[] memory);

        // do nothing
    }
    else
    {
        // unkown function call
    }

    return paralleTag;
}

bytes TransferPerfPrecompiled::call(dev::blockverifier::ExecutiveContext::Ptr _context,
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
    else if (func == name2Selector[BENCH_METHOD_CREATE_ENABLED_ACCOUNT_STR3_UINT])
    {
        out = createEnabledAccount(_context, data, _origin);
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
    else if (func == name2Selector[BENCH_METHOD_TRANSFER_ACCOUNT_STR_ARR3_STR])
    {
        out = transfer1toN(_context, data, _origin);
    }
    else if (func == name2Selector[BENCH_METHOD_ACCOUNT_QUERY_FLOW_STR3_UINT2])
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
        out = abi.abiIn("", CODE_BT_INVALID_UNKOWN_FUNC_CALL);
    }

    return out;
}

// remove the white space characters on both sides
void TransferPerfPrecompiled::trim(std::string& _s)
{
    _s.erase(0, _s.find_first_not_of(" "));
    _s.erase(_s.find_last_not_of(" ") + 1);
}

bool TransferPerfPrecompiled::validTime(const std::string& _s)
{  // _s => 2019-04-11 03:25:01
    struct tm t;

    if (strptime(_s.c_str(), "%Y-%m-%d %H:%M:%S", &t) == NULL)
        return false;

    return true;
}

std::pair<bool, time_t> TransferPerfPrecompiled::stringTime2TimeT(const std::string& _s)
{
    // _s => 2019-04-11 03:25:01
    struct tm t;

    if (strptime(_s.c_str(), "%Y-%m-%d %H:%M:%S", &t) == NULL)
        return std::make_pair(false, 0);

    return std::make_pair(true, mktime(&t));
}

bool TransferPerfPrecompiled::validUserStatus(const std::string& _s)
{
    return (_s == BENCH_TRANSFER_USER_STATUS_CREATE) || (_s == BENCH_TRANSFER_USER_STATUS_USABLE) ||
           (_s == BENCH_TRANSFER_USER_STATUS_CLOSED);
}

bool TransferPerfPrecompiled::validPhone(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPerfPrecompiled::validAddress(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPerfPrecompiled::validAccountStatus(const std::string& _s)
{
    return (_s == BENCH_TRANSFER_ACCOUNT_STATUS_CREATE) ||
           (_s == BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE) ||
           (_s == BENCH_TRANSFER_ACCOUNT_STATUS_USABLE) ||
           (_s == BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED);
}

bool TransferPerfPrecompiled::validUserID(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPerfPrecompiled::validAccountID(const std::string& _s)
{
    return !_s.empty();
}

bool TransferPerfPrecompiled::validFlowID(const std::string& _s)
{
    return !_s.empty();
}

Table::Ptr TransferPerfPrecompiled::openTable(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, TransferTable _t, const std::string& _id)
{
    std::string tableName;
    std::string tableKey;
    std::string tableFields;

    switch (_t)
    {
    case TransferTable::User:
        // user table =>
        // |----|------|-------------|------|--------|---------|-------|
        // | ID | name | accountList | time | status | address | phone |
        // |----|------|-------------|------|--------|---------|-------|
        //  User status: CREATE  USABLE  CLOSE
        //  const std::string BENCH_TRANSFER_USER = "_ext_bench_tranfser_user_";
        tableName = BENCH_TRANSFER_USER;
        tableKey = "ID";
        tableFields = "name,accountList,time,status,address,phone,modifyCount";
        break;
    case TransferTable::Account:
        // account table =>
        // |----|----------|--------|---------|------|-----------|-------------|
        // | ID |  userID  | status | balance | time | flowCount | modifyCount |
        // |----|----------|--------|---------|------|-----------|-------------|
        //  Account status: CREATE  USABLE  FREEZE  CLOSE
        //  const std::string BENCH_TRANSFER_ACCOUNT = "_ext_bench_transfer_account_";
        tableName = BENCH_TRANSFER_ACCOUNT;
        tableKey = "ID";
        tableFields = "userID,status,balance,time,flowCount,modifyCount";
        break;
    case TransferTable::Flow:
        // account flow table =>
        // |-------|-------|----|------|-----|--------|------|
        // | field | index | ID | from |  to | amount | time |
        // |-------|-------|----|------|-----|--------|------|
        //  const std::string BENCH_TRANSFER_ACCOUNT_FLOW_PREFIX =
        //  "_ext_bench_transfer_account_flow_";
        tableName = BENCH_TRANSFER_ACCOUNT_FLOW_PREFIX + _id;
        tableKey = "index";
        tableFields = "ID,from,to,amount,time";
        break;
    case TransferTable::UserStateModify:
        // user modify table =>
        // |-------|-------|-----|-----|------|
        // | index | field | old | new | time |
        // |-------|-------|-----|-----|------|
        //  const std::string BENCH_TRANSFER_USER_STATE_MODIFY_PREFIX =
        //  "_ext_bench_transfer_user_modify_";
        tableName = BENCH_TRANSFER_USER_STATE_MODIFY_PREFIX + _id;
        tableKey = "index";
        tableFields = "field,old,new,time";
        break;
    case TransferTable::AccountStateModify:
        // account modify table =>
        // |-------|-------|-----|-----|------|
        // | index | field | old | new | time |
        // |-------|-------|-----|-----|------|
        //  const std::string BENCH_TRANSFER_ACCOUNT_STATE_MODIFY_PREFIX =
        //  "_ext_bench_transfer_account_modify_";
        tableName = BENCH_TRANSFER_ACCOUNT_STATE_MODIFY_PREFIX + _id;
        tableKey = "index";
        tableFields = "field,old,new,time";
        break;
    default:
    {  // unkown table type
    }
    };

    auto table = Precompiled::openTable(_context, tableName);
    if (table)
    {
        return table;
    }

    // table not exist, create it first.
    table = createTable(_context, tableName, tableKey, tableFields, _origin);
    if (table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TransferPerfPrecompiled") << LOG_DESC("create table")
                               << LOG_KV("table", tableName) << LOG_KV("key", tableKey)
                               << LOG_KV("fields", tableFields);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TransferPerfPrecompiled")
                               << LOG_DESC("create table failed") << LOG_KV("table", tableName)
                               << LOG_KV("key", tableKey) << LOG_KV("fields", tableFields);
    }

    return table;
}

bytes TransferPerfPrecompiled::createUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    dev::eth::ContractABI abi;

    // function createUser(string memory userID, string memory userName, string memory time)
    // public returns(int256);
    int retCode = 0;

    std::string userID;
    std::string userName;
    std::string strTime;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, userName, strTime);

        trim(userID);
        trim(userName);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_EXIST;
            break;
        }

        // insert user
        auto entry = table->newEntry();
        entry->setField(BENCH_TRANSFER_USER_FILED_ID, userID);
        entry->setField(BENCH_TRANSFER_USER_FILED_NAME, userName);
        entry->setField(BENCH_TRANSFER_USER_FILED_TIME, strTime);
        entry->setField(BENCH_TRANSFER_USER_FILED_STATUS, BENCH_TRANSFER_USER_STATUS_CREATE);
        entry->setField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT, u256(0).str());

        auto count = table->insert(userID, entry, std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // insert successfully

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("createUser") << LOG_KV("id", userID)
                               << LOG_KV("name", userName) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("createUser") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("id", userID)
                               << LOG_KV("name", userName) << LOG_KV("time", strTime);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::createEnabledUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    dev::eth::ContractABI abi;

    // function createEnabledUser(string memory userID, string memory userName, string memory time)
    // public returns(int256);
    int retCode = 0;

    std::string userID;
    std::string userName;
    std::string strTime;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, userName, strTime);

        trim(userID);
        trim(userName);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_EXIST;
            break;
        }

        // insert user
        auto entry = table->newEntry();
        entry->setField(BENCH_TRANSFER_USER_FILED_ID, userID);
        entry->setField(BENCH_TRANSFER_USER_FILED_NAME, userName);
        entry->setField(BENCH_TRANSFER_USER_FILED_TIME, strTime);
        entry->setField(BENCH_TRANSFER_USER_FILED_STATUS, BENCH_TRANSFER_USER_STATUS_USABLE);
        entry->setField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT, u256(0).str());

        auto count = table->insert(userID, entry, std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // insert successfully

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("createEnabledUser") << LOG_KV("id", userID)
                               << LOG_KV("name", userName) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("createEnabledUser") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("id", userID)
                               << LOG_KV("name", userName) << LOG_KV("time", strTime);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::closeUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    dev::eth::ContractABI abi;

    // function closeUser(string memory userID, string memory time) public returns(int256);
    int retCode = 0;
    std::string userID;
    std::string strTime;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, strTime);

        trim(userID);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        auto accountList = entry->getField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST);
        auto status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        // check user status
        if (status == BENCH_TRANSFER_USER_STATUS_CLOSED)
        {  // user already closed
            retCode = CODE_BT_INVALID_USER_INVALID_STATUS;
            break;
        }

        // check all accounts belong to user status
        std::vector<std::string> accounts;
        boost::split(accounts, accountList, boost::is_any_of(","));

        auto accountTable = openTable(_context, _origin, TransferTable::Account);
        if (!accountTable)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        std::size_t errorCount = 0;
        for (const auto& account : accounts)
        {
            // check if user id already exist
            auto entries = table->select(account, table->newCondition());
            if (!entries.get() || (0u == entries->size()))
            {
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("closeUser") << LOG_DESC("select account failed")
                    << LOG_KV("userID", userID) << LOG_KV("accountID", account);
                errorCount++;
                break;
            }

            auto entry = entries->get(0);
            auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

            if (status != BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED)
            {
                errorCount++;
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("closeUser") << LOG_DESC("account not closed status")
                    << LOG_KV("userID", userID) << LOG_KV("accountID", account)
                    << LOG_KV("status", status);
                break;
            }
        }

        // check accounts which belong to this user failed.
        if (errorCount > 0)
        {
            retCode = CODE_BT_INVALID_USER_ACCOUNT_INVALID_STATUS;
            break;
        }

        auto newEntry = table->newEntry();
        // set user create status
        newEntry->setField(BENCH_TRANSFER_USER_FILED_STATUS, BENCH_TRANSFER_USER_STATUS_CLOSED);
        // set modifyCount
        newEntry->setField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT, modifyCount.str());

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // retCode =
        addStateChangeLog(_context, _origin, userID, index, BENCH_TRANSFER_USER_FILED_STATUS,
            status, BENCH_TRANSFER_USER_STATUS_CLOSED, strTime, ChangeRecordType::User);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("closeUser") << LOG_KV("retCode", retCode)
                               << LOG_KV("id", userID) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("closeUser") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("id", userID)
                               << LOG_KV("time", strTime);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::enableUser(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    dev::eth::ContractABI abi;

    // function enableUser(string memory userID, string memory time) public returns(int256);
    int retCode = 0;
    std::string userID;
    std::string strTime;
    std::string status;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, strTime);

        trim(userID);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        // insert user
        auto entry = entries->get(0);
        status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if ((status == BENCH_TRANSFER_USER_STATUS_CLOSED) ||
            (status == BENCH_TRANSFER_USER_STATUS_USABLE))
        {
            retCode = CODE_BT_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("enableUser") << LOG_DESC("user not suitable status")
                << LOG_KV("userID", userID) << LOG_KV("status", status);
            break;
        }

        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        auto newEntry = table->newEntry();
        // set user create status
        newEntry->setField(BENCH_TRANSFER_USER_FILED_STATUS, BENCH_TRANSFER_USER_STATUS_USABLE);
        // set modifyCount
        newEntry->setField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT, modifyCount.str());

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // retCode =
        addStateChangeLog(_context, _origin, userID, index, BENCH_TRANSFER_USER_FILED_STATUS,
            status, BENCH_TRANSFER_USER_STATUS_USABLE, strTime, ChangeRecordType::User);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("enableUser") << LOG_KV("retCode", retCode)
                               << LOG_KV("id", userID) << LOG_KV("time", strTime)
                               << LOG_KV("status", status);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("enableUser") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("id", userID)
                               << LOG_KV("time", strTime);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::updateUserPhone(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    int retCode = 0;
    // function updateUserPhone(string memory userID, string memory phone, string memory time)
    // public  returns(int256);
    dev::eth::ContractABI abi;
    std::string userID;
    std::string userPhone;
    std::string strTime;

    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, userPhone, strTime);

        trim(userID);
        trim(userPhone);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validPhone(userPhone) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        // check user status
        auto entry = entries->get(0);
        auto status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if (status != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("updateUserPhone") << LOG_DESC("user not usable status")
                << LOG_KV("userID", userID) << LOG_KV("status", status);
            break;
        }

        auto phone = entry->getField(BENCH_TRANSFER_USER_FILED_PHONE);
        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        auto newEntry = table->newEntry();
        // set user phone
        newEntry->setField(BENCH_TRANSFER_USER_FILED_PHONE, userPhone);
        // set modifyCount
        newEntry->setField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT, modifyCount.str());

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // update user state table
        // retCode =
        addStateChangeLog(_context, _origin, userID, index, BENCH_TRANSFER_USER_FILED_PHONE, phone,
            userPhone, strTime, ChangeRecordType::User);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("updateUserPhone") << LOG_KV("retCode", retCode)
                               << LOG_KV("id", userID) << LOG_KV("phone", userPhone)
                               << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("updateUserPhone") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("id", userID)
                               << LOG_KV("phone", userPhone) << LOG_KV("time", strTime);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::updateUserAddress(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    int retCode = 0;
    //    function updateUserAddress(string memory userID, string memory addr, string memory
    //    time) public returns(int256);
    dev::eth::ContractABI abi;
    std::string userID;
    std::string userAddr;
    std::string strTime;

    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, userAddr, strTime);

        trim(userID);
        trim(userAddr);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validAddress(userAddr) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        // check user status
        auto entry = entries->get(0);
        auto status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if (status != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("updateUserAddress") << LOG_DESC("user not usable status")
                << LOG_KV("userID", userID) << LOG_KV("status", status);
            break;
        }

        auto address = entry->getField(BENCH_TRANSFER_USER_FILED_ADDRESS);
        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        auto newEntry = table->newEntry();
        // set user phone
        newEntry->setField(BENCH_TRANSFER_USER_FILED_ADDRESS, userAddr);
        // set modifyCount
        newEntry->setField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT, modifyCount.str());

        auto count = table->update(
            userID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // update user state table
        // retCode =
        addStateChangeLog(_context, _origin, userID, index, BENCH_TRANSFER_USER_FILED_ADDRESS,
            address, userAddr, strTime, ChangeRecordType::User);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("updateUserAddress") << LOG_KV("retCode", retCode)
                               << LOG_KV("id", userID) << LOG_KV("address", userAddr)
                               << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("updateUserAddress") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("id", userID)
                               << LOG_KV("address", userAddr) << LOG_KV("time", strTime);
    }


    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::queryUserStatus(
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
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("queryUserStatus") << LOG_KV("id", userID)
                               << LOG_KV("status", status);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("queryUserStatus") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("id", userID);
    }


    return abi.abiIn("", retCode, status);
}

bytes TransferPerfPrecompiled::queryUserState(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    //         function queryUserState(string memory userID) view public returns(string
    //         memory,string memory,string memory,string memory,string memory); //address phone
    //         status accounts time

    int retCode = 0;
    std::string userID;
    std::string userAddr;
    std::string userPhone;
    std::string userStatus;
    std::string userAccouts;
    std::string userTime;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, userID);

        trim(userID);

        // paramters check
        if (!validUserID(userID))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto table = openTable(_context, _origin, TransferTable::User);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(userID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        auto status = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        if (status != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("queryUserState") << LOG_DESC("user not usable status")
                << LOG_KV("userID", userID) << LOG_KV("status", status);
            break;
        }

        // get user address
        userAddr = entry->getField(BENCH_TRANSFER_USER_FILED_ADDRESS);
        userPhone = entry->getField(BENCH_TRANSFER_USER_FILED_PHONE);
        userStatus = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        userTime = entry->getField(BENCH_TRANSFER_USER_FILED_TIME);
        userAccouts = entry->getField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("queryUserState") << LOG_KV("retCode", retCode)
                               << LOG_KV("id", userID) << LOG_KV("address", userAddr)
                               << LOG_KV("phone", userPhone) << LOG_KV("status", userStatus)
                               << LOG_KV("time", userTime) << LOG_KV("accounts", userAccouts);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("queryUserState") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }

    return abi.abiIn("", retCode, userAddr, userPhone, userStatus, userTime, userAccouts);
}

// account table operation
bytes TransferPerfPrecompiled::createAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function createAccount(string memory accountID, string memory userID, string memory time)
    // public returns(int256);

    int retCode = 0;

    std::string userID;
    std::string accountID;
    std::string strTime;

    dev::eth::ContractABI abi;

    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, accountID, strTime);

        trim(userID);
        trim(accountID);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validAccountID(accountID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto accountTable = openTable(_context, _origin, TransferTable::Account);
        if (!accountTable)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if account already exist
        auto entries = accountTable->select(accountID, accountTable->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_EXIST;
            break;
        }

        // check if user exist
        auto userTable = openTable(_context, _origin, TransferTable::User);
        if (!userTable)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        entries = userTable->select(userID, userTable->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string userStatus = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        // check if user status available
        if (userStatus != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("createAccount") << LOG_DESC("user not usable status")
                << LOG_KV("userID", userID) << LOG_KV("userStatus", userStatus);
            break;
        }

        // update user accountList filed
        std::string accounts = entry->getField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST);
        accounts = (accounts.empty() ? accountID : "," + accountID);

        auto newUserEntry = userTable->newEntry();
        newUserEntry->setField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST, accounts);

        auto count = userTable->update(userID, newUserEntry, userTable->newCondition(),
            std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // insert account list
        auto newAccountEntry = accountTable->newEntry();
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_ID, accountID);
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_TIME, strTime);
        newAccountEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_CREATE);
        count = accountTable->insert(
            accountID, newAccountEntry, std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        // insert sucessfully

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("createAccount") << LOG_KV("userID", userID)
                               << LOG_KV("accountID", accountID) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("createAccount") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::createEnabledAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function createEnabledAccount(string memory accountID, string memory userID, string memory
    // time,uint256 amount) public returns(int256);

    int retCode = 0;

    std::string userID;
    std::string accountID;
    std::string strTime;
    u256 amount;

    dev::eth::ContractABI abi;

    do
    {
        // analytical parameters
        abi.abiOut(_data, userID, accountID, strTime, amount);

        trim(userID);
        trim(accountID);
        trim(strTime);

        // paramters check
        if (!validUserID(userID) || !validAccountID(accountID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto accountTable = openTable(_context, _origin, TransferTable::Account);
        if (!accountTable)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if account already exist
        auto entries = accountTable->select(accountID, accountTable->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_EXIST;
            break;
        }

        // check if user exist
        auto userTable = openTable(_context, _origin, TransferTable::User);
        if (!userTable)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_USER_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        entries = userTable->select(userID, userTable->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_USER_NT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string userStatus = entry->getField(BENCH_TRANSFER_USER_FILED_STATUS);
        // check if user status available
        if (userStatus != BENCH_TRANSFER_USER_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_USER_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("createAccount") << LOG_DESC("user not usable status")
                << LOG_KV("userID", userID) << LOG_KV("userStatus", userStatus);
            break;
        }

        // update user accountList filed
        std::string accounts = entry->getField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST);
        accounts = (accounts.empty() ? accountID : "," + accountID);
        auto newUserEntry = userTable->newEntry();
        newUserEntry->setField(BENCH_TRANSFER_USER_FILED_ACCOUNT_LIST, accounts);

        auto count = userTable->update(userID, newUserEntry, userTable->newCondition(),
            std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_USER_TABLE_NO_AUTHORIZED;
            break;
        }

        // insert account list
        auto newAccountEntry = accountTable->newEntry();
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_ID, accountID);
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_TIME, strTime);
        newAccountEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_USABLE);
        newAccountEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, amount.str());
        count = accountTable->insert(
            accountID, newAccountEntry, std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        // insert sucessfully

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("createEnabledAccount") << LOG_KV("userID", userID)
                               << LOG_KV("accountID", accountID) << LOG_KV("time", strTime)
                               << LOG_KV("amount", amount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("createEnabledAccount") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }


    return abi.abiIn("", retCode);
}


bytes TransferPerfPrecompiled::enableAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    //    function enableAccount(string memory accountID, string memory time) public
    //    returns(int256);

    int retCode = 0;

    std::string accountID;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (!validAccountID(accountID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_CREATE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("enableAccount") << LOG_DESC("account not suitable status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_USABLE);
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_MODIFY_COUNT, modifyCount.str());
        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

        // update account state table
        // retCode =
        addStateChangeLog(_context, _origin, accountID, index, BENCH_TRANSFER_USER_FILED_STATUS,
            status, BENCH_TRANSFER_ACCOUNT_STATUS_USABLE, strTime, ChangeRecordType::Account);

    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("enableAccount") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("enableAccount") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::freezeAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function freezeAccount(string memory accountID, string memory time) public
    // returns(int256);
    int retCode = 0;

    std::string accountID;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (!validAccountID(accountID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("freezeAccount") << LOG_DESC("account not usable status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE);
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_MODIFY_COUNT, modifyCount.str());

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

        // update account state table
        // retCode =
        addStateChangeLog(_context, _origin, accountID, index, BENCH_TRANSFER_USER_FILED_STATUS,
            status, BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE, strTime, ChangeRecordType::Account);

    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("freezeAccount") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("freezeAccount") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }
    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::unfreezeAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function unfreezeAccount(string memory accountID,string memory time) public
    // returns(int256);
    int retCode = 0;

    std::string accountID;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (!validAccountID(accountID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_FREEZE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("unfreezeAccount") << LOG_DESC("account not freeze status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_USABLE);
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_MODIFY_COUNT, modifyCount.str());

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

        // update account state table
        // retCode =
        addStateChangeLog(_context, _origin, accountID, index, BENCH_TRANSFER_USER_FILED_STATUS,
            status, BENCH_TRANSFER_ACCOUNT_STATUS_USABLE, strTime, ChangeRecordType::Account);

    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("unfreezeAccount") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("unfreezeAccount") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }
    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::closeAccount(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function closeAccount(string memory accountID, string memory time) public
    // returns(int256);
    int retCode = 0;

    std::string accountID;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {  // analytical parameters
        abi.abiOut(_data, accountID, strTime);

        trim(accountID);
        trim(strTime);

        // paramters check
        if (!validAccountID(accountID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        std::string status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        auto modifyCount = u256(entry->getField(BENCH_TRANSFER_USER_FILED_MODIFY_COUNT));
        auto index = modifyCount;
        modifyCount += 1;

        if (status == BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("closeAccount") << LOG_DESC("account close status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        // if balance of the account is not zero, the account cannot be closed
        auto balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        if (balance > 0)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_BALANCE_NOT_ZERO;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("closeAccount") << LOG_DESC("account balance zero")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status)
                << LOG_KV("balance", balance);
            break;
        }

        auto newEntry = table->newEntry();
        newEntry->setField(
            BENCH_TRANSFER_ACCOUNT_FILED_STATUS, BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED);
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_MODIFY_COUNT, modifyCount.str());

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

        // update account state table
        // retCode =
        addStateChangeLog(_context, _origin, accountID, index, BENCH_TRANSFER_USER_FILED_STATUS,
            status, BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED, strTime, ChangeRecordType::Account);

    } while (0);


    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("closeAccount") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("closeAccount") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }
    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::queryAccountStatus(
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
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get acount status
        status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("queryAccountStatus") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("status", status);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("queryAccountStatus") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }

    return abi.abiIn("", retCode, status);
}


bytes TransferPerfPrecompiled::queryAccountState(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // userID,status,time,uint256,uint256
    // function queryAccountState(string memory accountID) public view returns(int256, string
    // memory, string memory, string memory);
    int retCode = 0;
    std::string accountID;
    std::string userID;
    std::string strStatus;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID);

        trim(accountID);

        // paramters check
        if (!validAccountID(accountID))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get acount status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status == BENCH_TRANSFER_ACCOUNT_STATUS_CLOSED)
        {  // account has been closed
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("queryAccountState") << LOG_DESC("account close status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        userID = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_USER_ID);
        strStatus = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        strTime = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_TIME);
        // flowCount = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        // modifyCount = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_MODIFY_COUNT));

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("queryAccountState") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("userID", userID)
                               << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("queryAccountState") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }

    return abi.abiIn("", retCode, userID, strStatus, strTime);
}

bytes TransferPerfPrecompiled::balance(
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
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("balance") << LOG_DESC("account not usable status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("balance") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("balance", balance);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("balance") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }

    return abi.abiIn("", retCode, balance);
}

bytes TransferPerfPrecompiled::deposit(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function deposit(string memory accountID, uint256 amount, string memory flowID, string
    // memory time) public returns(int256);

    int retCode = 0;

    std::string accountID;
    u256 amount;
    std::string flowID;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID, amount, flowID, strTime);

        trim(accountID);
        trim(flowID);
        trim(strTime);

        // paramters check
        if (!validAccountID(accountID) || !validFlowID(flowID) || !validTime(strTime) ||
            (amount == 0))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("deposit") << LOG_DESC("account not usable status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        auto balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        auto flowCount = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        if ((balance + amount) < balance)
        {  // overflow
            retCode = CODE_BT_INVALID_ACCOUNT_BALANCE_OVERFLOW;
            break;
        }

        auto index = flowCount;
        balance += amount;
        flowCount += 1;

        auto newEntry = table->newEntry();
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, balance.str());
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT, flowCount.str());
        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_NO_AUTHORIZED;
            break;
        }

        // add flow
        // retCode =
        addFlow(
            _context, _origin, index, index, flowID, std::string(""), accountID, amount, strTime);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("deposit") << LOG_KV("accountID", accountID)
                               << LOG_KV("amount", amount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("deposit") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);

        // throw for rollback??
    }

    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::withDraw(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function withDraw(string memory accountID, uint256 amount, string memory flowID, string
    // memory time) public returns(int256);

    int retCode = 0;

    std::string accountID;
    u256 amount;
    std::string flowID;
    std::string strTime;

    dev::eth::ContractABI abi;
    do
    {
        // analytical parameters
        abi.abiOut(_data, accountID, amount, flowID, strTime);

        trim(accountID);
        trim(flowID);
        trim(strTime);

        // paramters check
        if (!validAccountID(accountID) || !validFlowID(flowID) || !validTime(strTime))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if user exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = table->select(accountID, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get user status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("withDraw") << LOG_DESC("account not usable status")
                << LOG_KV("accountID", accountID) << LOG_KV("status", status);
            break;
        }

        auto balance = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        auto flowCount = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        if (balance < amount)
        {  // overflow
            retCode = CODE_BT_INVALID_ACCOUNT_BALANCE_INSUFFICIENT;
            break;
        }

        auto index = flowCount;
        balance -= amount;
        flowCount += 1;

        auto newEntry = table->newEntry();
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, balance.str());
        newEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT, flowCount.str());

        auto count = table->update(
            accountID, newEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));

        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            break;
        }

        // add flow
        // retCode =
        addFlow(
            _context, _origin, index, index, flowID, accountID, std::string(""), amount, strTime);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("withDraw") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("amount", amount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("withDraw") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode);
    }

    return abi.abiIn("", retCode);
}

int TransferPerfPrecompiled::doTransfer(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, const std::string& _from, const std::string& _to, const u256& _amount,
    const std::string& _flowID, const std::string& _strTime, const u256& _totalAmount, bool _doTest)
{
    int retCode = 0;
    do
    {
        if (!validAccountID(_from) || !validAccountID(_to) || !validTime(_strTime) ||
            (_amount == 0))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto table = openTable(_context, _origin, TransferTable::Account);
        if (!table)
        {  // create table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if _from already exist
        auto entries = table->select(_from, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto fromEntry = entries->get(0);
        auto fromStatus = fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        auto fromBalance = u256(fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        auto fromFlowCount = u256(fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));

        // check if _to already exist
        entries = table->select(_to, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto toEntry = entries->get(0);
        auto toStatus = toEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        auto toBalance = u256(toEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE));
        auto toFlowCount = u256(fromEntry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));

        // check _from status and _to status
        if (fromStatus != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE ||
            toStatus != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("doTransfer") << LOG_DESC("account not usable status")
                << LOG_KV("from", _from) << LOG_KV("to", _to) << LOG_KV("fromStatus", fromStatus)
                << LOG_KV("toStatus", toStatus);
            break;
        }

        // check from account balance enough
        if (fromBalance < _totalAmount)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_BALANCE_INSUFFICIENT;
            break;
        }

        // to account overflow
        if (toBalance + _amount < toBalance)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_BALANCE_OVERFLOW;
            break;
        }

        if (_doTest)
        {  // test operation, check if _origin access
            auto newFromEntry = table->newEntry();
            fromBalance -= u256(0);
            // transfer operation
            newFromEntry->setField(BENCH_TRANSFER_ACCOUNT_FILED_BALANCE, fromBalance.str());

            auto count = table->update(_from, newFromEntry, table->newCondition(),
                std::make_shared<AccessOptions>(_origin));

            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            }

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
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        count = table->update(
            _to, newToEntry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = CODE_BT_INVALID_ACCOUNT_TABLE_NO_AUTHORIZED;
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        // retCode =
        addFlow(_context, _origin, fromIndex, toIndex, _flowID, _from, _to, _amount, _strTime);

    } while (0);

    return retCode;
}

bytes TransferPerfPrecompiled::transfer1toN(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function transfer(string memory fromAccountID, string[] memory toAccountID,uint256[]
    // memory amount, string[] memory flowID, string memory time) public returns(int256);

    int retCode = 0;

    std::string fromAccountID;
    std::vector<std::string> toAccountIDs;
    std::vector<u256> amounts;
    std::vector<std::string> flowIDs;
    std::string strTime;

    dev::eth::ContractABI abi;

    do
    {
        // analytical parameters
        abi.abiOut(_data, fromAccountID, toAccountIDs, amounts, flowIDs, strTime);

        // parameters check
        if (!validAccountID(fromAccountID) || toAccountIDs.empty() ||
            (toAccountIDs.size() != amounts.size()) || (toAccountIDs.size() != flowIDs.size()))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        u256 totalAmount = 0;
        for (std::size_t index = 0; index < toAccountIDs.size(); index++)
        {
            totalAmount += amounts[index];
        }

        // test operation
        std::size_t errorCount = 0;
        for (std::size_t index = 0; index < toAccountIDs.size(); index++)
        {
            auto r = doTransfer(_context, _origin, fromAccountID, toAccountIDs[index],
                amounts[index], flowIDs[index], strTime, totalAmount, true);
            if (0 != r)
            {
                errorCount += 1;
                retCode = r;
                break;
            }
        }

        if (errorCount > 0)
        {
            break;
        }

        // transfer opertion
        for (std::size_t index = 0; index < toAccountIDs.size(); index++)
        {
            doTransfer(_context, _origin, fromAccountID, toAccountIDs[index], amounts[index],
                flowIDs[index], strTime, totalAmount, false);
        }

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("transfer1toN") << LOG_KV("from", fromAccountID)
                               << LOG_KV("to", toString(toAccountIDs))
                               << LOG_KV("amount", toString(amounts))
                               << LOG_KV("flowID", toString(flowIDs)) << LOG_KV("time", strTime);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("transfer1toN") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("from", fromAccountID)
                               << LOG_KV("to", toString(toAccountIDs))
                               << LOG_KV("amount", toString(amounts))
                               << LOG_KV("flowID", toString(flowIDs)) << LOG_KV("time", strTime);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::transfer1to1(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function transfer(string memory fromAccountID, string memory toAccountID,uint256 amount,
    // string memory flowID, string memory time) public returns(int256);

    int retCode = 0;

    std::string fromAccountID;
    std::string toAccountID;
    u256 amount;
    std::string flowID;
    std::string strTime;

    dev::eth::ContractABI abi;

    do
    {
        // analytical parameters
        abi.abiOut(_data, fromAccountID, toAccountID, amount, flowID, strTime);
        // transfer operation
        retCode = doTransfer(
            _context, _origin, fromAccountID, toAccountID, amount, flowID, strTime, amount, false);

    } while (0);

    if (0 == retCode)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("transfer1to1") << LOG_KV("from", fromAccountID)
                               << LOG_KV("to", toAccountID) << LOG_KV("flowID", flowID)
                               << LOG_KV("time", strTime) << LOG_KV("amount", amount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("transfer1to1") << LOG_DESC("failed")
                               << LOG_KV("retCode", retCode) << LOG_KV("from", fromAccountID)
                               << LOG_KV("to", toAccountID) << LOG_KV("flowID", flowID)
                               << LOG_KV("time", strTime) << LOG_KV("amount", amount);
    }

    return abi.abiIn("", retCode);
}

bytes TransferPerfPrecompiled::queryAccountFlowByIndex(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    //    function queryAccountFlow(string memory accountID, string memory index) public
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
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto table = openTable(_context, _origin, TransferTable::Flow, accountID);
        if (!table)
        {  // open table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_FLOW_TABLE_FAILED;
            break;
        }

        auto entries = table->select(index.str(), table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_FLOW_NOT_EXIST;
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
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("queryAccountFlow") << LOG_KV("accountID", accountID)
                               << LOG_KV("index", index) << LOG_KV("result", result);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("queryAccountFlow") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("index", index);
    }

    return abi.abiIn("", retCode, result);
}

bytes TransferPerfPrecompiled::queryAccountFlow(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data, Address const& _origin)
{
    // function queryAccountFlow(string memory accountID, string memory start, string memory end,
    // uint256 index, uint256 limit) public returns(int256, uint256, string[] memory);
    int retCode = 0;

    std::string accountID;
    std::string start;
    std::string end;
    u256 index;
    u256 limit;

    std::vector<std::string> results;
    u256 count = 0;
    dev::eth::ContractABI abi;

    do
    {
        abi.abiOut(_data, accountID, start, end, index, limit);
        trim(accountID);
        trim(start);
        trim(end);

        if (!validAccountID(accountID))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        auto s = stringTime2TimeT(start);
        auto e = stringTime2TimeT(end);
        if (!s.first || !e.first || (s.second < e.second))
        {
            retCode = CODE_BT_INVALID_INVALID_PARAMS;
            break;
        }

        // check if account exist
        auto accountTable = openTable(_context, _origin, TransferTable::Account);
        if (!accountTable)
        {  // open table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_ACCOUNT_TABLE_FAILED;
            break;
        }

        // check if user id already exist
        auto entries = accountTable->select(accountID, accountTable->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            retCode = CODE_BT_INVALID_ACCOUNT_NOT_EXIST;
            break;
        }

        auto entry = entries->get(0);
        // get account status
        auto status = entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_STATUS);
        if (status != BENCH_TRANSFER_ACCOUNT_STATUS_USABLE)
        {
            retCode = CODE_BT_INVALID_ACCOUNT_INVALID_STATUS;
            break;
        }

        count = u256(entry->getField(BENCH_TRANSFER_ACCOUNT_FILED_FLOW_COUNT));
        if (count == 0)
        {  // account flow null
            break;
        }

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("queryAccountFlow") << LOG_KV("accountID", accountID)
                               << LOG_KV("flowCount", count);

        // check if account exist
        auto flowTable = openTable(_context, _origin, TransferTable::Flow, accountID);
        if (!flowTable)
        {  // open table failed , unexpected error
            retCode = CODE_BT_INVALID_OPEN_FLOW_TABLE_FAILED;
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
                retCode = CODE_BT_INVALID_ACCOUNT_FLOW_NOT_EXIST;
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("queryAccountFlow") << LOG_KV("accountID", accountID)
                    << LOG_KV("flowCount", count) << LOG_KV("midFlow0", mid);
                break;
            }

            auto entry = entries->get(0);
            auto flowT = stringTime2TimeT(entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME));
            // time compare
            if (flowT.second >= s.second)
            {
                right = mid - 1;
            }
            else
            {
                left = mid + 1;
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

        left = 0;
        right = count - 1;

        while (left <= right)
        {
            u256 mid = left + (right - left) / 2;
            auto entries = flowTable->select(mid.str(), flowTable->newCondition());
            if (!entries.get() || (0u == entries->size()))
            {
                retCode = CODE_BT_INVALID_ACCOUNT_FLOW_NOT_EXIST;
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("queryAccountFlow") << LOG_KV("accountID", accountID)
                    << LOG_KV("flowCount", count) << LOG_KV("midFlow1", mid);
                break;
            }

            auto entry = entries->get(0);
            auto flowT = stringTime2TimeT(entry->getField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME));
            if (flowT.second >= e.second)
            {
                right = mid - 1;
            }
            else
            {
                left = mid + 1;
            }
        }

        if (!(retCode == 0))
        {
            break;
        }

        if (right = (u256(0) - 1))
        {  // not exist
            break;
        }

        endFlowInex = right;

        count = (endFlowInex - startFlowIndex + 1);

        for (u256 i = startFlowIndex; i <= endFlowInex && limit > 0; ++i)
        {
            if (i < index || i >= index + limit)
            {
                continue;
            }

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
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("queryAccountFlow") << LOG_KV("accountID", accountID)
                               << LOG_KV("start", start) << LOG_KV("end", end)
                               << LOG_KV("index", index) << LOG_KV("limit", limit)
                               << LOG_KV("count", count);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("queryAccountFlow") << LOG_KV("retCode", retCode)
                               << LOG_KV("accountID", accountID) << LOG_KV("start", start)
                               << LOG_KV("end", end) << LOG_KV("index", index)
                               << LOG_KV("limit", limit);
    }

    return abi.abiIn("", retCode, count, results);
}

FlowType TransferPerfPrecompiled::getFlowType(const std::string& _from, const std::string& _to)
{
    return (_from.empty() ? FlowType::Deposit :
                            (_to.empty() ? FlowType::WithDraw : FlowType::Transfer));
}

int TransferPerfPrecompiled::addFlow(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, const u256& _fromIndex, const u256& _toIndex,
    const std::string& _flowID, const std::string& _from, const std::string& _to,
    const u256& _amount, const std::string& _strTime)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("addFlow") << LOG_KV("fromIndex", _fromIndex)
                           << LOG_KV("toIndex", _toIndex) << LOG_KV("from", _from)
                           << LOG_KV("to", _to) << LOG_KV("flowID", _flowID)
                           << LOG_KV("amount", _amount) << LOG_KV("time", _strTime);
    int retCode = 0;
    // time check
    do
    {
        auto flowType = getFlowType(_from, _to);
        if (flowType == FlowType::Transfer)
        {  // transfer operation
            auto fromTable = openTable(_context, _origin, TransferTable::Flow, _from);
            if (!fromTable)
            {  // create table failed , unexpected error
                retCode = CODE_BT_INVALID_OPEN_FLOW_TABLE_FAILED;
                PRECOMPILED_LOG(ERROR) << LOG_BADGE("addFlow") << LOG_DESC("openTable failed")
                                       << LOG_KV("from", _from) << LOG_KV("origin", _origin.hex());
                // throw Exception for rollback
                BOOST_THROW_EXCEPTION(PermissionDenied());
                break;
            }
            auto toTable = openTable(_context, _origin, TransferTable::Flow, _to);
            if (!toTable)
            {  // create table failed , unexpected error
                retCode = CODE_BT_INVALID_OPEN_FLOW_TABLE_FAILED;
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
                retCode = CODE_BT_INVALID_FLOW_TABLE_NO_AUTHORIZED;

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
            count =
                toTable->insert(_toIndex.str(), toEntry, std::make_shared<AccessOptions>(_origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                retCode = CODE_BT_INVALID_FLOW_TABLE_NO_AUTHORIZED;
                PRECOMPILED_LOG(ERROR) << LOG_BADGE("addFlow") << LOG_DESC("insert to failed")
                                       << LOG_KV("origin", _origin.hex());
                // throw Exception for rollback
                BOOST_THROW_EXCEPTION(PermissionDenied());
                break;
            }
        }
        else
        {  // deposit or withDraw operation
            auto accountID = _from.empty() ? _to : _from;
            auto table = openTable(_context, _origin, TransferTable::Flow, accountID);
            if (!table)
            {  // create table failed , unexpected error
                retCode = CODE_BT_INVALID_OPEN_FLOW_TABLE_FAILED;
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("addFlow") << LOG_DESC("openTable failed")
                    << LOG_KV("accountID", accountID) << LOG_KV("origin", "0x" + _origin.hex());
                // throw Exception for rollback
                BOOST_THROW_EXCEPTION(PermissionDenied());
                break;
            }

            auto entry = table->newEntry();
            entry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_INDEX, _fromIndex.str());
            entry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_ID, _flowID);
            entry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_FROM, _from);
            entry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TO, _to);
            entry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_AMOUNT, _amount.str());
            entry->setField(BENCH_TRANSFER_ACCOUNT_FLOW_FIELD_TIME, _strTime);

            auto count =
                table->insert(_fromIndex.str(), entry, std::make_shared<AccessOptions>(_origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                retCode = CODE_BT_INVALID_OPEN_FLOW_TABLE_FAILED;
                PRECOMPILED_LOG(ERROR) << LOG_BADGE("addFlow") << LOG_DESC("insert failed")
                                       << LOG_KV("origin", _origin.hex());
                // throw Exception for rollback
                BOOST_THROW_EXCEPTION(PermissionDenied());
                break;
            }
        }

        // insert successfully

    } while (0);

    return retCode;
}

int TransferPerfPrecompiled::addStateChangeLog(dev::blockverifier::ExecutiveContext::Ptr _context,
    Address const& _origin, const std::string& _id, const u256& _index, const std::string& _field,
    const std::string& _old, const std::string& _new, const std::string& _time,
    ChangeRecordType _type)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("addStateChangeLog") << LOG_KV("id", _id)
                           << LOG_KV("index", _index) << LOG_KV("field", _field)
                           << LOG_KV("old", _old) << LOG_KV("new", _new) << LOG_KV("time", _time);
    int retCode = 0;
    do
    {
        auto table = openTable(_context, _origin,
            (_type == ChangeRecordType::Account ? TransferTable::Account : TransferTable::User),
            _id);
        if (!table)
        {  // create table failed , unexpected error
            retCode = (_type == ChangeRecordType::Account ?
                           CODE_BT_INVALID_OPEN_ACCOUNT_STATE_TABLE_FAILED :
                           CODE_BT_INVALID_OPEN_USER_STATE_TABLE_FAILED);
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("addStateChangeLog") << LOG_DESC("openTable failed")
                                   << LOG_KV("from", _id) << LOG_KV("origin", _origin.hex());
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        auto entry = table->newEntry();
        entry->setField(BENCH_TRANSFER_STATE_MODIFY_FIELD_INDEX, _index.str());
        entry->setField(BENCH_TRANSFER_STATE_MODIFY_FIELD_FIELD, _field);
        entry->setField(BENCH_TRANSFER_STATE_MODIFY_FIELD_OLD, _old);
        entry->setField(BENCH_TRANSFER_STATE_MODIFY_FIELD_NEW, _new);
        entry->setField(BENCH_TRANSFER_STATE_MODIFY_FIELD_TIME, _time);

        auto count = table->insert(_index.str(), entry, std::make_shared<AccessOptions>(_origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            retCode = (_type == ChangeRecordType::Account ?
                           CODE_BT_INVALID_ACCOUNT_STATE_CHANGE_NO_AUTHORIZED :
                           CODE_BT_INVALID_USER_STATE_CHANGE_TABLE_NO_AUTHORIZED);
            PRECOMPILED_LOG(WARNING) << LOG_BADGE("addStateChangeLog") << LOG_DESC("insert failed")
                                     << LOG_KV("index", _index) << LOG_KV("origin", _origin.hex());
            // throw Exception for rollback
            BOOST_THROW_EXCEPTION(PermissionDenied());
            break;
        }

        // insert successfully

    } while (0);

    return retCode;
}