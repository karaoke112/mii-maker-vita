#include "trade.hpp"

#include "mii_library.hpp" // platformRandomBytes

#include <borealis.hpp>

#include <algorithm>
#include <cstring>

#if defined(__PSV__)
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/net/net.h>
#include <psp2/netcheck_dialog.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/pspnet_adhocctl.h>
#include <psp2/sysmodule.h>
#include <psp2/system_param.h>
#else
#include <arpa/inet.h>
#include <cstdlib>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace trade {

// ---- messages ------------------------------------------------------------

static const uint8_t kMagic[4] = { 'M', 'I', 'I', 'T' };
static constexpr uint8_t kVersion = 1;
static constexpr size_t kNameBytes = 40; // UTF-8, zero padded
static constexpr size_t kHeader = 4 + 1 + 1 + 4 + 4 + 4;

static void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        out.push_back((uint8_t)(v >> (8 * i)));
}

static uint32_t get32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void putName(std::vector<uint8_t>& out, const std::string& name)
{
    // cut at a whole UTF-8 character
    size_t n = std::min(name.size(), kNameBytes - 1);
    while (n > 0 && n < name.size() && ((uint8_t)name[n] & 0xC0) == 0x80)
        n--;
    for (size_t i = 0; i < kNameBytes; i++)
        out.push_back(i < n ? (uint8_t)name[i] : 0);
}

std::vector<uint8_t> encode(const Message& m)
{
    std::vector<uint8_t> out(kMagic, kMagic + 4);
    out.push_back(kVersion);
    out.push_back((uint8_t)m.type);
    put32(out, m.from);
    put32(out, m.to);
    put32(out, m.offer);
    if (m.type == Type::HELLO) {
        out.push_back(m.receiving ? 1 : 0);
        putName(out, m.name);
    } else if (m.type == Type::OFFER) {
        putName(out, m.name);
        uint8_t ci[MII_CHARINFO_SIZE];
        mii_charinfo_encode(&m.mii, ci);
        out.insert(out.end(), ci, ci + sizeof(ci));
    }
    const uint16_t crc = mii_crc16(out.data(), out.size(), 0);
    out.push_back((uint8_t)(crc >> 8));
    out.push_back((uint8_t)crc);
    return out;
}

bool decode(const uint8_t* d, size_t size, Message* m)
{
    if (size < kHeader + 2 || std::memcmp(d, kMagic, 4) != 0 || d[4] != kVersion)
        return false;
    if (mii_crc16(d, size, 0) != 0) // the CRC is big-endian at the end: the whole checks to 0
        return false;
    const size_t body = size - 2;
    m->type = (Type)d[5];
    m->from = get32(d + 6);
    m->to = get32(d + 10);
    m->offer = get32(d + 14);
    const uint8_t* p = d + kHeader;
    auto name = [](const uint8_t* q) { return std::string((const char*)q, strnlen((const char*)q, kNameBytes)); };
    switch (m->type) {
    case Type::HELLO:
        if (body != kHeader + 1 + kNameBytes)
            return false;
        m->receiving = p[0] != 0;
        m->name = name(p + 1);
        return true;
    case Type::OFFER:
        if (body != kHeader + kNameBytes + MII_CHARINFO_SIZE)
            return false;
        m->name = name(p);
        return mii_charinfo_decode(&m->mii, p + kNameBytes, MII_CHARINFO_SIZE) == MII_OK &&
            mii_verify(&m->mii, 0) == MII_FIELD_COUNT;
    case Type::ACCEPT:
    case Type::DECLINE:
        return body == kHeader;
    }
    return false;
}

// ---- the network ---------------------------------------------------------

static constexpr uint16_t kPort = 0x4D49; // "MI"

#if defined(__PSV__)

// The app's ad-hoc id (both consoles run the same title, so they meet).
static const char kAdhocId[] = "MIIV00001";

static bool g_netUp = false;

static void netUp()
{
    if (g_netUp)
        return;
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC);
    static uint8_t pool[256 * 1024];
    SceNetInitParam init;
    init.memory = pool;
    init.size = sizeof(pool);
    init.flags = 0;
    const int r = sceNetInit(&init);
    brls::Logger::info("trade: sceNetInit 0x{:08X}", (unsigned)r); // fails harmlessly if already up
    g_netUp = true;
}

class PdpTransport : public Transport
{
  public:
    explicit PdpTransport(int id)
        : id(id)
    {
    }
    ~PdpTransport() override { sceNetAdhocPdpDelete(id, 0); }

    bool broadcast(const std::vector<uint8_t>& data) override
    {
        SceNetEtherAddr all;
        std::memset(&all, 0xFF, sizeof(all));
        return sceNetAdhocPdpSend(id, &all, kPort, data.data(), (int)data.size(), 0, SCE_NET_ADHOC_F_NONBLOCK) >= 0;
    }

    int receive(uint8_t* buffer, int capacity) override
    {
        SceNetEtherAddr from;
        SceUShort16 port = 0;
        int len = capacity;
        const int r = sceNetAdhocPdpRecv(id, &from, &port, buffer, &len, 0, SCE_NET_ADHOC_F_NONBLOCK);
        return r < 0 ? -1 : len;
    }

  private:
    int id;
};

void Link::start()
{
    netUp();
    int r = sceNetAdhocInit();
    brls::Logger::info("trade: sceNetAdhocInit 0x{:08X}", (unsigned)r);
    SceNetAdhocctlAdhocId adhocId;
    std::memset(&adhocId, 0, sizeof(adhocId));
    adhocId.type = SCE_NET_ADHOCCTL_ADHOCTYPE_RESERVED;
    std::memcpy(adhocId.data, kAdhocId, SCE_NET_ADHOCCTL_ADHOCID_LEN);
    r = sceNetAdhocctlInit(&adhocId);
    brls::Logger::info("trade: sceNetAdhocctlInit 0x{:08X}", (unsigned)r);

    static SceNetAdhocctlGroupName group;
    std::memset(&group, 0, sizeof(group));
    SceNetCheckDialogParam param;
    sceNetCheckDialogParamInit(&param);
    param.mode = SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN;
    param.groupName = &group;
    std::memcpy(param.npCommunicationId.data, kAdhocId, 9);
    param.npCommunicationId.term = '\0';
    param.npCommunicationId.num = 0;
    param.timeoutUs = 0;
    r = sceNetCheckDialogInit(&param);
    brls::Logger::info("trade: sceNetCheckDialogInit 0x{:08X}", (unsigned)r);
    if (r < 0) {
        current = State::FAILED;
        return;
    }
    brls::Application::blockInputs(); // the dialog has the buttons now
    current = State::JOINING;
}

Link::State Link::poll()
{
    if (current != State::JOINING)
        return current;
    if (sceNetCheckDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return current;
    SceNetCheckDialogResult result;
    std::memset(&result, 0, sizeof(result));
    sceNetCheckDialogGetResult(&result);
    sceNetCheckDialogTerm();
    brls::Application::unblockInputs();
    brls::Logger::info("trade: dialog result {}", result.result);
    current = result.result == SCE_COMMON_DIALOG_RESULT_OK ? State::CONNECTED : State::FAILED;
    return current;
}

std::unique_ptr<Transport> Link::open(std::string* error)
{
    SceNetEtherAddr mac;
    std::memset(&mac, 0, sizeof(mac));
    sceNetAdhocctlGetEtherAddr(&mac);
    const int id = sceNetAdhocPdpCreate(&mac, kPort, 0x2000, 0);
    if (id < 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "sceNetAdhocPdpCreate 0x%08X", (unsigned)id);
        *error = buf;
        return nullptr;
    }
    return std::unique_ptr<Transport>(new PdpTransport(id));
}

void Link::stop()
{
    if (current == State::JOINING) {
        sceNetCheckDialogAbort();
        sceNetCheckDialogTerm();
        brls::Application::unblockInputs();
    }
    if (current != State::IDLE) {
        sceNetAdhocctlTerm();
        sceNetAdhocTerm();
    }
    current = State::IDLE;
}

std::string userName()
{
    char name[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE] = {};
    if (sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, (SceChar8*)name, sizeof(name)) < 0 || !name[0])
        return "PS Vita";
    return name;
}

#else

// Multicast on the loopback interface: every copy of the app on this
// computer hears every other.
static const char kGroup[] = "239.255.77.73";

class UdpTransport : public Transport
{
  public:
    explicit UdpTransport(int fd)
        : fd(fd)
    {
    }
    ~UdpTransport() override { close(fd); }

    bool broadcast(const std::vector<uint8_t>& data) override
    {
        sockaddr_in to {};
        to.sin_family = AF_INET;
        to.sin_port = htons(kPort);
        inet_pton(AF_INET, kGroup, &to.sin_addr);
        return sendto(fd, data.data(), data.size(), 0, (sockaddr*)&to, sizeof(to)) == (ssize_t)data.size();
    }

    int receive(uint8_t* buffer, int capacity) override
    {
        const ssize_t n = recv(fd, buffer, capacity, MSG_DONTWAIT);
        return n < 0 ? -1 : (int)n;
    }

  private:
    int fd;
};

void Link::start() { current = State::CONNECTED; }
Link::State Link::poll() { return current; }
void Link::stop() { current = State::IDLE; }

std::unique_ptr<Transport> Link::open(std::string* error)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        *error = "socket";
        return nullptr;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ip_mreq group {};
    inet_pton(AF_INET, kGroup, &group.imr_multiaddr);
    inet_pton(AF_INET, "127.0.0.1", &group.imr_interface);
    unsigned char loop = 1;
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0 ||
        setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group)) < 0 ||
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &group.imr_interface, sizeof(group.imr_interface)) < 0 ||
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        *error = "multicast setup failed";
        close(fd);
        return nullptr;
    }
    return std::unique_ptr<Transport>(new UdpTransport(fd));
}

std::string userName()
{
    const char* u = std::getenv("USER");
    return std::string(u && *u ? u : "PC") + " (" + std::to_string(getpid() % 1000) + ")";
}

#endif

Link::~Link()
{
    stop();
}

// ---- the conversation ----------------------------------------------------

static constexpr uint64_t kHelloEveryUs = 1000000, kPeerGoneUs = 4000000;
static constexpr uint64_t kResendUs = 400000, kGiveUpUs = 60000000;

Session::Session(std::unique_ptr<Transport> transport, bool receiving, std::string name)
    : transport(std::move(transport))
    , receiving(receiving)
    , name(std::move(name))
{
    do
        platformRandomBytes(reinterpret_cast<uint8_t*>(&self), sizeof(self));
    while (self == 0);
}

void Session::post(const Message& m)
{
    if (transport)
        transport->broadcast(encode(m));
}

void Session::update(uint64_t now)
{
    if (!transport)
        return;
    if (now - lastHello >= kHelloEveryUs) {
        lastHello = now;
        Message hello;
        hello.type = Type::HELLO;
        hello.from = self;
        hello.receiving = receiving;
        hello.name = name;
        post(hello);
    }

    uint8_t buffer[512];
    int n;
    while ((n = transport->receive(buffer, sizeof(buffer))) >= 0) {
        Message m;
        if (!decode(buffer, (size_t)n, &m) || m.from == self || (m.to != 0 && m.to != self))
            continue;
        switch (m.type) {
        case Type::HELLO: {
            if (!m.receiving)
                break;
            auto it = std::find_if(peers.begin(), peers.end(), [&](const Peer& p) { return p.id == m.from; });
            if (it == peers.end())
                peers.push_back({ m.from, m.name, now });
            else {
                it->name = m.name;
                it->lastSeenUs = now;
            }
            break;
        }
        case Type::OFFER: {
            if (!receiving)
                break;
            auto it = std::find_if(seen.begin(), seen.end(),
                [&](const Seen& s) { return s.from == m.from && s.offer == m.offer; });
            if (it != seen.end()) {
                if (it->answer)
                    answer(m.from, m.offer, it->answer == 1); // our answer got lost: again
                break;
            }
            seen.push_back({ m.from, m.offer, 0 });
            if (seen.size() > 64)
                seen.erase(seen.begin());
            if (onOffer)
                onOffer(Peer { m.from, m.name, now }, m.offer, m.mii);
            break;
        }
        case Type::ACCEPT:
        case Type::DECLINE:
            if (pending.active && m.from == pending.to && m.offer == pending.offer) {
                pending.active = false;
                if (onAnswer)
                    onAnswer(m.type == Type::ACCEPT, false);
            }
            break;
        }
    }

    peers.erase(std::remove_if(peers.begin(), peers.end(), [&](const Peer& p) { return now - p.lastSeenUs > kPeerGoneUs; }),
        peers.end());

    if (pending.active) {
        if (now - pending.started > kGiveUpUs) {
            pending.active = false;
            if (onAnswer)
                onAnswer(false, true);
        } else if (now - pending.lastSent >= kResendUs) {
            pending.lastSent = now;
            Message offer;
            offer.type = Type::OFFER;
            offer.from = self;
            offer.to = pending.to;
            offer.offer = pending.offer;
            offer.name = name;
            offer.mii = pending.mii;
            post(offer);
        }
    }
}

void Session::answer(uint32_t to, uint32_t offer, bool accept)
{
    for (Seen& s : seen)
        if (s.from == to && s.offer == offer)
            s.answer = accept ? 1 : 2;
    Message m;
    m.type = accept ? Type::ACCEPT : Type::DECLINE;
    m.from = self;
    m.to = to;
    m.offer = offer;
    // datagrams can be lost: say it a few times (repeats are harmless)
    for (int i = 0; i < 3; i++)
        post(m);
}

void Session::send(uint32_t to, const mii_charinfo_t& mii)
{
    pending.active = true;
    pending.to = to;
    pending.offer = nextOffer++;
    pending.mii = mii;
    pending.started = brls::getCPUTimeUsec();
    pending.lastSent = 0;
}

} // namespace trade
