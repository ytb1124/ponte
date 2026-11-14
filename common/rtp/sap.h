#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "sdp.h"

namespace pan::rtp
{
    /** one session to announce: a single-leg sdp leaving on one interface **/
    struct SapSession
    {
        std::string sInterface;     //egress interface (en0 ...)
        std::string sOriginIp;      //sender ip on that interface (o= / source-filter / SAP origin)
        SdpSession session;         //exactly one leg
    };

    /** RFC 2974 SAP announcer (239.255.255.255:9875) - makes the senders visible
    *   to SAP based discovery, e.g. Dante Controller's AES67 flows.
    *   The provider is polled once per second; new or changed sessions are
    *   announced at once, unchanged ones every nIntervalS, vanished ones get a
    *   SAP deletion. Each session keeps a stable o= id; its version moves on
    *   every content change. **/
    class SapAnnouncer
    {
        public:
            using Provider = std::function<std::vector<SapSession>()>;

            ~SapAnnouncer();

            void Start(Provider provider, int nIntervalS, int nTtl);
            /** sends deletions for everything still announced **/
            void Stop();

            nlohmann::json GetStatusJson() const;

        private:
            struct Entry
            {
                std::string sInterface;
                std::string sOriginIp;
                std::string sSdp;
                uint16_t nMsgHash = 0;
                uint64_t nVersion = 0;
                uint64_t nBodyHash = 0;
                std::chrono::steady_clock::time_point lastSent;
                bool bSeen = false;
            };

            void Loop();
            bool Send(const Entry& entry, bool bDelete);
            int SocketFor(const std::string& sInterface);
            void CloseSockets();

            Provider m_provider;
            int m_nIntervalS = 30;
            int m_nTtl = 64;

            std::thread m_thread;
            std::atomic<bool> m_bRun{false};
            std::mutex m_wakeMutex;
            std::condition_variable m_wake;

            mutable std::mutex m_mutex;     //guards m_mEntries for status reads
            std::map<std::string, Entry> m_mEntries;   //key: interface|group|port
            std::map<std::string, int> m_mSockets;
            std::atomic<uint64_t> m_nAnnouncements{0};
            std::atomic<uint64_t> m_nDeletions{0};
            std::atomic<uint64_t> m_nSendErrors{0};
            bool m_bLastSendOk = true;      //announcer thread only (and Stop after join)
    };

    /** RFC 2974 SAP listener: collects the sdp announced by other devices
    *   (Dante AES67 flows, RAVENNA, other aes67eleven nodes, our own sender).
    *   Sessions are keyed by their o= identity (user, session id, address), so a
    *   new version replaces the old entry; deletions and silence (nTimeoutS)
    *   remove it. **/
    class SapListener
    {
        public:
            struct Discovered
            {
                std::string sKey;       //o= identity "user sessid addr"
                std::string sName;      //s=
                std::string sOrigin;    //SAP origin ip
                std::string sSdp;
                uint64_t nVersion = 0;
                std::chrono::steady_clock::time_point lastSeen;
            };

            ~SapListener();
            bool Start(const std::string& sInterface, int nTimeoutS);
            void Stop();

            std::vector<Discovered> List() const;
            std::optional<std::string> FindSdp(const std::string& sKey) const;
            nlohmann::json GetStatusJson() const;

            /** parse one datagram (exposed for tests); false = not a usable sap packet **/
            bool HandlePacket(const uint8_t* pData, size_t nSize);

        private:
            void Loop();

            int m_nSocket = -1;
            int m_nTimeoutS = 300;
            std::thread m_thread;
            std::atomic<bool> m_bRun{false};
            mutable std::mutex m_mutex;
            std::map<std::string, Discovered> m_mSessions;
            std::atomic<uint64_t> m_nPackets{0};
            std::atomic<uint64_t> m_nIgnored{0};
    };

    /** wire format, exposed for tests: header + "application/sdp\0" + sdp **/
    std::vector<uint8_t> BuildSapPacket(bool bDelete, uint16_t nMsgHash, const std::string& sOriginIp, const std::string& sSdp);
}
