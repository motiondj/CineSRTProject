#include "CoreMinimal.h"
#include <cstdint>  // intptr_t 사용

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#include "SRTNetworkWorker.h"
#include "srt.h"
#include <string>

namespace SRTNetwork
{
    static bool bInitialized = false;
    
    bool Initialize()
    {
        if (bInitialized) return true;
        #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }
        #endif
        if (srt_startup() == 0) {
            bInitialized = true;
            return true;
        }
        return false;
    }
    void Shutdown()
    {
        if (bInitialized) {
            srt_cleanup();
            #ifdef _WIN32
            WSACleanup();
            #endif
            bInitialized = false;
        }
    }
    void* CreateSocket()
    {
        SRTSOCKET sock = srt_create_socket();
        if (sock == SRT_INVALID_SOCK) return nullptr;
        // intptr_t를 통한 안전한 변환
        return reinterpret_cast<void*>(static_cast<intptr_t>(sock));
    }
    void CloseSocket(void* socket)
    {
        if (socket) {
            SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
            srt_close(sock);
        }
    }
    bool SetSocketOption(void* socket, int opt, const void* value, int len)
    {
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        return srt_setsockopt(sock, 0, static_cast<SRT_SOCKOPT>(opt), value, len) == 0;
    }
    bool Connect(void* socket, const char* ip, int port)
    {
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        inet_pton(AF_INET, ip, &sa.sin_addr);
        return srt_connect(sock, (sockaddr*)&sa, sizeof(sa)) != SRT_ERROR;
    }
    bool Bind(void* socket, int port)
    {
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = INADDR_ANY;
        return srt_bind(sock, (sockaddr*)&sa, sizeof(sa)) == 0;
    }
    bool Listen(void* socket, int backlog)
    {
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        return srt_listen(sock, backlog) == 0;
    }
    void* Accept(void* socket)
    {
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SRTSOCKET client = srt_accept(sock, (sockaddr*)&client_addr, &addr_len);
        if (client == SRT_INVALID_SOCK) return nullptr;
        return reinterpret_cast<void*>(static_cast<intptr_t>(client));
    }
    int Send(void* socket, const char* data, int len)
    {
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        return srt_send(sock, data, len);
    }
    const char* GetLastError()
    {
        return srt_getlasterror_str();
    }
    bool GetStats(void* socket, Stats& stats)
    {
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        SRT_TRACEBSTATS srtStats;
        if (srt_bstats(sock, &srtStats, 1) == 0) {
            stats.mbpsSendRate = srtStats.mbpsSendRate;
            stats.msRTT = srtStats.msRTT;
            stats.pktSndLossTotal = srtStats.pktSndLossTotal;
            return true;
        }
        return false;
    }

    bool SetNonBlocking(void* socket, bool nonblocking)
    {
        if (!socket) return false;
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        int sync_mode = nonblocking ? 0 : 1;
        bool result = true;
        result &= (srt_setsockopt(sock, 0, SRTO_SNDSYN, &sync_mode, sizeof(sync_mode)) == 0);
        result &= (srt_setsockopt(sock, 0, SRTO_RCVSYN, &sync_mode, sizeof(sync_mode)) == 0);
        if (nonblocking)
        {
            int timeout_ms = 100;
            srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
        }
        return result;
    }

    void* AcceptWithTimeout(void* socket, int timeout_ms)
    {
        if (!socket) return nullptr;
        
        SRTSOCKET sock = static_cast<SRTSOCKET>(reinterpret_cast<intptr_t>(socket));
        
        // Accept 타임아웃 설정
        int rcvtimeo = timeout_ms;
        srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
        
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SRTSOCKET client = srt_accept(sock, (sockaddr*)&client_addr, &addr_len);
        
        if (client != SRT_INVALID_SOCK)
        {
            return reinterpret_cast<void*>(static_cast<intptr_t>(client));
        }
        
        return nullptr;
    }

    static VersionInfo CachedVersionInfo = {0, 0, 0, "", "", false};
    static bool bVersionCached = false;

    bool GetVersionInfo(VersionInfo& OutInfo)
    {
        if (!bVersionCached)
        {
            uint32_t version = srt_getversion();
            CachedVersionInfo.Major = (version >> 16) & 0xFF;
            CachedVersionInfo.Minor = (version >> 8) & 0xFF;
            CachedVersionInfo.Patch = version & 0xFF;
            CachedVersionInfo.FullVersion = FString::Printf(TEXT("%d.%d.%d"),
                CachedVersionInfo.Major,
                CachedVersionInfo.Minor,
                CachedVersionInfo.Patch);
#ifdef SRT_VERSION_BUILD
            CachedVersionInfo.BuildInfo = UTF8_TO_TCHAR(SRT_VERSION_BUILD);
#else
            CachedVersionInfo.BuildInfo = TEXT("Release");
#endif
            CachedVersionInfo.bIsCompatible =
                (CachedVersionInfo.Major > 1) ||
                (CachedVersionInfo.Major == 1 && CachedVersionInfo.Minor >= 4);
            bVersionCached = true;
        }
        OutInfo = CachedVersionInfo;
        return CachedVersionInfo.bIsCompatible;
    }
    bool GetSystemInfo(SystemInfo& OutInfo)
    {
        GetVersionInfo(OutInfo.SRTVersion);
#ifdef OPENSSL_VERSION_TEXT
        OutInfo.OpenSSLVersion.FullVersion = UTF8_TO_TCHAR(OPENSSL_VERSION_TEXT);
#else
        OutInfo.OpenSSLVersion.FullVersion = TEXT("Unknown");
#endif
#ifdef _WIN64
        OutInfo.Platform = TEXT("Windows 64-bit");
#else
        OutInfo.Platform = TEXT("Unknown Platform");
#endif
        OutInfo.BuildDate = UTF8_TO_TCHAR(__DATE__ " " __TIME__);
        OutInfo.bEncryptionSupported = true;
        return true;
    }
    bool CheckCompatibility()
    {
        VersionInfo info;
        return GetVersionInfo(info);
    }
    const char* GetVersionString()
    {
        static std::string versionStr;
        if (versionStr.empty())
        {
            VersionInfo info;
            GetVersionInfo(info);
            versionStr = TCHAR_TO_UTF8(*info.FullVersion);
        }
        return versionStr.c_str();
    }
} 