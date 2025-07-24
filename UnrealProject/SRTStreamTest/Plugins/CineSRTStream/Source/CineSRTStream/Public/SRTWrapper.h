#pragma once
// SRT C++ 호환 래퍼 헤더
// 이 파일은 C++ 링키지 충돌을 완전히 방지합니다

// SRT 상수들
#define SRTT_LIVE 0
#define SRTO_TRANSTYPE 1
#define SRTO_SENDER 2
#define SRTO_STREAMID 3
#define SRTO_PBKEYLEN 4
#define SRTO_PASSPHRASE 5
#define SRTO_LATENCY 6
#define SRTO_MSS 7
#define SRTO_FC 8
#define SRTO_SNDBUF 9

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
        
    private:
        int m_socket;
    };
    
    // SRT 라이브러리 초기화/정리
    bool Initialize();
    void Cleanup();
    
    // SRT 버전 정보
    const char* GetVersion();
} 