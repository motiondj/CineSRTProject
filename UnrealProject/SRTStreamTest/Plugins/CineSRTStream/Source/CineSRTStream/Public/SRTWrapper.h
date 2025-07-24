#pragma once

// SRT C++ 호환 래퍼 헤더 (순수 C 래퍼 사용)
// 이 파일은 C++ 링키지 충돌을 완전히 방지합니다

// SRT 상수들 (실제 srt.h 값과 일치)
#define SRTT_LIVE 0
#define SRTO_TRANSTYPE 50
#define SRTO_SENDER 21
#define SRTO_STREAMID 47
#define SRTO_PBKEYLEN 27
#define SRTO_PASSPHRASE 26
#define SRTO_LATENCY 23
#define SRTO_MSS 0
#define SRTO_FC 4
#define SRTO_SNDBUF 5

// SRT 통계 구조체 (C 호환)
struct SRTStats
{
    int64_t msTimeStamp;
    int64_t pktSentTotal;
    int64_t pktRecvTotal;
    int pktSndLossTotal;
    int pktRcvLossTotal;
    int pktRetransTotal;
    double mbpsSendRate;
    double mbpsRecvRate;
    double msRTT;
    double mbpsBandwidth;
    int byteAvailSndBuf;
    int byteAvailRcvBuf;
};

// C 래퍼 함수 선언 (extern "C"로 C++에서 호출 가능)
extern "C" {
    int SRT_C_Initialize(void);
    void SRT_C_Cleanup(void);
    const char* SRT_C_GetVersion(void);
    int SRT_C_CreateSocket(void);
    int SRT_C_CloseSocket(int sock);
    int SRT_C_Bind(int sock, const char* ip, int port);
    int SRT_C_Connect(int sock, const char* ip, int port);
    int SRT_C_Listen(int sock, int backlog);
    int SRT_C_Accept(int sock);
    int SRT_C_Send(int sock, const char* data, int len);
    int SRT_C_Recv(int sock, char* data, int len);
    int SRT_C_SetOption(int sock, int opt, const void* value, int len);
    int SRT_C_GetOption(int sock, int opt, void* value, int* len);
    int SRT_C_GetLastError(void);
    const char* SRT_C_GetLastErrorString(void);
    int SRT_C_GetStats(int sock, void* stats, int clear);
    int SRT_C_IsValidSocket(int sock);
}

// C++ 래퍼 클래스들
namespace SRTWrapper {
    class SRTSocket {
    public:
        SRTSocket();
        ~SRTSocket();
        
        bool Create();
        bool Close();
        bool Bind(const char* ip, int port);
        bool Connect(const char* ip, int port);
        bool Listen(int backlog = 10);
        SRTSocket* Accept();
        
        int Send(const char* data, int len);
        int Recv(char* data, int len);
        
        bool SetOption(int opt, const void* value, int len);
        bool GetOption(int opt, void* value, int* len);
        
        int GetLastError() const;
        const char* GetLastErrorString() const;
        
        bool IsValid() const { return m_socket >= 0; }
        
        // 소켓 핸들 접근 (통계용)
        int GetSocketHandle() const { return m_socket; }
        
    private:
        int m_socket;
    };
    
    // SRT 라이브러리 초기화/정리
    bool Initialize();
    void Cleanup();
    
    // SRT 버전 정보
    const char* GetVersion();
} 