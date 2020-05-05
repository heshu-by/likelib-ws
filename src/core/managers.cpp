#include "managers.hpp"

#include "base/error.hpp"

namespace lk
{


std::uint64_t AccountState::getNonce() const noexcept
{
    return _nonce;
}


void AccountState::incNonce() noexcept
{
    ++_nonce;
}


lk::Balance AccountState::getBalance() const noexcept
{
    return _balance;
}


void AccountState::setBalance(lk::Balance new_balance)
{
    _balance = new_balance;
}


void AccountState::addBalance(lk::Balance delta)
{
    _balance += delta;
}


void AccountState::subBalance(lk::Balance delta)
{
    if (_balance < delta) {
        throw base::LogicError("trying to take more LK from account than it has");
    }
    _balance -= delta;
}


const base::Sha256& AccountState::getCodeHash() const noexcept
{
    return _code_hash;
}


void AccountState::setCodeHash(base::Sha256 code_hash)
{
    _code_hash = std::move(code_hash);
}


bool AccountState::checkStorageValue(const base::Sha256& key) const
{
    return _storage.find(key) != _storage.end();
}


AccountState::StorageData AccountState::getStorageValue(const base::Sha256& key) const
{
    if (auto it = _storage.find(key); it == _storage.end()) {
        RAISE_ERROR(base::LogicError, "value was not found by a given key");
    }
    else {
        return it->second;
    }
}


void AccountState::setStorageValue(const base::Sha256& key, base::Bytes value)
{
    StorageData& sd = _storage[key];
    sd.data = std::move(value);
    sd.was_modified = true;
}


void AccountManager::newAccount(const lk::Address& address, base::Sha256 code_hash)
{
    if (hasAccount(address)) {
        RAISE_ERROR(base::LogicError, "address already exists");
    }

    std::unique_lock lk(_rw_mutex);
    AccountState state;
    state.setCodeHash(std::move(code_hash));
    _states[address] = std::move(state);
}


bool AccountManager::hasAccount(const lk::Address& address) const
{
    std::shared_lock lk(_rw_mutex);
    return _states.find(address) != _states.end();
}


bool AccountManager::deleteAccount(const lk::Address& address)
{
    std::unique_lock lk(_rw_mutex);
    if (auto it = _states.find(address); it != _states.end()) {
        _states.erase(it);
        return true;
    }
    else {
        return false;
    }
}


lk::Address AccountManager::newContract(const lk::Address& address, base::Sha256 associated_code_hash)
{
    // address = Ripemd160(associated_code_hash + account_address + Bytes(to_string(nonce))
    auto& account = getAccount(address);
    auto nonce = account.getNonce() + 1;
    account.incNonce();
    base::Bytes nonce_string_data{ std::to_string(nonce) };

    auto bytes_address = base::Ripemd160::compute(associated_code_hash.getBytes().toBytes() +
                                                  address.getBytes().toBytes() + nonce_string_data);
    auto account_address = lk::Address(bytes_address.getBytes());

    newAccount(account_address, std::move(associated_code_hash));
    return account_address;
}


const AccountState& AccountManager::getAccount(const lk::Address& address) const
{
    std::shared_lock lk(_rw_mutex);
    auto it = _states.find(address);
    if (it == _states.end()) {
        RAISE_ERROR(base::InvalidArgument, "cannot getAccount for non-existent account");
    }
    else {
        return it->second;
    }
}


AccountState& AccountManager::getAccount(const lk::Address& address)
{
    std::shared_lock lk(_rw_mutex);
    auto it = _states.find(address);
    if (it == _states.end()) {
        AccountState state; // TODO: lazy creation
        return _states[address] = state;
    }
    else {
        return it->second;
    }
}


lk::Balance AccountManager::getBalance(const lk::Address& account_address) const
{
    if (hasAccount(account_address)) {
        return getAccount(account_address).getBalance();
    }
    else {
        return 0;
    }
}


bool AccountManager::checkTransaction(const lk::Transaction& tx) const
{
    std::shared_lock lk(_rw_mutex);
    if (_states.find(tx.getFrom()) == _states.end()) {
        return false;
    }
    return getAccount(tx.getFrom()).getBalance() >= tx.getAmount();
}


bool AccountManager::tryTransferMoney(const lk::Address& from, const lk::Address& to, lk::Balance amount)
{
    if (!hasAccount(from)) {
        return false;
    }
    auto& from_account = getAccount(from);
    if (from_account.getBalance() < amount) {
        return false;
    }
    if (!hasAccount(to)) {
        newAccount(to, base::Sha256::null());
    }
    auto& to_account = getAccount(to);

    from_account.subBalance(amount);
    to_account.addBalance(amount);
    return true;
}


void AccountManager::update(const lk::Transaction& tx)
{
    std::unique_lock lk(_rw_mutex);
    auto from_iter = _states.find(tx.getFrom());

    if (from_iter == _states.end() || from_iter->second.getBalance() < tx.getAmount()) {
        RAISE_ERROR(base::LogicError, "account doesn't have enough funds to perform the operation");
    }

    auto& from_state = from_iter->second;
    from_state.subBalance(tx.getAmount());
    if (auto to_iter = _states.find(tx.getTo()); to_iter != _states.end()) {
        to_iter->second.addBalance(tx.getAmount());
    }
    else {
        AccountState to_state;
        to_state.setBalance(tx.getAmount());
        _states.insert({ tx.getTo(), std::move(to_state) });
    }
    from_state.incNonce();
}


void AccountManager::update(const lk::Block& block)
{
    for (const auto& tx : block.getTransactions()) {
        update(tx);
    }
}


void AccountManager::updateFromGenesis(const lk::Block& block)
{
    std::unique_lock lk(_rw_mutex);
    for (const auto& tx : block.getTransactions()) {
        AccountState state;
        state.setBalance(tx.getAmount());
        _states.insert({ tx.getTo(), std::move(state) });
    }
}


std::optional<std::reference_wrapper<const base::Bytes>> CodeManager::getCode(const base::Sha256& hash) const
{
    if (auto it = _code_db.find(hash); it == _code_db.end()) {
        return std::nullopt;
    }
    else {
        return it->second;
    }
}


void CodeManager::saveCode(base::Bytes code)
{
    auto hash = base::Sha256::compute(code);
    _code_db.insert({ std::move(hash), std::move(code) });
}

} // namespace core
