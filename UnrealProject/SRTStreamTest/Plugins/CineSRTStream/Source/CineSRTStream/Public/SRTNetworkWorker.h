#pragma once

namespace SRTNetwork
{
    // SRT 옵션 상수 (srt.h 값과 동일)
    constexpr int OPT_TRANSTYPE = 50;
    constexpr int OPT_SENDER = 21;
    constexpr int OPT_STREAMID = 47;
    constexpr int OPT_PBKEYLEN = 27;
    constexpr int OPT_PASSPHRASE = 26;
    constexpr int OPT_LATENCY = 23;
    constexpr int OPT_MSS = 0;
    constexpr int OPT_FC = 4;
    constexpr int OPT_SNDBUF = 5;
    
    constexpr int TRANSTYPE_LIVE = 0;
    
    // 초기화
    bool Initialize();
    void Shutdown();
    
    // 소켓 관리
    void* CreateSocket();
    void CloseSocket(void* socket);
    bool SetSocketOption(void* socket, int opt, const void* value, int len);
    
    // 연결
    bool Connect(void* socket, const char* ip, int port);
    bool Bind(void* socket, int port);
    bool Listen(void* socket, int backlog);
    void* Accept(void* socket);
    
    // 데이터 전송
    int Send(void* socket, const char* data, int len);
    
    // 에러
    const char* GetLastError();
    
    // 통계
    struct Stats
    {
        double mbpsSendRate;
        double msRTT;
        int pktSndLossTotal;
    };
    bool GetStats(void* socket, Stats& stats);
} 