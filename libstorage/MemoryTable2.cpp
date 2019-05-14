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
/** @file MemoryTable2.cpp
 *  @author ancelmo
 *  @date 20180921
 */
#include "MemoryTable2.h"
#include "Common.h"
#include "Table.h"
#include <arpa/inet.h>
#include <json/json.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libprecompiled/Common.h>
#include <tbb/parallel_sort.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <thread>
#include <vector>

using namespace dev;
using namespace dev::storage;
using namespace dev::precompiled;

Entries::ConstPtr MemoryTable2::select(const std::string& key, Condition::Ptr condition)
{
    return selectNoLock(key, condition);
}

Entries::Ptr MemoryTable2::selectNoLock(const std::string& key, Condition::Ptr condition)
{
    try
    {
        auto entries = std::make_shared<Entries>();
        condition->EQ(m_tableInfo->key, key);
        if (m_remoteDB)
        {
            // query remoteDB anyway
            Entries::Ptr dbEntries =
                m_remoteDB->select(m_blockHash, m_blockNum, m_tableInfo, key, condition);

            if (!dbEntries)
            {
                return entries;
            }

            for (size_t i = 0; i < dbEntries->size(); ++i)
            {
                auto entryIt = m_dirty.find(dbEntries->get(i)->getID());
                if (entryIt != m_dirty.end())
                {
                    entries->addEntry(entryIt->second);
                }
                else
                {
                    entries->addEntry(dbEntries->get(i));
                }
            }
        }

        auto it = m_newEntries.find(key);
        if (it != m_newEntries.end())
        {
            auto indices = processEntries(it->second, condition);
            for (auto itIndex : indices)
            {
                it->second->get(itIndex)->setTempIndex(itIndex);
                entries->addEntry(it->second->get(itIndex));
            }
        }

        return entries;
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2") << LOG_DESC("Table select failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return std::make_shared<Entries>();
}

int MemoryTable2::update(
    const std::string& key, Entry::Ptr entry, Condition::Ptr condition, AccessOptions::Ptr options)
{
    try
    {
        if (options->check && !checkAuthority(options->origin))
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable2")
                                 << LOG_DESC("update permission denied")
                                 << LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);

            return storage::CODE_NO_AUTHORIZED;
        }

        checkField(entry);

        auto entries = selectNoLock(key, condition);
        std::vector<Change::Record> records;

        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr updateEntry = entries->get(i);

            // if id not equals to zero and not in the m_dirty, must be new dirty entry
            if (updateEntry->getID() != 0 && m_dirty.find(updateEntry->getID()) == m_dirty.end())
            {
                m_dirty.insert(std::make_pair(updateEntry->getID(), updateEntry));
            }

            for (auto& it : *(entry->fields()))
            {
                //_id_ always got initialized value 0 from Entry::Entry()
                // no need to update _id_ while updating entry
                if (it.first != "_id_" && it.first != m_tableInfo->key)
                {
                    records.emplace_back(updateEntry->getTempIndex(), it.first,
                        updateEntry->getField(it.first), updateEntry->getID());
                    updateEntry->setField(it.first, it.second);
                }
            }
        }

        m_recorder(shared_from_this(), Change::Update, key, records);

        m_isDirty = true;
        return entries->size();
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2")
                           << LOG_DESC("Access MemoryTable2 failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return 0;
}

int MemoryTable2::insert(
    const std::string& key, Entry::Ptr entry, AccessOptions::Ptr options, bool needSelect)
{
    try
    {
        (void)needSelect;

        if (options->check && !checkAuthority(options->origin))
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable2")
                                 << LOG_DESC("insert permission denied")
                                 << LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);
            return storage::CODE_NO_AUTHORIZED;
        }

        checkField(entry);

        entry->setField(m_tableInfo->key, key);
        auto it = m_newEntries.find(key);

        if (it == m_newEntries.end())
        {
            Entries::Ptr entries = std::make_shared<Entries>();
            it = m_newEntries.insert(std::make_pair(key, entries)).first;
        }
        auto iter = it->second->addEntry(entry);

        // auto iter = m_newEntries->addEntry(entry);
        Change::Record record(iter);

        std::vector<Change::Record> value{record};
        m_recorder(shared_from_this(), Change::Insert, key, value);

        m_isDirty = true;
        return 1;
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2")
                           << LOG_DESC("Access MemoryTable2 failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return 0;
}

int MemoryTable2::remove(
    const std::string& key, Condition::Ptr condition, AccessOptions::Ptr options)
{
    try
    {
        if (options->check && !checkAuthority(options->origin))
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable2")
                                 << LOG_DESC("remove permission denied")
                                 << LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);
            return storage::CODE_NO_AUTHORIZED;
        }

        auto entries = selectNoLock(key, condition);

        std::vector<Change::Record> records;
        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr removeEntry = entries->get(i);

            removeEntry->setStatus(1);

            // if id not equals to zero and not in the m_dirty, must be new dirty entry
            if (removeEntry->getID() != 0 && m_dirty.find(removeEntry->getID()) == m_dirty.end())
            {
                m_dirty.insert(std::make_pair(removeEntry->getID(), removeEntry));
            }

            records.emplace_back(removeEntry->getTempIndex(), "", "", removeEntry->getID());
        }

        m_recorder(shared_from_this(), Change::Remove, key, records);

        m_isDirty = true;
        return entries->size();
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2")
                           << LOG_DESC("Access MemoryTable2 failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return 0;
}

dev::h256 MemoryTable2::hash()
{
    if (m_isDirty)
    {
        m_tableData.reset(new dev::storage::TableData());
        dump();
    }

    return m_hash;
}

dev::storage::TableData::Ptr MemoryTable2::dump()
{
    if (m_isDirty)
    {
        m_tableData = std::make_shared<dev::storage::TableData>();
        m_tableData->info = m_tableInfo;
        m_tableData->dirtyEntries = std::make_shared<Entries>();

        auto tempEntries = tbb::concurrent_vector<Entry::Ptr>();

        tbb::parallel_for(m_dirty.range(),
            [&](tbb::concurrent_unordered_map<uint32_t, Entry::Ptr>::range_type& range) {
                for (auto it = range.begin(); it != range.end(); ++it)
                {
                    if (!it->second->deleted())
                    {
                        m_tableData->dirtyEntries->addEntry(it->second);
                        tempEntries.push_back(it->second);
                    }
                }
            });

        m_tableData->newEntries = std::make_shared<Entries>();
        tbb::parallel_for(m_newEntries.range(),
            [&](tbb::concurrent_unordered_map<std::string, Entries::Ptr>::range_type& range) {
                for (auto it = range.begin(); it != range.end(); ++it)
                {
                    tbb::parallel_for(tbb::blocked_range<size_t>(0, it->second->size(), 1000),
                        [&](tbb::blocked_range<size_t>& rangeIndex) {
                            for (auto i = rangeIndex.begin(); i < rangeIndex.end(); ++i)
                            {
                                if (!it->second->get(i)->deleted())
                                {
                                    m_tableData->newEntries->addEntry(it->second->get(i));
                                    tempEntries.push_back(it->second->get(i));
                                }
                            }
                        });
                }
            });

        tbb::parallel_sort(tempEntries.begin(), tempEntries.end(), EntryLess(m_tableInfo));
        bytes allData;
        for (size_t i = 0; i < tempEntries.size(); ++i)
        {
            auto entry = tempEntries[i];
            for (auto fieldIt : *(entry->fields()))
            {
                if (isHashField(fieldIt.first))
                {
                    allData.insert(allData.end(), fieldIt.first.begin(), fieldIt.first.end());
                    allData.insert(allData.end(), fieldIt.second.begin(), fieldIt.second.end());
                }
            }
        }

        if (allData.empty())
        {
            m_hash = h256();
        }

        bytesConstRef bR(allData.data(), allData.size());
        m_hash = dev::sha256(bR);

        m_isDirty = false;
    }

    return m_tableData;
}

void MemoryTable2::rollback(const Change& _change)
{
    LOG(TRACE) << "Before rollback newEntries size: " << m_newEntries.size();

    switch (_change.kind)
    {
    case Change::Insert:
    {
        LOG(TRACE) << "Rollback insert record newIndex: " << _change.value[0].index;

        auto it = m_newEntries.find(_change.key);
        if (it != m_newEntries.end())
        {
            auto entry = it->second->get(_change.value[0].index);
            entry->setDeleted(true);
        }
        break;
    }
    case Change::Update:
    {
        for (auto& record : _change.value)
        {
            LOG(TRACE) << "Rollback update record id: " << record.id
                       << " newIndex: " << record.index;
            if (record.id)
            {
                auto it = m_dirty.find(record.id);
                if (it != m_dirty.end())
                {
                    it->second->setField(record.key, record.oldValue);
                }
            }
            else
            {
                auto it = m_newEntries.find(_change.key);
                if (it != m_newEntries.end())
                {
                    auto entry = it->second->get(record.index);
                    entry->setField(record.key, record.oldValue);
                }
            }
        }
        break;
    }
    case Change::Remove:
    {
        for (auto& record : _change.value)
        {
            LOG(TRACE) << "Rollback remove record id: " << record.id
                       << " newIndex: " << record.index;
            if (record.id)
            {
                auto it = m_dirty.find(record.id);
                if (it != m_dirty.end())
                {
                    it->second->setStatus(0);
                }
            }
            else
            {
                auto it = m_newEntries.find(_change.key);
                if (it != m_newEntries.end())
                {
                    auto entry = it->second->get(record.index);
                    entry->setStatus(0);
                }
            }
        }
        break;
    }
    case Change::Select:

    default:
        break;
    }

    // LOG(TRACE) << "After rollback newEntries size: " << m_newEntries->size();
}
