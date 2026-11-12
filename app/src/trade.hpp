#pragma once

#include <mii/mii.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Sending Miis between Vitas over ad-hoc wireless (no access point), as in
// docs/ARCHITECTURE.md. Desktop builds use UDP multicast on this computer
// instead, so two copies of the app can trade while testing.
//
// Every message is broadcast; each carries the sender's session id (random
// per screen) and the id it is meant for. A receiving Vita announces itself
// once a second; a sender offers a Mii (its CharInfo, so the Mii keeps its
// identity) until the receiver accepts or declines.
namespace trade {

enum class Type : uint8_t { HELLO = 1, OFFER = 2, ACCEPT = 3, DECLINE = 4 };

struct Message {
    Type type = Type::HELLO;
    uint32_t from = 0, to = 0; // session ids; to = 0: everyone
    uint32_t offer = 0;        // which offer an OFFER / ACCEPT / DECLINE is about
    bool receiving = false;    // HELLO: the sender of this message takes Miis
    std::string name;          // HELLO and OFFER: who is sending (console user name)
    mii_charinfo_t mii {};     // OFFER
};

// The bytes on the air: "MIIT", version, type, from, to, offer, then the
// type's fields, then a CRC16 of everything before it. decode() rejects
// anything that does not check out.
std::vector<uint8_t> encode(const Message& m);
bool decode(const uint8_t* data, size_t size, Message* out);

// Broadcast datagrams: the ad-hoc network on the Vita, multicast elsewhere.
class Transport
{
  public:
    virtual ~Transport() = default;
    virtual bool broadcast(const std::vector<uint8_t>& data) = 0;
    // Next datagram, without waiting: its size, or -1 if there is none.
    virtual int receive(uint8_t* buffer, int capacity) = 0;
};

// Joining the ad-hoc network. On the Vita this shows the system's network
// dialog (both consoles must be in it); elsewhere there is nothing to join.
class Link
{
  public:
    enum class State { IDLE, JOINING, CONNECTED, FAILED };
    ~Link();
    void start();
    // Call every frame while JOINING.
    State poll();
    // The datagram channel, once CONNECTED (nullptr and `error` on failure).
    std::unique_ptr<Transport> open(std::string* error);
    // Leave the network.
    void stop();
    State state() const { return current; }

  private:
    State current = State::IDLE;
};

// The console's user name (a stand-in on desktop builds).
std::string userName();

// One trading screen's side of the conversation. Call update() every frame.
class Session
{
  public:
    struct Peer {
        uint32_t id;
        std::string name;
        uint64_t lastSeenUs;
    };

    // receiving: announce this console and take offers
    Session(std::unique_ptr<Transport> transport, bool receiving, std::string name);

    void update(uint64_t nowUs);

    // Receiving: an offer arrived; answer with answer(). Repeats of an
    // offer already being decided are not reported again.
    std::function<void(const Peer& from, uint32_t offer, const mii_charinfo_t& mii)> onOffer;
    void answer(uint32_t to, uint32_t offer, bool accept);

    // Sending: the consoles currently taking Miis.
    const std::vector<Peer>& receivers() const { return peers; }
    // Offers `mii` to a receiver; onAnswer reports the outcome
    // (accepted, declined, or nobody answered: timedOut).
    void send(uint32_t to, const mii_charinfo_t& mii);
    std::function<void(bool accepted, bool timedOut)> onAnswer;
    bool sending() const { return pending.active; }

  private:
    void post(const Message& m);

    std::unique_ptr<Transport> transport;
    bool receiving;
    std::string name;
    uint32_t self;
    uint64_t lastHello = 0;
    std::vector<Peer> peers;

    struct Pending {
        bool active = false;
        uint32_t to = 0, offer = 0;
        mii_charinfo_t mii;
        uint64_t started = 0, lastSent = 0;
    } pending;

    // offers this receiver has seen: (from, offer) -> answer (0 deciding,
    // 1 accepted, 2 declined), so repeats are answered the same way
    struct Seen {
        uint32_t from, offer;
        int answer;
    };
    std::vector<Seen> seen;
    uint32_t nextOffer = 1;
};

} // namespace trade
