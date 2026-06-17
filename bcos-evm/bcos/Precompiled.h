/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief precompiled base class and precompiled map
 * @file Precompiled.h
 * @author: kyonRay, xingqiangbai
 * @date: 2021-05-25
 */

#pragma once
#include "bcos-executor/src/precompiled/common/PrecompiledGas.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-utilities/Common.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace bcos
{
namespace executor
{
class TransactionExecutive;
}

namespace precompiled
{
class Precompiled : public std::enable_shared_from_this<Precompiled>
{
public:
    using Ptr = std::shared_ptr<Precompiled>;
    using PrecompiledParams =
        std::function<void(const std::shared_ptr<executor::TransactionExecutive>& executive,
            PrecompiledExecResult::Ptr const& callParameters)>;

    Precompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~Precompiled() = default;

    virtual std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) = 0;
    virtual bool isParallelPrecompiled();

    virtual std::vector<std::string> getParallelTag(bytesConstRef, bool);

protected:
    std::map<std::string, uint32_t, std::less<>> name2Selector;
    [[no_unique_address]] std::unordered_map<uint32_t,
        std::pair<protocol::BlockVersion, PrecompiledParams>>
        selector2Func;
    crypto::Hash::Ptr m_hashImpl;

    void registerFunc(uint32_t _selector, PrecompiledParams _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert({_selector, {_minVersion, std::move(_func)}});
    }

    void registerFunc(std::string const& _funcName, PrecompiledParams _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert(
            {getFuncSelector(_funcName, m_hashImpl), {_minVersion, std::move(_func)}});
    }

    template <class F>
    void registerFuncF(uint32_t _selector, F _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert(
            {_selector, {_minVersion, [this](auto&& _executive, auto&& _callParameters) {
                             F(std::forward<decltype(_executive)>(_executive),
                                 std::forward<decltype(_callParameters)>(_callParameters));
                         }}});
    }

protected:
    std::shared_ptr<PrecompiledGasFactory> m_precompiledGasFactory;

private:
    template <typename F>
    void Invoker(F func, const std::shared_ptr<executor::TransactionExecutive>& executive,
        PrecompiledExecResult::Ptr const& callParameters)
    {
        _Invoker(func, executive, callParameters);
    }
    template <typename R, typename T, typename... Args>
    void _Invoker(R (T::*func)(Args...),
        const std::shared_ptr<executor::TransactionExecutive>& executive,
        PrecompiledExecResult::Ptr const& callParameters)
    {
        using ArgsType = std::tuple<typename std::decay_t<Args>...>;
        ArgsType tuple;
        Deserialize(callParameters->params(), tuple);
        CallFunc<R>(
            func, (T*)this, callParameters, tuple, std::make_index_sequence<sizeof...(Args)>{});
    }

    template <typename Tuple, std::size_t... I>
    void _Deserialize(
        bytesConstRef dataRef, CodecWrapper const& codec, Tuple& tup, std::index_sequence<I...>)
    {
        codec.decode(dataRef, std::get<I>(tup)...);
    }

    template <typename... Args>
    void Deserialize(bytesConstRef dataRef, CodecWrapper const& codec, std::tuple<Args...>& val)
    {
        _Deserialize(dataRef, val, std::make_index_sequence<sizeof...(Args)>{});
    }

    template <typename P>
    void SetCallResult(P& val, PrecompiledExecResult::Ptr res, CodecWrapper const& codec)
    {
        res->setExecResult(codec.encode(val));
    }

    template <typename Tuple, std::size_t... I>
    void _SetCallResult(PrecompiledExecResult::Ptr res, CodecWrapper const& codec, Tuple& tup,
        std::index_sequence<I...>)
    {
        res->setExecResult(codec.encode(std::get<I>(tup)...));
    }

    template <typename... Args>
    void SetCallResult(
        std::tuple<Args...>& val, PrecompiledExecResult::Ptr res, CodecWrapper const& codec)
    {
        _SetCallResult(res, val, std::make_index_sequence<sizeof...(Args)>{});
    }

    template <typename R, typename T, typename F, typename Tuple, std::size_t... I>
    void CallFunc(F func, T* pObj, const std::shared_ptr<executor::TransactionExecutive>& executive,
        PrecompiledExecResult::Ptr const& res, Tuple& tup, std::index_sequence<I...>)
    {
        auto const& blockContext = executive->blockContext();
        CodecWrapper codec(blockContext.hashHandler(), blockContext.isWasm());
        try
        {
            if constexpr (std::is_same_v<R, void>)
            {
                (pObj->*func)(std::get<I>(tup)...);
            }
            else if constexpr (!std::is_same_v<R, void>)
            {
                R ret = (pObj->*func)(std::get<I>(tup)...);
                SetCallResult(std::move(ret), res);
            }
        }
        catch (const std::exception& e)
        {
            res->setExecResult(codec.encode(std::string(e.what())));
        }
    }
};

}  // namespace precompiled

namespace executor
{
struct PrecompiledAvailable
{
    precompiled::Precompiled::Ptr precompiled;
    std::function<bool(uint32_t, bool, ledger::Features const& features)> availableFunc;
};
class PrecompiledMap
{
public:
    using Ptr = std::shared_ptr<PrecompiledMap>;
    PrecompiledMap() = default;
    PrecompiledMap(PrecompiledMap const&) = default;
    PrecompiledMap(PrecompiledMap&&) = default;
    PrecompiledMap& operator=(PrecompiledMap const&) = delete;
    PrecompiledMap& operator=(PrecompiledMap&&) = default;
    ~PrecompiledMap() = default;

    auto insert(std::string_view _key, precompiled::Precompiled::Ptr _precompiled,
        protocol::BlockVersion minVersion = protocol::BlockVersion::RC4_VERSION,
        bool needAuth = false)
    {
        auto func = [minVersion, needAuth](
                        uint32_t version, bool isAuth, ledger::Features const& features) -> bool {
            bool flag = true;
            if (needAuth)
            {
                flag = isAuth;
            }
            return version >= minVersion && flag;
        };
        return m_map.insert({std::string(_key), {std::move(_precompiled), std::move(func)}});
    }

    auto insert(std::string_view _key, precompiled::Precompiled::Ptr _precompiled,
        std::function<bool(uint32_t, bool, ledger::Features const& features)> func)
    {
        return m_map.insert({std::string(_key), {std::move(_precompiled), std::move(func)}});
    }
    precompiled::Precompiled::Ptr at(std::string_view, uint32_t version, bool isAuth,
        ledger::Features const& features) const noexcept;
    bool contains(std::string const& key, uint32_t version, bool isAuth,
        ledger::Features const& features) const noexcept;
    size_t size() const noexcept { return m_map.size(); }

private:
    std::unordered_map<std::string, PrecompiledAvailable> m_map;
};
}  // namespace executor
}  // namespace bcos
