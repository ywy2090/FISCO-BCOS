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
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : Implement of BWRateLimiter
 * @file: BWRateLimiter.h
 * @author: yujiechen
 * @date: 2020-04-15
 */
#pragma once

#include <bcos-gateway/libratelimit/BWRateLimiterInterface.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

class BWRateLimiter : public BWRateLimiterInterface
{
public:
    using Ptr = std::shared_ptr<BWRateLimiter>;
    using ConstPtr = std::shared_ptr<const BWRateLimiter>;
    using UniquePtr = std::unique_ptr<const BWRateLimiter>;

public:
    BWRateLimiter(int64_t _maxQPS);

    BWRateLimiter(BWRateLimiter&&) = delete;
    BWRateLimiter(const BWRateLimiter&) = delete;
    BWRateLimiter& operator=(const BWRateLimiter&) = delete;
    BWRateLimiter& operator=(BWRateLimiter&&) = delete;

    ~BWRateLimiter() override {}

public:
    /**
     * @brief
     *
     * @param _requiredPermits
     */
    void acquire(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return true
     * @return false
     */
    bool tryAcquire(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return int64_t
     */
    int64_t acquireWithoutWait(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @return int64_t
     */
    int64_t rollback(int64_t _requiredPermits) override
    {
        // TODO: impl
        return 0;
    }

public:
    int64_t maxQPS() const { return m_maxQPS; }

    void setMaxPermitsSize(int64_t const& _maxPermitsSize);
    void setBurstTimeInterval(int64_t const& _burstInterval);
    void setMaxBurstReqNum(int64_t const& _maxBurstReqNum);

protected:
    int64_t fetchPermitsAndGetWaitTime(
        int64_t _requiredPermits, bool _fetchPermitsWhenRequireWait, int64_t _now);

    void updatePermits(int64_t _now);

    void updateCurrentStoredPermits(int64_t _requiredPermits);

private:
    mutable bcos::Mutex m_mutex;

    // the max QPS
    int64_t m_maxQPS;

    // stored permits
    std::atomic<int64_t> m_currentStoredPermits = 0;

    // the interval time to update storedPermits
    double m_permitsUpdateInterval;
    int64_t m_lastPermitsUpdateTime;
    int64_t m_maxPermits = 0;

    std::atomic<int64_t> m_futureBurstResetTime;
    // the current burstReqNum, every m_burstTimeInterval is refreshed to 0
    std::atomic<int64_t> m_burstReqNum = {0};
    // the max burst num during m_burstTimeInterval
    int64_t m_maxBurstReqNum = 0;
    // default burst interval is 1s
    uint64_t m_burstTimeInterval = 1000000;
};
}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos