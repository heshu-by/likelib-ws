#pragma once

#include "address.hpp"
#include "types.hpp"

#include <base/crypto.hpp>
#include <base/serialization.hpp>
#include <base/time.hpp>


namespace lk
{


class ContractData
{
  public:
    explicit ContractData() = default;
    ContractData(base::Bytes message, base::PropertyTree abi);

    ~ContractData() = default;

    void setMessage(base::Bytes code);
    void setAbi(base::PropertyTree abi);

    const base::Bytes& getMessage() const noexcept;
    const base::PropertyTree& getAbi() const noexcept;

    void serialize(base::SerializationOArchive& oa) const;
    static ContractData deserialize(base::SerializationIArchive& ia);

  private:
    base::Bytes _message;
    base::PropertyTree _abi;
};

//
// class Sign
//{
//  public:
//    Sign() = default;
//    Sign(base::RsaPublicKey sender_public_key, base::Bytes rsa_encrypted_hash);
//
//    bool isNull() const noexcept;
//
//    const base::RsaPublicKey& getPublicKey() const;
//    const base::Bytes& getRsaEncryptedHash() const;
//
//    static Sign fromBase64(const std::string& base64_signature);
//    std::string toBase64() const;
//
//    void serialize(base::SerializationOArchive& oa) const;
//    static Sign deserialize(base::SerializationIArchive& ia);
//
//  private:
//    struct Data
//    {
//        base::RsaPublicKey sender_public_key;
//        base::Bytes rsa_encrypted_hash;
//    };
//
//    std::optional<Data> _data;
//};

using Sign = base::FixedBytes<base::Secp256PrivateKey::SECP256_SIGNATURE_SIZE>;


class Transaction
{
  public:
    Transaction(lk::Address from,
                lk::Address to,
                lk::Balance amount,
                std::uint64_t fee,
                base::Time timestamp,
                base::Bytes data,
                lk::Sign sign = lk::Sign{});
    Transaction(const Transaction&) = default;
    Transaction(Transaction&&) = default;

    Transaction& operator=(const Transaction&) = default;
    Transaction& operator=(Transaction&&) = default;

    ~Transaction() = default;
    //=================
    const lk::Address& getFrom() const noexcept;
    const lk::Address& getTo() const noexcept;
    const lk::Balance& getAmount() const noexcept;
    const base::Time& getTimestamp() const noexcept;
    const base::Bytes& getData() const noexcept;
    const std::uint64_t& getFee() const noexcept;
    //=================
    void sign(const base::Secp256PrivateKey& key);
    bool checkSign() const;
    const lk::Sign& getSign() const noexcept;
    //=================
    bool operator==(const Transaction& other) const;
    bool operator!=(const Transaction& other) const;
    //=================
    base::Sha256 hashOfTransaction() const;
    //=================
    static Transaction deserialize(base::SerializationIArchive& ia);
    void serialize(base::SerializationOArchive& oa) const;
    //=================
  private:
    //=================
    lk::Address _from;
    lk::Address _to;
    lk::Balance _amount;
    std::uint64_t _fee;
    base::Time _timestamp;
    base::Bytes _data;
    lk::Sign _sign;
    //=================
    void serializeHeader(base::SerializationOArchive& oa) const;
    //=================
};


std::ostream& operator<<(std::ostream& os, const Transaction& tx);


class TransactionBuilder
{
  public:
    void setFrom(lk::Address from);
    void setTo(lk::Address to);
    void setAmount(lk::Balance amount);
    void setTimestamp(base::Time timestamp);
    void setFee(std::uint64_t fee);
    void setData(base::Bytes data);
    void setSign(lk::Sign sign);

    [[nodiscard]] Transaction build() const&;
    [[nodiscard]] Transaction build() &&;

  private:
    std::optional<lk::Address> _from;
    std::optional<lk::Address> _to;
    std::optional<lk::Balance> _amount;
    std::optional<base::Time> _timestamp;
    std::optional<std::uint64_t> _fee;
    std::optional<base::Bytes> _data;
    std::optional<lk::Sign> _sign;
};


const Transaction& invalidTransaction();


class TransactionStatus
{
  public:
    enum class StatusCode : uint8_t
    {
        Success = 0,
        Rejected = 1,
        Revert = 2,
        Failed = 3
    };

    enum class ActionType : uint8_t
    {
        None = 0,
        Transfer = 1,
        ContractCall = 2,
        ContractCreation = 3
    };

    explicit TransactionStatus(StatusCode status,
                               ActionType type,
                               std::uint64_t fee_left,
                               const std::string& message = "") noexcept;

    TransactionStatus(const TransactionStatus&) = default;
    TransactionStatus(TransactionStatus&&) = default;
    TransactionStatus& operator=(const TransactionStatus&) = default;
    TransactionStatus& operator=(TransactionStatus&&) = default;

    operator bool() const noexcept;

    bool operator!() const noexcept;

    const std::string& getMessage() const noexcept;
    std::string& getMessage() noexcept;

    StatusCode getStatus() const noexcept;

    ActionType getType() const noexcept;

    std::uint64_t getFeeLeft() const noexcept;

  private:
    StatusCode _status;
    ActionType _action;
    std::string _message;
    std::uint64_t _fee_left;
};

} // namespace lk
