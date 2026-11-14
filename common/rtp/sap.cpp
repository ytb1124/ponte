#include "sap.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sstream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "compat/net_compat.h"
#include "log.h"

using namespace pan::rtp;
using json = nlohmann::json;

namespace
{
    constexpr const char* kSapGroup = "239.255.255.255";    //AES67 / Dante: admin scope relative SAP address
    constexpr uint16_t kSapPort = 9875;

    uint64_t Fnv1a(const std::string& s)
    {
        uint64_t nHash = 0xcbf29ce484222325ull;
        for(unsigned char c : s) { nHash = (nHash ^ c) * 0x100000001b3ull; }
        return nHash;
    }

    uint16_t MsgHash(const std::string& sSdp)
    {
        auto nHash = Fnv1a(sSdp);
        auto n16 = static_cast<uint16_t>(nHash ^ (nHash >> 16) ^ (nHash >> 32) ^ (nHash >> 48));
        return n16 == 0 ? 1 : n16;  //0 is reserved for "no hash"
    }
}

std::vector<uint8_t> pan::rtp::BuildSapPacket(bool bDelete, uint16_t nMsgHash, const std::string& sOriginIp, const std::string& sSdp)
{
    std::vector<uint8_t> vPacket;
    //V=1, A=0 (ipv4), R=0, T=announce/delete, E=0, C=0
    vPacket.push_back(bDelete ? 0x24 : 0x20);
    vPacket.push_back(0);                       //no authentication
    vPacket.push_back(static_cast<uint8_t>(nMsgHash >> 8));
    vPacket.push_back(static_cast<uint8_t>(nMsgHash & 0xff));
    in_addr origin{};
    inet_pton(AF_INET, sOriginIp.c_str(), &origin);
    auto pOrigin = reinterpret_cast<const uint8_t*>(&origin.s_addr);
    vPacket.insert(vPacket.end(), pOrigin, pOrigin+4);
    static const char kType[] = "application/sdp";
    vPacket.insert(vPacket.end(), kType, kType+sizeof(kType));  //includes the terminating 0
    vPacket.insert(vPacket.end(), sSdp.begin(), sSdp.end());
    return vPacket;
}

SapAnnouncer::~SapAnnouncer()
{
    Stop();
}

void SapAnnouncer::Start(Provider provider, int nIntervalS, int nTtl)
{
    Stop();
    m_provider = std::move(provider);
    m_nIntervalS = std::max(1, nIntervalS);
    m_nTtl = std::clamp(nTtl, 1, 255);
    m_bRun = true;
    m_thread = std::thread([this]() { Loop(); });
    LOG_INFO("sap") << "announcing on " << kSapGroup << ":" << kSapPort << " every " << m_nIntervalS << "s";
}

void SapAnnouncer::Stop()
{
    if(!m_bRun.exchange(false)) { return; }
    m_wake.notify_all();
    if(m_thread.joinable()) { m_thread.join(); }

    std::lock_guard<std::mutex> lg(m_mutex);
    for(const auto& [sKey, entry] : m_mEntries) { Send(entry, true); }
    m_mEntries.clear();
    CloseSockets();
}

void SapAnnouncer::Loop()
{
    while(m_bRun)
    {
        auto vSessions = m_provider ? m_provider() : std::vector<SapSession>{};
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lg(m_mutex);
            for(auto& [sKey, entry] : m_mEntries) { entry.bSeen = false; }

            for(auto& sap : vSessions)
            {
                if(sap.session.vLegs.size() != 1 || sap.sOriginIp.empty()) { continue; }
                const auto& leg = sap.session.vLegs[0];
                auto sKey = sap.sInterface + "|" + leg.sMulticast + "|" + std::to_string(leg.nPort);

                //stable session id per stream, version follows the content
                auto nSessId = (Fnv1a(sKey + "|" + sap.sOriginIp) & 0x7fffffffull) + 1;
                auto sBody = GenerateSdp(sap.session, sap.sOriginIp, {sap.sOriginIp}, nSessId, 1);
                auto nBodyHash = Fnv1a(sBody);

                auto it = m_mEntries.find(sKey);
                bool bNew = it == m_mEntries.end();
                bool bChanged = !bNew && (it->second.nBodyHash != nBodyHash || it->second.sOriginIp != sap.sOriginIp);
                if(bChanged)
                {
                    Send(it->second, true);     //retire the old description first
                }
                if(bNew || bChanged)
                {
                    Entry entry;
                    entry.sInterface = sap.sInterface;
                    entry.sOriginIp = sap.sOriginIp;
                    entry.nBodyHash = nBodyHash;
                    entry.nVersion = bNew ? 1 : it->second.nVersion + 1;
                    entry.sSdp = GenerateSdp(sap.session, sap.sOriginIp, {sap.sOriginIp}, nSessId, entry.nVersion);
                    entry.nMsgHash = MsgHash(entry.sSdp);
                    entry.bSeen = true;
                    //a failed send leaves lastSent old, so the next second retries
                    if(Send(entry, false)) { entry.lastSent = now; }
                    m_mEntries[sKey] = std::move(entry);
                    LOG_INFO("sap") << (bNew ? "announce " : "update ") << sKey << " (" << sap.session.sName << ")";
                    continue;
                }

                it->second.bSeen = true;
                if(now - it->second.lastSent >= std::chrono::seconds(m_nIntervalS))
                {
                    if(Send(it->second, false)) { it->second.lastSent = now; }
                }
            }

            for(auto it = m_mEntries.begin(); it != m_mEntries.end();)
            {
                if(it->second.bSeen) { ++it; continue; }
                LOG_INFO("sap") << "delete " << it->first;
                Send(it->second, true);
                it = m_mEntries.erase(it);
            }
        }

        std::unique_lock<std::mutex> lk(m_wakeMutex);
        m_wake.wait_for(lk, std::chrono::seconds(1), [this]() { return !m_bRun.load(); });
    }
}

int SapAnnouncer::SocketFor(const std::string& sInterface)
{
    if(auto it = m_mSockets.find(sInterface); it != m_mSockets.end()) { return it->second; }

    int nSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(nSocket < 0) { return -1; }
    if(!compat::SetMulticastEgress(nSocket, sInterface))
    {
        close(nSocket);
        return -1;  //interface down - retried on the next send
    }
    uint8_t nTtl = static_cast<uint8_t>(m_nTtl);
    setsockopt(nSocket, IPPROTO_IP, IP_MULTICAST_TTL, &nTtl, sizeof(nTtl));
    uint8_t nLoop = 1;  //local listeners (sap browsers on this mac) see us too
    setsockopt(nSocket, IPPROTO_IP, IP_MULTICAST_LOOP, &nLoop, sizeof(nLoop));
    m_mSockets[sInterface] = nSocket;
    return nSocket;
}

void SapAnnouncer::CloseSockets()
{
    for(const auto& [sInterface, nSocket] : m_mSockets) { close(nSocket); }
    m_mSockets.clear();
}

bool SapAnnouncer::Send(const Entry& entry, bool bDelete)
{
    int nSocket = SocketFor(entry.sInterface);
    if(nSocket < 0)
    {
        if(m_nSendErrors++ == 0 || m_bLastSendOk) { LOG_WARN("sap") << "interface " << entry.sInterface << " not usable"; }
        m_bLastSendOk = false;
        return false;
    }
    //a deletion carries only the origin line, which identifies the session (RFC 2974 §6)
    std::string sPayload = entry.sSdp;
    if(bDelete)
    {
        auto nStart = sPayload.find("o=");
        auto nEnd = nStart == std::string::npos ? nStart : sPayload.find("\r\n", nStart);
        if(nStart != std::string::npos && nEnd != std::string::npos)
        {
            sPayload = sPayload.substr(nStart, nEnd - nStart + 2);
        }
    }
    auto vPacket = BuildSapPacket(bDelete, entry.nMsgHash, entry.sOriginIp, sPayload);

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kSapPort);
    inet_pton(AF_INET, kSapGroup, &dest.sin_addr);
    if(sendto(nSocket, vPacket.data(), vPacket.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0)
    {
        //log on the ok -> failing edge only (a retry runs every second).
        //EHOSTUNREACH on macos usually = "Local Network" permission missing
        int nErr = errno;
        if(m_nSendErrors++ == 0 || m_bLastSendOk) { LOG_WARN("sap") << "send on " << entry.sInterface << " failed: " << strerror(nErr); }
        m_bLastSendOk = false;
        //the interface may have gone away - rebuild the socket next time
        close(nSocket);
        m_mSockets.erase(entry.sInterface);
        return false;
    }
    if(!m_bLastSendOk) { LOG_INFO("sap") << "sending on " << entry.sInterface << " works"; }
    m_bLastSendOk = true;
    (bDelete ? m_nDeletions : m_nAnnouncements)++;
    return true;
}

json SapAnnouncer::GetStatusJson() const
{
    json js;
    js["enabled"] = m_bRun.load();
    js["interval_s"] = m_nIntervalS;
    js["announcements"] = m_nAnnouncements.load();
    js["deletions"] = m_nDeletions.load();
    js["send_errors"] = m_nSendErrors.load();
    js["sessions"] = json::array();
    std::lock_guard<std::mutex> lg(m_mutex);
    for(const auto& [sKey, entry] : m_mEntries)
    {
        js["sessions"].push_back({{"key", sKey}, {"origin", entry.sOriginIp}, {"version", entry.nVersion}});
    }
    return js;
}

//---------------------------------------------------------------- listener

namespace
{
    //"o=- 123 4 IN IP4 10.0.0.1" -> key "- 123 10.0.0.1", version 4
    bool ParseOrigin(const std::string& sSdp, std::string& sKey, uint64_t& nVersion)
    {
        auto nStart = sSdp.find("o=");
        if(nStart == std::string::npos) { return false; }
        auto nEnd = sSdp.find_first_of("\r\n", nStart);
        std::istringstream line(sSdp.substr(nStart+2, nEnd == std::string::npos ? std::string::npos : nEnd-nStart-2));
        std::string sUser, sSessId, sVersion, sNet, sAddrType, sAddr;
        if(!(line >> sUser >> sSessId >> sVersion >> sNet >> sAddrType >> sAddr)) { return false; }
        sKey = sUser + " " + sSessId + " " + sAddr;
        nVersion = strtoull(sVersion.c_str(), nullptr, 10);
        return true;
    }

    std::string SdpField(const std::string& sSdp, const char* sPrefix)
    {
        auto nStart = sSdp.find(sPrefix);
        if(nStart == std::string::npos) { return ""; }
        nStart += strlen(sPrefix);
        auto nEnd = sSdp.find_first_of("\r\n", nStart);
        return sSdp.substr(nStart, nEnd == std::string::npos ? std::string::npos : nEnd-nStart);
    }
}

SapListener::~SapListener()
{
    Stop();
}

bool SapListener::Start(const std::string& sInterface, int nTimeoutS)
{
    Stop();
    m_nTimeoutS = std::max(10, nTimeoutS);
    m_nSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(m_nSocket < 0) { return false; }
    int nOn = 1;
    setsockopt(m_nSocket, SOL_SOCKET, SO_REUSEADDR, &nOn, sizeof(nOn));
    setsockopt(m_nSocket, SOL_SOCKET, SO_REUSEPORT, &nOn, sizeof(nOn));   //dante controller & co listen too
    timeval tv{1, 0};
    setsockopt(m_nSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if(!compat::BindUdp(m_nSocket, kSapPort, kSapGroup) || !compat::JoinMulticast(m_nSocket, kSapGroup, sInterface))
    {
        LOG_WARN("sap") << "listener on " << sInterface << " failed";
        close(m_nSocket);
        m_nSocket = -1;
        return false;
    }
    m_bRun = true;
    m_thread = std::thread([this]() { Loop(); });
    LOG_INFO("sap") << "listening on " << sInterface;
    return true;
}

void SapListener::Stop()
{
    if(!m_bRun.exchange(false)) { return; }
    if(m_thread.joinable()) { m_thread.join(); }
    if(m_nSocket >= 0) { close(m_nSocket); m_nSocket = -1; }
}

void SapListener::Loop()
{
    std::vector<uint8_t> vBuffer(65536);
    while(m_bRun)
    {
        auto nSize = recv(m_nSocket, vBuffer.data(), vBuffer.size(), 0);
        if(nSize > 0) { HandlePacket(vBuffer.data(), size_t(nSize)); }

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lg(m_mutex);
        for(auto it = m_mSessions.begin(); it != m_mSessions.end();)
        {
            if(now - it->second.lastSeen > std::chrono::seconds(m_nTimeoutS))
            {
                LOG_INFO("sap") << "timed out: " << it->second.sName;
                it = m_mSessions.erase(it);
            }
            else { ++it; }
        }
    }
}

bool SapListener::HandlePacket(const uint8_t* pData, size_t nSize)
{
    m_nPackets++;
    if(nSize < 8 || (pData[0] >> 5) != 1 || (pData[0] & 0x03) != 0)   //version 1, not encrypted/compressed
    {
        m_nIgnored++;
        return false;
    }
    bool bIpv6 = pData[0] & 0x10;
    bool bDelete = pData[0] & 0x04;
    size_t nOffset = 4 + (bIpv6 ? 16 : 4) + size_t(pData[1]) * 4;  //origin + authentication
    if(nOffset >= nSize) { m_nIgnored++; return false; }

    char sOrigin[INET6_ADDRSTRLEN] = {0};
    inet_ntop(bIpv6 ? AF_INET6 : AF_INET, pData+4, sOrigin, sizeof(sOrigin));

    std::string sPayload(reinterpret_cast<const char*>(pData+nOffset), nSize-nOffset);
    //optional payload type: a 0-terminated mime type before the sdp
    if(sPayload.rfind("v=0", 0) != 0 && sPayload.rfind("o=", 0) != 0)
    {
        auto nZero = sPayload.find('\0');
        if(nZero == std::string::npos) { m_nIgnored++; return false; }
        if(sPayload.substr(0, nZero) != "application/sdp") { m_nIgnored++; return false; }
        sPayload = sPayload.substr(nZero+1);
    }

    std::string sKey;
    uint64_t nVersion = 0;
    if(!ParseOrigin(sPayload, sKey, nVersion)) { m_nIgnored++; return false; }

    std::lock_guard<std::mutex> lg(m_mutex);
    if(bDelete)
    {
        if(auto it = m_mSessions.find(sKey); it != m_mSessions.end())
        {
            LOG_INFO("sap") << "deleted: " << it->second.sName;
            m_mSessions.erase(it);
        }
        return true;
    }

    auto& entry = m_mSessions[sKey];
    bool bNew = entry.sKey.empty();
    if(!bNew && nVersion < entry.nVersion) { return true; }     //stale re-send of an older version
    if(bNew || nVersion != entry.nVersion)
    {
        LOG_INFO("sap") << (bNew ? "discovered: " : "updated: ") << SdpField(sPayload, "s=") << " from " << sOrigin;
    }
    entry.sKey = sKey;
    entry.sName = SdpField(sPayload, "s=");
    entry.sOrigin = sOrigin;
    entry.sSdp = sPayload;
    entry.nVersion = nVersion;
    entry.lastSeen = std::chrono::steady_clock::now();
    return true;
}

std::vector<SapListener::Discovered> SapListener::List() const
{
    std::lock_guard<std::mutex> lg(m_mutex);
    std::vector<Discovered> v;
    for(const auto& [sKey, entry] : m_mSessions) { v.push_back(entry); }
    return v;
}

std::optional<std::string> SapListener::FindSdp(const std::string& sKey) const
{
    std::lock_guard<std::mutex> lg(m_mutex);
    if(auto it = m_mSessions.find(sKey); it != m_mSessions.end()) { return it->second.sSdp; }
    return std::nullopt;
}

json SapListener::GetStatusJson() const
{
    json js = json::array();
    auto now = std::chrono::steady_clock::now();
    for(const auto& entry : List())
    {
        js.push_back({{"key", entry.sKey}, {"name", entry.sName}, {"origin", entry.sOrigin},
                      {"version", entry.nVersion},
                      {"age_s", std::chrono::duration_cast<std::chrono::seconds>(now - entry.lastSeen).count()},
                      {"sdp", entry.sSdp}});
    }
    return js;
}
