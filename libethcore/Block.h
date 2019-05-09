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
/**
 * @brief basic data structure for block
 *
 * @file Block.h
 * @author: yujiechem
 * @date 2018-09-20
 */
#pragma once
#include "BlockHeader.h"
#include "Transaction.h"
#include "TransactionReceipt.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libdevcore/TrieHash.h>

namespace dev
{
namespace eth
{
class Block
{
public:
    ///-----constructors of Block
    Block() = default;
    explicit Block(bytesConstRef _data,
        CheckTransaction const _option = CheckTransaction::Everything, bool _withReceipt = true,
        bool _withTxHash = false);
    explicit Block(bytes const& _data,
        CheckTransaction const _option = CheckTransaction::Everything, bool _withReceipt = true,
        bool _withTxHash = false);
    /// copy constructor
    Block(Block const& _block);
    /// assignment operator
    Block& operator=(Block const& _block);
    ~Block() {}
    ///-----opearator overloads of Block
    /// operator ==
    bool equalAll(Block const& _block) const
    {
        return m_blockHeader == _block.blockHeader() && m_sigList == _block.sigList() &&
               m_transactions == _block.transactions();
    }

    bool equalWithoutSig(Block const& _block) const
    {
        return m_blockHeader == _block.blockHeader() && m_transactions == _block.transactions();
    }

    bool equalHeader(Block const& _block) const { return m_blockHeader == _block.blockHeader(); }

    explicit operator bool() const { return bool(m_blockHeader); }

    ///-----encode functions
    void encode(bytes& _out) const;
    void encodeRC2(bytes& _out) const;

    ///-----decode functions
    void decode(bytesConstRef _block, CheckTransaction const _option = CheckTransaction::Everything,
        bool _withReceipt = true, bool _withTxHash = false);
    void decodeRC2(bytesConstRef _block,
        CheckTransaction const _option = CheckTransaction::Everything, bool _withReceipt = true,
        bool _withTxHash = false);

    /// @returns the RLP serialisation of this block.
    bytes rlp() const
    {
        bytes out;
        encode(out);
        return out;
    }

    std::shared_ptr<bytes> rlpP() const
    {
        std::shared_ptr<bytes> out = std::make_shared<bytes>();
        encode(*out);
        return out;
    }

    ///-----get interfaces
    Transactions const& transactions() const { return m_transactions; }
    TransactionReceipts const& transactionReceipts() const { return m_transactionReceipts; }
    Transaction const& transaction(size_t const _index) const { return m_transactions[_index]; }
    BlockHeader const& blockHeader() const { return m_blockHeader; }
    BlockHeader& header() { return m_blockHeader; }
    h256 headerHash() const { return m_blockHeader.hash(); }
    std::vector<std::pair<u256, Signature>> const& sigList() const { return m_sigList; }

    ///-----set interfaces
    /// set m_transactions
    void setTransactions(Transactions const& _trans)
    {
        m_transactions = _trans;
        noteChange();
    }
    /// set m_transactionReceipts
    void setTransactionReceipts(TransactionReceipts const& transReceipt)
    {
        m_transactionReceipts = transReceipt;
        noteReceiptChange();
    }
    /// append a single transaction to m_transactions
    void appendTransaction(Transaction const& _trans)
    {
        m_transactions.push_back(_trans);
        noteChange();
    }
    /// append transactions
    void appendTransactions(Transactions const& _trans_array)
    {
        for (auto const& trans : _trans_array)
            m_transactions.push_back(trans);
        noteChange();
    }
    /// set block header
    void setBlockHeader(BlockHeader const& _blockHeader) { m_blockHeader = _blockHeader; }
    /// set sig list
    void inline setSigList(std::vector<std::pair<u256, Signature>> const& _sigList)
    {
        m_sigList = _sigList;
    }
    /// get hash of block header
    h256 blockHeaderHash() { return m_blockHeader.hash(); }
    bool isSealed() const { return (m_blockHeader.sealer() != Invalid256); }
    size_t getTransactionSize() const { return m_transactions.size(); }

    /// get transactionRoot
    h256 const transactionRoot() { return header().transactionsRoot(); }
    h256 const receiptRoot() { return header().receiptsRoot(); }

    /**
     * @brief: reset the current block
     *  1. if the blockHeader param has been set, then populate a new block header from the
     * blockHeader passed by param
     *  2. if the blockHeader param is default, doesn't populate a new block header
     * @param _parent: the block header used to populate a new block header
     */
    void resetCurrentBlock(BlockHeader const& _parent = BlockHeader())
    {
        /// the default sealer of blockHeader is Invalid256
        if (_parent.sealer() != Invalid256)
        {
            m_blockHeader.populateFromParent(_parent);
        }
        /// sealer must be reseted since it's used to decide a block is valid or not
        m_blockHeader.setSealer(Invalid256);
        m_transactions.clear();
        m_transactionReceipts.clear();
        m_sigList.clear();
        m_txsCache.clear();
        m_tReceiptsCache.clear();
        noteChange();
        noteReceiptChange();
    }

    void setEmptyBlock(uint64_t timeStamp)
    {
        m_blockHeader.setNumber(0);
        m_blockHeader.setGasUsed(u256(0));
        m_blockHeader.setSealer(u256(0));
        m_blockHeader.setTimestamp(timeStamp);
        noteChange();
        noteReceiptChange();
    }

    void appendTransactionReceipt(TransactionReceipt const& _tran)
    {
        m_transactionReceipts.push_back(_tran);
        noteReceiptChange();
    }

    void resizeTransactionReceipt(size_t _totalReceipt)
    {
        m_transactionReceipts.resize(_totalReceipt);
        noteReceiptChange();
    }

    void setTransactionReceipt(size_t _receiptId, TransactionReceipt const& _tran)
    {
        m_transactionReceipts[_receiptId] = _tran;
        noteReceiptChange();
    }

    void updateSequenceReceiptGas()
    {
        u256 totalGas = 0;
        for (auto& receipt : m_transactionReceipts)
        {
            totalGas += receipt.gasUsed();
            receipt.setGasUsed(totalGas);
        }
    }

    void setStateRootToAllReceipt(h256 const& _stateRoot)
    {
        for (auto& receipt : m_transactionReceipts)
            receipt.setStateRoot(_stateRoot);
        noteReceiptChange();
    }

    void clearAllReceipts()
    {
        m_transactionReceipts.clear();
        noteReceiptChange();
    }

    const TransactionReceipts& getTransactionReceipts() const { return m_transactionReceipts; }
    void calTransactionRoot(bool update = true) const;
    void calTransactionRootRC2(bool update = true) const;
    void calReceiptRoot(bool update = true) const;
    void calReceiptRootRC2(bool update = true) const;

    /**
     * @brief: set sender for specified transaction, if the sender hasn't been set, then recover
     * sender from the signature
     *
     * @param index: the index of the transaction
     * @param sender: the sender address
     */
    void setSenderForTransaction(size_t index, dev::Address const& sender = ZeroAddress)
    {
        if (sender == ZeroAddress)
        {
            /// verify signature and recover sender from the signature
            m_transactions[index].sender();
        }
        else
        {
            m_transactions[index].forceSender(sender);
        }
    }

private:
    /// callback this function when transaction has been changed
    void noteChange()
    {
        WriteGuard l_txscache(x_txsCache);
        m_txsCache = bytes();
    }

    /// callback this function when transaction receipt has been changed
    void noteReceiptChange()
    {
        WriteGuard l_receipt(x_txReceiptsCache);
        m_tReceiptsCache = bytes();
    }

private:
    /// block header of the block (field 0)
    mutable BlockHeader m_blockHeader;
    /// transaction list (field 1)
    mutable Transactions m_transactions;
    TransactionReceipts m_transactionReceipts;
    /// sig list (field 3)
    std::vector<std::pair<u256, Signature>> m_sigList;
    /// m_transactions converted bytes, when m_transactions changed,
    /// should refresh this catch when encode

    mutable SharedMutex x_txsCache;
    mutable bytes m_txsCache;

    mutable SharedMutex x_txReceiptsCache;
    mutable bytes m_tReceiptsCache;

    mutable dev::h256 m_transRootCache;
    mutable dev::h256 m_receiptRootCache;
};
}  // namespace eth
}  // namespace dev
