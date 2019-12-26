#include "block.hpp"

#include "base/hash.hpp"

#include <utility>

namespace bc
{

Block::Block(base::Sha256 prev_block_hash, TransactionsSet txs)
    : _prev_block_hash{std::move(prev_block_hash)}, _txs(std::move(txs))
{}


base::SerializationOArchive& Block::serialize(base::SerializationOArchive& oa, const Block& block)
{
    return oa << block.getNonce() << block.getPrevBlockHash().getBytes() << block.getTransactions();
}


Block Block::deserialize(base::SerializationIArchive& ia)
{
    NonceInt nonce;
    ia >> nonce;

    base::Bytes prev_block_hash;
    ia >> prev_block_hash;

    TransactionsSet txs;
    ia >> txs;

    Block ret{base::Sha256(prev_block_hash), std::move(txs)};
    ret.setNonce(nonce);

    return ret;
}


const base::Sha256& Block::getPrevBlockHash() const
{
    return _prev_block_hash;
}


const TransactionsSet& Block::getTransactions() const
{
    return _txs;
}


NonceInt Block::getNonce() const noexcept
{
    return _nonce;
}


void Block::setNonce(NonceInt nonce) noexcept
{
    _nonce = nonce;
}


void Block::setPrevBlockHash(const base::Sha256& prev_block_hash)
{
    _prev_block_hash = prev_block_hash;
}


void Block::setTransactions(TransactionsSet txs)
{
    _txs = std::move(txs);
}


void Block::addTransaction(const Transaction& tx)
{
    _txs.add(tx);
}


base::SerializationIArchive& operator>>(base::SerializationIArchive& ia, Block& block)
{
    block = Block::deserialize(ia);
    return ia;
}


base::SerializationOArchive& operator<<(base::SerializationOArchive& oa, const Block& block)
{
    Block::serialize(oa, block);
    return oa;
}

} // namespace bc