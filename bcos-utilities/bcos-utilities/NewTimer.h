

/**
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
 * @brief implementation for NewTimerFactory
 * @file NewTimerFactory.h
 * @author: octopuswang
 * @date 2023-02-20
 */
#pragma once
#include "Common.h"
#include "bcos-utilities/BoostLog.h"
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exception>
#include <memory>

namespace bcos
{

using TimerTask = std::function<void()>;

class NewTimer : public std::enable_shared_from_this<NewTimer>
{
public:
    using Ptr = std::shared_ptr<NewTimer>;
    using ConstPtr = std::shared_ptr<NewTimer>;

    NewTimer(std::shared_ptr<boost::asio::io_service> _ioService, TimerTask&& _task,
        int _periodMS,  // NOLINT
        int _delayMS)
      : m_ioService(std::move(_ioService)),
        m_timerTask(std::move(_task)),
        m_delayMS(_delayMS),
        m_periodMS(_periodMS)
    {}
    NewTimer(const NewTimer&) = delete;
    NewTimer(NewTimer&&) = delete;
    NewTimer& operator=(const NewTimer&) = delete;
    NewTimer& operator=(NewTimer&&) = delete;
    ~NewTimer() { stop(); }

    int periodMS() const { return m_periodMS; }
    int delayMS() const { return m_delayMS; }
    TimerTask timerTask() const { return m_timerTask; }

    void start()
    {
        if (m_running)
        {
            return;
        }
        m_running = true;

        if (m_delayMS > 0)
        {  // delay handle
            startDelayTask();
        }
        else if (m_periodMS > 0)
        {
            // periodic task handle
            startPeriodTask();
        }
        else
        {
            // execute the task directly
            executeTask();
        }
    }

    void startDelayTask()
    {
        m_delayHandler = std::make_shared<boost::asio::deadline_timer>(
            *(m_ioService), boost::posix_time::milliseconds(m_delayMS));

        auto self = weak_from_this();
        m_delayHandler->async_wait([self](const boost::system::error_code& e) {
            auto timer = self.lock();
            if (!timer)
            {
                return;
            }

            if (timer->periodMS() > 0)
            {
                timer->startPeriodTask();
            }
            else
            {
                timer->executeTask();
            }
        });
    }

    void startPeriodTask()
    {
        m_timerHandler = std::make_shared<boost::asio::deadline_timer>(
            *(m_ioService), boost::posix_time::milliseconds(m_periodMS));
        auto self = weak_from_this();
        m_timerHandler->async_wait([self](const boost::system::error_code& e) {
            auto timer = self.lock();
            if (!timer)
            {
                return;
            }

            timer->executeTask();
            timer->startPeriodTask();
        });
    }

    void executeTask()
    {
        try
        {
            if (m_timerTask)
            {
                m_timerTask();
            }
        }
        catch (const std::exception& _e)
        {
            BCOS_LOG(WARNING) << LOG_BADGE("Timer") << LOG_DESC("timer task exception")
                              << LOG_KV("what", _e.what());
        }
    }

    void stop()
    {
        if (!m_running)
        {
            return;
        }

        if (m_delayHandler)
        {
            m_delayHandler->cancel();
        }

        if (m_timerHandler)
        {
            m_timerHandler->cancel();
        }
    }

private:
    bool m_running = false;

    std::shared_ptr<boost::asio::io_service> m_ioService;
    TimerTask m_timerTask;
    int m_delayMS;
    int m_periodMS;
    std::shared_ptr<boost::asio::deadline_timer> m_delayHandler;
    std::shared_ptr<boost::asio::deadline_timer> m_timerHandler;
};
class NewTimerFactory
{
public:
    using Ptr = std::shared_ptr<NewTimerFactory>;
    using ConstPtr = std::shared_ptr<NewTimerFactory>;

    NewTimerFactory(std::shared_ptr<boost::asio::io_service> _ioService)
      : m_ioService(std::move(_ioService))
    {}
    NewTimerFactory() : m_ioService(std::make_shared<boost::asio::io_service>())
    {
        // No io_service object is provided, create io_service and the worker thread
        startThread();
    }
    NewTimerFactory(const NewTimerFactory&) = delete;
    NewTimerFactory(NewTimerFactory&&) = delete;
    NewTimerFactory& operator=(const NewTimerFactory&) = delete;
    NewTimerFactory& operator=(NewTimerFactory&&) = delete;
    ~NewTimerFactory() { stopThread(); }

    /**
     * @brief
     *
     * @param _task
     * @param _periodMS
     * @param delayMS
     * @return Timer::Ptr
     */
    NewTimer::Ptr createTimer(TimerTask&& _task, int _periodMS, int _delayMS = 0)  // NOLINT
    {
        auto timer = std::make_shared<NewTimer>(m_ioService, std::move(_task), _periodMS, _delayMS);
        return timer;
    }

private:
    void startThread()
    {
        if (m_worker)
        {
            return;
        }
        m_running = true;

        BCOS_LOG(INFO) << LOG_BADGE("startThread") << LOG_DESC("start the timer thread");

        m_worker = std::make_unique<std::thread>([this]() {
            bcos::pthread_setThreadName(m_threadName);
            BCOS_LOG(INFO) << LOG_BADGE("startThread") << LOG_DESC("the timer thread start")
                           << LOG_KV("threadName", m_threadName);
            while (m_running)
            {
                try
                {
                    m_ioService->run();
                    if (!m_running)
                    {
                        break;
                    }
                }
                catch (std::exception const& e)
                {
                    BCOS_LOG(WARNING) << LOG_BADGE("startThread")
                                      << LOG_DESC("Exception in Worker Thread of timer")
                                      << LOG_KV("error", boost::diagnostic_information(e));
                }
                m_ioService->reset();
            }

            BCOS_LOG(INFO) << LOG_BADGE("startThread") << LOG_DESC("the timer thread stop");
        });
    }

    void stopThread()
    {
        if (!m_worker)
        {
            return;
        }
        m_running = false;
        BCOS_LOG(INFO) << LOG_BADGE("stopThread") << LOG_DESC("stop the timer thread");

        m_ioService->stop();
        if (m_worker->get_id() != std::this_thread::get_id())
        {
            m_worker->join();
            m_worker.reset();
        }
        else
        {
            m_worker->detach();
        }
    }

    std::atomic_bool m_running = {false};
    std::string m_threadName = "newTimer";
    std::unique_ptr<std::thread> m_worker = nullptr;
    std::shared_ptr<boost::asio::io_service> m_ioService = nullptr;
};

}  // namespace bcos