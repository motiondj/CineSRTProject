#pragma once

namespace SRTNetwork
{
    // 기존 상수들...
    constexpr int OPT_TRANSTYPE = 50;
    constexpr int OPT_SENDER = 21;
    constexpr int OPT_STREAMID = 47;
    constexpr int OPT_PBKEYLEN = 27;
    constexpr int OPT_PASSPHRASE = 26;
    constexpr int OPT_LATENCY = 23;
    constexpr int OPT_MSS = 0;
    constexpr int OPT_FC = 4;
    constexpr int OPT_SNDBUF = 5;
    constexpr int OPT_SNDDROPDELAY = 30;
    constexpr int OPT_SNDTIMEO = 34;
    constexpr int OPT_PEERLATENCY = 18;
    constexpr int OPT_PEERIDLETIMEO = 19;
    constexpr int TRANSTYPE_LIVE = 0;
    constexpr int OPT_VERSION = 31;         // SRTO_VERSION (0x1f)
    constexpr int OPT_MINVERSION = 32;      // SRTO_MINVERSION (0x20)
    constexpr int OPT_ENFORCEDENCRYPTION = 37; // SRTO_ENFORCEDENCRYPTION (0x25)

    // 버전 정보 구조체
    struct VersionInfo
    {
        int Major;
        int Minor;
        int Patch;
        FString FullVersion;
        FString BuildInfo;
        bool bIsCompatible;
    };
    // 시스템 정보 구조체
    struct SystemInfo
    {
        VersionInfo SRTVersion;
        VersionInfo OpenSSLVersion;
        FString Platform;
        FString BuildDate;
        bool bEncryptionSupported;
    };
    // 버전 및 시스템 정보 함수
    bool GetVersionInfo(VersionInfo& OutInfo);
    bool GetSystemInfo(SystemInfo& OutInfo);
    bool CheckCompatibility();
    const char* GetVersionString();

    // 기존 함수들...
    bool Initialize();
    void Shutdown();
    void* CreateSocket();
    void CloseSocket(void* socket);
    bool SetSocketOption(void* socket, int opt, const void* value, int len);
    bool Connect(void* socket, const char* ip, int port);
    bool Bind(void* socket, int port);
    bool Listen(void* socket, int backlog);
    void* Accept(void* socket);
    int Send(void* socket, const char* data, int len);
    const char* GetLastError();
    struct Stats
    {
        double mbpsSendRate;
        double msRTT;
        int pktSndLossTotal;
    };
    bool GetStats(void* socket, Stats& stats);
    bool SetNonBlocking(void* socket, bool nonblocking);
    void* AcceptWithTimeout(void* socket, int timeout_ms);
} 