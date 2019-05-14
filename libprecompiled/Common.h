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
/** @file Common.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once
#include "libblockverifier/Precompiled.h"
#include <memory>
#include <string>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}

namespace storage
{
class Table;
}

namespace precompiled
{
#define PRECOMPILED_LOG(LEVEL) LOG(LEVEL) << "[PRECOMPILED]"

enum PrecompiledError : int
{
    // CRUDPrecompiled -51599 ~ -51500
    CODE_CONDITION_OPERATION_UNDEFINED = -51504,
    CODE_PARSE_CONDITION_ERROR = -51503,
    CODE_PARSE_ENTRY_ERROR = -51502,
    CODE_FUNCTION_NOT_EXIST = -51501,
    CODE_TABLE_NOT_EXIST = -51500,

    // DagTransferPrecompiled -51499 ~ -51400
    CODE_INVALID_OPENTALBLE_FAILED = -51406,
    CODE_INVALID_BALANCE_OVERFLOW = -51405,
    CODE_INVALID_INSUFFICIENT_BALANCE = -51404,
    CODE_INVALID_USER_ALREADY_EXIST = -51403,
    CODE_INVALID_USER_NOT_EXIST = -51402,
    CODE_INVALID_AMOUNT = -51401,
    CODE_INVALID_USER_NAME = -51400,

    // SystemConfigPrecompiled -51399 ~ -51300
    CODE_INVALID_CONFIGURATION_VALUES = -51300,

    // CNSPrecompiled -51299 ~ -51200
    CODE_ADDRESS_AND_VERSION_EXIST = -51200,

    // ConsensusPrecompiled -51199 ~ -51100
    CODE_LAST_SEALER = -51101,
    CODE_INVALID_NODEID = -51100,

    // PermissionPrecompiled -51099 ~ -51000
    CODE_TABLE_AND_ADDRESS_NOT_EXIST = -51001,
    CODE_TABLE_AND_ADDRESS_EXIST = -51000,

    // Common error code among all precompiled contracts -50199 ~ -50100
    CODE_UNKNOW_FUNCTION_CALL = -50100,

    // correct return: code great or equal 0
    CODE_SUCCESS = 0
};

void getErrorCodeOut(bytes& out, int const& result);


}  // namespace precompiled
}  // namespace dev
