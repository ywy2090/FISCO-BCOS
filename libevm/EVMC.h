/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file EVMC.h
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#pragma once

#include "VMFace.h"

#include <libethcore/EVMSchedule.h>

#include <evmc/evmc.h>

namespace dev
{
namespace eth
{
/// Translate the EVMSchedule to EVM-C revision.
evmc_revision toRevision(EVMSchedule const& _schedule);

/// The RAII wrapper for an EVM-C instance.
class EVM
{
public:
    explicit EVM(evmc_instance* _instance) noexcept;

    ~EVM() { m_instance->destroy(m_instance); }

    EVM(EVM const&) = delete;
    EVM& operator=(EVM) = delete;

    class Result
    {
    public:
        explicit Result(evmc_result const& _result) : m_result(_result) {}

        ~Result()
        {
            if (m_result.release)
                m_result.release(&m_result);
        }

        Result(Result&& _other) noexcept : m_result(_other.m_result)
        {
            // Disable releaser of the rvalue object.
            _other.m_result.release = nullptr;
        }

        Result(Result const&) = delete;
        Result& operator=(Result const&) = delete;

        evmc_status_code status() const { return m_result.status_code; }

        int64_t gasLeft() const { return m_result.gas_left; }

        bytesConstRef output() const { return {m_result.output_data, m_result.output_size}; }

    private:
        evmc_result m_result;
    };

    /// Handy wrapper for evmc_execute().
    Result execute(ExtVMFace& _ext, int64_t gas)
    {
        auto mode = toRevision(_ext.evmSchedule());
        evmc_call_kind kind = _ext.isCreate() ? EVMC_CREATE : EVMC_CALL;
        uint32_t flags = _ext.staticCall() ? EVMC_STATIC : 0;

        // this is ensured by solidity compiler
        assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.

        evmc_message msg = {toEvmC(_ext.myAddress()), toEvmC(_ext.caller()), toEvmC(_ext.value()),
            _ext.data().data(), _ext.data().size(), toEvmC(_ext.codeHash()), toEvmC(0x0_cppui256),
            gas, static_cast<int32_t>(_ext.depth()), kind, flags};
        return Result{m_instance->execute(
            m_instance, &_ext, mode, &msg, _ext.code().data(), _ext.code().size())};
    }

private:
    /// The VM instance created with EVM-C <prefix>_create() function.
    evmc_instance* m_instance = nullptr;
};


/// The wrapper implementing the VMFace interface with a EVM-C VM as a backend.
class EVMC : public EVM, public VMFace
{
public:
    explicit EVMC(evmc_instance* _instance) : EVM(_instance) {}

    owning_bytes_ref exec(u256& io_gas, ExtVMFace& _ext, OnOpFunc const& _onOp) final;
};
}  // namespace eth
}  // namespace dev
