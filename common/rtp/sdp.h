#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pan::rtp
{
    struct SdpLeg
    {
        std::string sMid;           //a=mid (leg name, e.g. PRI/SEC)
        std::string sMulticast;
        std::string sSourceFilter;  //optional source specific address
        uint16_t nPort = 5004;
    };

    struct SdpSession
    {
        std::string sName;
        std::vector<SdpLeg> vLegs;  //1 = single, 2 = ST 2022-7 DUP
        int nPayloadType = 96;
        int nSampleRate = 48000;
        int nChannels = 2;
        int nBitsPerSample = 24;    //L24 or L16
        double dPacketTimeMs = 1.0;
        std::string sPtpGrandmaster;
        int nPtpDomain = 0;
        bool bClockDomain = false;  //legacy a=clock-domain line (Dante/RAVENNA style SAP announcements)
    };

    std::optional<SdpSession> ParseSdp(const std::string& sSdp);
    /** nSessId/nSessVersion 0 = derive both from the current time (one-off sdp);
    *   periodic announcements (SAP) pass stable values so receivers see one session **/
    std::string GenerateSdp(const SdpSession& session, const std::string& sOriginIp, const std::vector<std::string>& vSourceIps,
                            uint64_t nSessId = 0, uint64_t nSessVersion = 0);
}
