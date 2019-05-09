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
/** @file SQLStorage.h
 *  @author ancelmo
 *  @date 20190326
 */

#pragma once

#include "Storage.h"
#include <json/json.h>
#include <libchannelserver/ChannelRPCServer.h>
#include <libdevcore/FixedHash.h>

namespace dev
{
namespace storage
{
class SQLStorage : public Storage
{
public:
    typedef std::shared_ptr<SQLStorage> Ptr;

    SQLStorage();
    virtual ~SQLStorage(){};

    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override;
    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override;
    bool onlyDirty() override;

    virtual void setTopic(const std::string& topic);
    virtual void setChannelRPCServer(dev::ChannelRPCServer::Ptr channelRPCServer);
    virtual void setMaxRetry(int maxRetry);

    virtual void setFatalHandler(std::function<void(std::exception&)> fatalHandler)
    {
        m_fatalHandler = fatalHandler;
    }

private:
    Json::Value requestDB(const Json::Value& value);

    std::function<void(std::exception&)> m_fatalHandler;

    std::string m_topic;
    dev::ChannelRPCServer::Ptr m_channelRPCServer;
    int m_maxRetry = 0;
};

}  // namespace storage

}  // namespace dev
