#pragma once

#include "base/error.hpp"
#include "base/time.hpp"
#include "base/utility.hpp"
#include "core/address.hpp"
#include "core/block.hpp"
#include "net/error.hpp"
#include "net/session.hpp"

#include <atomic>
#include <forward_list>
#include <memory>

namespace lk
{

// peer context requirements for handling messages
class Core;
class Host;
class PeerPoolBase;
class KademliaPeerPoolBase;
//=======================

// messages available
namespace msg
{
// clang-format off
DEFINE_ENUM_CLASS_WITH_STRING_CONVERSIONS(Type, std::uint8_t,
  (DEBUG_MIN)
  (CONNECT)
  (CANNOT_ACCEPT)
  (ACCEPTED)
  (PING)
  (PONG)
  (LOOKUP)
  (LOOKUP_RESPONSE)
  (TRANSACTION)
  (GET_BLOCK)
  (BLOCK)
  (BLOCK_NOT_FOUND)
  (CLOSE)
  (DEBUG_MAX)
)
// clang-format on

class Message
{};

struct Connect;
struct CannotAccept;
struct Accepted;
struct Ping;
struct Pong;
struct Lookup;
struct LookupResponse;
struct Transaction;
struct GetBlock;
struct Block;
struct BlockNotFound;
struct Close;
}
//=======================

class Rating
{
  public:
    // TODO: remember peer rating during several node runs

    explicit Rating(std::int_fast32_t initial_value = base::config::NET_INITIAL_PEER_RATING);

    std::int_fast32_t getValue() const noexcept;
    explicit operator bool() const noexcept; // if false, then the peer is too bad

    Rating& nonExpectedMessage() noexcept;
    Rating& invalidMessage() noexcept;
    Rating& badBlock() noexcept;
    Rating& differentGenesis() noexcept;

  private:
    std::int_fast32_t _value;
};


class Message
{};


class Peer : public std::enable_shared_from_this<Peer>
{
  public:
    //=========================
    /**
     * Classes that are required for network messages handling.
     */
    struct Context
    {
        lk::Core& core; //! for operating with blockchain
        lk::Host& host; //! for operating with host data
    };

    enum class State
    {
        JUST_ESTABLISHED,
        REQUESTED_BLOCKS,
        SYNCHRONISED
    };

    struct IdentityInfo
    {
        net::Endpoint endpoint;
        lk::Address address;

        static IdentityInfo deserialize(base::SerializationIArchive& ia);
        void serialize(base::SerializationOArchive& oa) const;
    };
    //=========================
    static std::shared_ptr<Peer> accepted(std::shared_ptr<net::Session> session, Context context);
    static std::shared_ptr<Peer> connected(std::shared_ptr<net::Session> session, Context context);

    ~Peer() = default;
    //=========================
    base::Time getLastSeen() const;
    net::Endpoint getEndpoint() const;
    net::Endpoint getPublicEndpoint() const;
    bool wasConnectedTo() const noexcept;
    //=========================
    const lk::Address& getAddress() const noexcept;
    //=========================
    IdentityInfo getInfo() const;
    bool isClosed() const;
    //=========================
    void requestLookup(const lk::Address& address, uint8_t alpha);
    void requestBlock(const base::Sha256& block_hash);

    void sendBlock(const base::Sha256& block_hash, const lk::Block& block);
    void sendTransaction(const lk::Transaction& tx);
    //=========================
    /**
     * If the peer was accepted, it responds to it whether the acception was successful or not.
     * If the peer was connected to it waits for reply.
     */
    void startSession();

    template<typename T>
    void endSession(T last_message);

  private:
    //=========================
    Peer(std::shared_ptr<net::Session> session,
         bool was_connected_to,
         boost::asio::io_context& io_context,
         lk::PeerPoolBase& non_handshaked_pool,
         lk::KademliaPeerPoolBase& handshaked_pool,
         lk::Core& core,
         lk::Host& host);
    //=========================
    /*
     * Tries to add peer to a peer pool, to which it is attached.
     * @return true if success, false - otherwise
     */
    bool tryAddToPool();
    void detachFromPools(); // only called inside onClose or inside destructor
    //=========================
    /*
     * Handler of session messages.
     */
    class Handler : public net::Session::Handler
    {
      public:
        Handler(std::weak_ptr<Peer> peer);

        void onReceive(const base::Bytes& bytes) override;
        void onClose() override;

      private:
        std::weak_ptr<Peer> _peer;
    };

    //===========================================================

    class Requests
    {
        using MessageIdType = std::uint16_t;

        class SessionHandler : public net::Session::Handler
        {
          public:
            SessionHandler(Requests& requests)
              : _r{ requests }
            {}

            void onReceive(const base::Bytes& bytes) override { _r.onMessageReceive(bytes); }

            void onClose() override { _r.onClose(); }

          private:
            Requests& _r;
        };

      public:
        Requests(std::weak_ptr<net::Session> session, boost::asio::io_context& io_context)
          : _session{ std::move(session) }
          , _io_context{ io_context }
        {
            if (auto s = _session.lock()) {
                s->setHandler(std::make_shared<SessionHandler>(*this));
            }
            else {
                RAISE_ERROR(net::ClosedSession);
            }
        }

        void onMessageReceive(const base::Bytes& received_bytes)
        {
            base::SerializationIArchive ia(received_bytes);
            auto msg_id = ia.deserialize<MessageIdType>();

            _active_requests.disownIf([msg_id, &received_bytes](Request& request) {
                if (request.getId() == msg_id) {
                    request.runCallback(received_bytes);
                    return true;
                }
                else {
                    return false;
                }
            });
        }

        void onClose() {}

        class Request : std::enable_shared_from_this<Request>
        {
          public:
            using Callback = std::function<void(const base::Bytes&)>;

            Request(base::OwningPool<Request>& active_requests,
                    MessageIdType id,
                    Callback callback,
                    boost::asio::io_context& io_context)
              : _active_requests{ active_requests }
              , _id{ id }
              , _callback{ std::move(callback) }
              , _timer{ io_context }
            {
                _timer.expires_after(std::chrono::seconds(base::config::NET_PING_FREQUENCY));
                _timer.async_wait([request_weak = weak_from_this()](const boost::system::error_code& ec) {
                    if (auto r = request_weak.lock()) {
                        if (!r->_is_already_processed.test_and_set()) {
                            r->_active_requests.disown(r.get());
                        }
                    }
                });
            }


            MessageIdType getId() const noexcept { return _id; }


            void runCallback(const base::Bytes& data)
            {
                if (!_is_already_processed.test_and_set()) {
                    _callback(data);
                }
            }

          private:
            base::OwningPool<Request>& _active_requests;
            MessageIdType _id;
            Callback _callback;
            boost::asio::steady_timer _timer;
            std::atomic_flag _is_already_processed;
        };


        void requestSilent(const Message& msg)
        {
            if (auto s = _session.lock()) {
                s->send(prepareMessage(msg));
            }
            else {
                RAISE_ERROR(net::SendOnClosedConnection, "attempt to request on closed connection");
            }
        }


        void requestWaitResponseById(const Message& msg, Request::Callback callback)
        {
            if (auto s = _session.lock()) {
                auto r =
                  std::make_shared<Request>(_active_requests, _next_message_id++, std::move(callback), _io_context);
                _active_requests.own(std::move(r));
                s->send(prepareMessage(msg));
            }
            else {
                RAISE_ERROR(net::SendOnClosedConnection, "attempt to request on closed connection");
            }
        }

      private:
        std::weak_ptr<net::Session> _session;
        boost::asio::io_context& _io_context;
        std::vector<Message> _requested;
        MessageIdType _next_message_id{ 0 };
        base::OwningPool<Request> _active_requests;

        base::Bytes prepareMessage(const Message& msg)
        {
            base::SerializationOArchive oa;
            oa.serialize(_next_message_id++);
            oa.serialize(msg);
            return std::move(oa).getBytes();
        }
    };

    //===========================================================

    template<typename M>
    void sendMessage(const M& msg, net::Connection::SendHandler on_send = {});
    //=========================
    std::shared_ptr<net::Session> _session;
    bool _is_started{ false };
    bool _was_connected_to; // peer was connected to or accepted?
    //=========================
    boost::asio::io_context& _io_context;
    //=========================
    Rating _rating;
    State _state{ State::JUST_ESTABLISHED };
    std::optional<net::Endpoint> _endpoint_for_incoming_connections;
    lk::Address _address;

    void setServerEndpoint(net::Endpoint endpoint);
    void setState(State state);
    State getState() const noexcept;
    //=========================
    class Synchronizer
    {
      public:
        explicit Synchronizer(Peer& peer);

        void handleReceivedTopBlockHash(const base::Sha256& peers_top_block);
        bool handleReceivedBlock(const base::Sha256& hash, const Block& block);
        bool isSynchronised() const;

      private:
        Peer& _peer;
        std::optional<base::Sha256> _requested_block;
        std::vector<lk::Block> _sync_blocks;

        void requestBlock(base::Sha256 block_hash);
    };

    Synchronizer _synchronizer{ *this };
    //================
    lk::PeerPoolBase& _non_handshaked_pool;
    bool _is_attached_to_handshaked_pool{ false };
    lk::KademliaPeerPoolBase& _handshaked_pool;
    lk::Core& _core;
    lk::Host& _host;
    //=========================
    void process(const base::Bytes& received_data);
    //=========================
    void handle(msg::Connect&& msg);
    void handle(msg::CannotAccept&& msg);
    void handle(msg::Accepted&& msg);
    void handle(msg::Ping&& msg);
    void handle(msg::Pong&& msg);
    void handle(msg::Lookup&& msg);
    void handle(msg::LookupResponse&& msg);
    void handle(msg::Transaction&& msg);
    void handle(msg::GetBlock&& msg);
    void handle(msg::Block&& msg);
    void handle(msg::BlockNotFound&& msg);
    void handle(msg::Close&& msg);
    //=========================
};


namespace msg
{

struct Connect : Message
{
    static constexpr Type TYPE_ID = Type::CONNECT;

    lk::Address address;
    std::uint16_t public_port;
    base::Sha256 top_block_hash;

    void serialize(base::SerializationOArchive& oa) const;
    static Connect deserialize(base::SerializationIArchive& ia);
};


struct CannotAccept : Message
{
    static constexpr Type TYPE_ID = Type::CANNOT_ACCEPT;

    DEFINE_ENUM_CLASS_WITH_STRING_CONVERSIONS(RefusionReason,
                                              std::uint8_t,
                                              (NOT_AVAILABLE)(BUCKET_IS_FULL)(BAD_RATING));

    RefusionReason why_not_accepted;
    std::vector<Peer::IdentityInfo> peers_info;

    void serialize(base::SerializationOArchive& oa) const;
    static CannotAccept deserialize(base::SerializationIArchive& ia);
};


struct Accepted : Message
{
    static constexpr Type TYPE_ID = Type::ACCEPTED;

    lk::Address address;
    uint16_t public_port; // zero public port states that peer didn't provide information about his public endpoint
    base::Sha256 top_block_hash;

    void serialize(base::SerializationOArchive& oa) const;
    static Accepted deserialize(base::SerializationIArchive& ia);
};


struct Ping : Message
{
    static constexpr Type TYPE_ID = Type::PING;

    void serialize(base::SerializationOArchive& oa) const;
    static Ping deserialize(base::SerializationIArchive& ia);
};


struct Pong : Message
{
    static constexpr Type TYPE_ID = Type::PONG;

    void serialize(base::SerializationOArchive& oa) const;
    static Pong deserialize(base::SerializationIArchive& ia);
};


struct Lookup : Message
{
    static constexpr Type TYPE_ID = Type::LOOKUP;

    lk::Address address;
    std::uint8_t selection_size;

    void serialize(base::SerializationOArchive& oa) const;
    static Lookup deserialize(base::SerializationIArchive& ia);
};


struct LookupResponse : Message
{
    static constexpr Type TYPE_ID = Type::LOOKUP_RESPONSE;

    lk::Address address;
    std::vector<Peer::IdentityInfo> peers_info;

    void serialize(base::SerializationOArchive& oa) const;
    static LookupResponse deserialize(base::SerializationIArchive& ia);
};


struct Transaction : Message
{
    static constexpr Type TYPE_ID = Type::TRANSACTION;

    lk::Transaction tx;

    void serialize(base::SerializationOArchive& oa) const;
    static Transaction deserialize(base::SerializationIArchive& ia);
};


struct GetBlock : Message
{
    static constexpr Type TYPE_ID = Type::GET_BLOCK;

    base::Sha256 block_hash;

    void serialize(base::SerializationOArchive& oa) const;
    static GetBlock deserialize(base::SerializationIArchive& ia);
};


struct Block : Message
{
    static constexpr Type TYPE_ID = Type::BLOCK;

    base::Sha256 block_hash;
    lk::Block block;

    void serialize(base::SerializationOArchive& oa) const;
    static Block deserialize(base::SerializationIArchive& ia);
};


struct BlockNotFound : Message
{
    static constexpr Type TYPE_ID = Type::BLOCK_NOT_FOUND;

    base::Sha256 block_hash;

    void serialize(base::SerializationOArchive& oa) const;
    static BlockNotFound deserialize(base::SerializationIArchive& ia);
};


struct Close : Message
{
    static constexpr Type TYPE_ID = Type::CLOSE;

    void serialize(base::SerializationOArchive& oa) const;
    static Close deserialize(base::SerializationIArchive& ia);
};
}


class PeerPoolBase
{
  public:
    virtual bool tryAddPeer(std::shared_ptr<Peer> peer) = 0;
    virtual void removePeer(std::shared_ptr<Peer> peer) = 0;
    virtual void removePeer(Peer* peer) = 0;

    virtual void forEachPeer(std::function<void(const Peer&)> f) const = 0;
    virtual void forEachPeer(std::function<void(Peer&)> f) = 0;

    virtual bool hasPeerWithEndpoint(const net::Endpoint& endpoint) const = 0;

    std::vector<Peer::IdentityInfo> allPeersInfo() const;
};

class KademliaPeerPoolBase : public PeerPoolBase
{
  public:
    virtual std::vector<Peer::IdentityInfo> lookup(const lk::Address& address, std::size_t alpha) = 0;
};

}

#include "peer.tpp"
