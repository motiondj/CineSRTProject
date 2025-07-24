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

#include "srt.h"

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
    struct Stats
    {
        double mbpsSendRate;
        double msRTT;
        int pktSndLossTotal;
    };
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
} 