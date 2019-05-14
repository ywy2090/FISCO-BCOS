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
/** @file BenchPrecompiled.h
 *  @author octopuswang
 *  @date 20190411
 */
#pragma once
#include <libblockverifier/ExecutiveContext.h>
#include <libprecompiled/Common.h>
#include <utility>

#include <libdevcore/Common.h>
#include <libethcore/Common.h>

namespace dev
{
namespace storage
{
class Table;
}

namespace precompiled
{
enum class TransferTable
{
    User,
    Account,
    Flow,
    UserStateModify,
    AccountStateModify
};

/*
Enum for accont flow type
*/
enum class FlowType
{
    Transfer,
    Deposit,
    WithDraw
};

/*
Enum for user/account state change type
*/
enum class ChangeRecordType
{
    User,
    Account
};

/*
Precompiled for TransferPerf.sol
*/
class TransferPerfPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<TransferPerfPrecompiled> Ptr;
    TransferPerfPrecompiled();
    virtual ~TransferPerfPrecompiled(){};

    std::string toString() override;

    bytes call(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _param,
        Address const& _origin = Address()) override;

private:
    template <typename T>
    std::string toString(const T& _t)
    {
        std::stringstream ss;
        ss << _t;
        return ss.str();
    }

    template <typename T>
    std::string toString(const std::vector<T>& _v, const std::string& separator = " ")
    {
        std::stringstream ss;
        for (typename std::vector<T>::size_type index = 0; index < _v.size(); ++index)
        {
            ss << _v[index];
            if (index < (_v.size() - 1))
            {
                ss << separator;
            }
        }
        return ss.str();
    }

    // remove the white space characters on both sides
    void trim(std::string& _s);
    std::pair<bool, time_t> stringTime2TimeT(const std::string& _s);
    bool validUserID(const std::string& _s);
    bool validAccountID(const std::string& _s);
    bool validFlowID(const std::string& _s);
    bool validTime(const std::string& _s);
    bool validUserStatus(const std::string& _s);
    bool validAccountStatus(const std::string& _s);
    bool validPhone(const std::string& _s);
    bool validAddress(const std::string& _s);

private:
    // user operation
    bytes createUser(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes createEnabledUser(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes closeUser(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes enableUser(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes updateUserPhone(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes updateUserAddress(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes queryUserStatus(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes queryUserState(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);

    // account operation
    bytes createAccount(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes createEnabledAccount(dev::blockverifier::ExecutiveContext::Ptr _context,
        bytesConstRef _data, Address const& _origin);
    bytes freezeAccount(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes enableAccount(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes unfreezeAccount(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes closeAccount(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes queryAccountState(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes balance(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes deposit(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes withDraw(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes transfer1to1(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes transfer1toN(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);
    bytes queryAccountFlow(dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _data,
        Address const& _origin);

private:
    FlowType getFlowType(const std::string& _from, const std::string& _to);
    int addStateChangeLog(dev::blockverifier::ExecutiveContext::Ptr _context,
        Address const& _origin, const std::string& _id, const u256& _index,
        const std::string& _field, const std::string& _old, const std::string& _new,
        const std::string& _time, ChangeRecordType _type);
    int addFlow(dev::blockverifier::ExecutiveContext::Ptr _context, Address const& _origin,
        const u256& _fromIndex, const u256& _toIndex, const std::string& _flowID,
        const std::string& _from, const std::string& _to, const u256& _amount,
        const std::string& _strTime);
    int doTransfer(dev::blockverifier::ExecutiveContext::Ptr _context, Address const& _origin,
        const std::string& _from, const std::string& _to, const u256& _amount,
        const std::string& _flowID, const std::string& _strTime, const u256& _totalAmount,
        bool _doTest = true);

public:
    // is this precompiled need parallel processing, default false.
    virtual bool isParallelPrecompiled() override { return true; }
    virtual std::vector<std::string> getParallelTag(bytesConstRef _param) override;

protected:
    std::shared_ptr<storage::Table> openTable(dev::blockverifier::ExecutiveContext::Ptr _context,
        Address const& _origin, TransferTable _t, const std::string& _id = "");
};

}  // namespace precompiled

}  // namespace dev