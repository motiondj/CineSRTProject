#include "SRTWrapper.h"

// Windows 헤더를 포함
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    // 경고 비활성화
    #pragma warning(push)
    #pragma warning(disable: 4005) // macro redefinition
    #pragma warning(disable: 4996) // deprecated functions
    #pragma warning(disable: 4244) // conversion warnings
    #pragma warning(disable: 4267) // size_t to int conversion
#endif

// SRT 헤더를 C 링키지로 포함
extern "C" {
    #include "srt.h"
}

#ifdef _WIN32
    #pragma warning(pop)
#endif

// C++ 래퍼 클래스들 구현
namespace SRTWrapper {
    
    SRTSocket::SRTSocket() : m_socket(-1) {
    }
    
    SRTSocket::~SRTSocket() {
        Close();
    }
    
    bool SRTSocket::Create() {
        m_socket = srt_create_socket();
        return m_socket != SRT_INVALID_SOCK;
    }
    
    bool SRTSocket::Close() {
        if (m_socket != SRT_INVALID_SOCK) {
            srt_close(m_socket);
            m_socket = SRT_INVALID_SOCK;
        }
        return true;
    }
    
    bool SRTSocket::Bind(const char* ip, int port) {
        if (m_socket == SRT_INVALID_SOCK) return false;
        
        sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        
        if (strcmp(ip, "0.0.0.0") == 0 || ip == nullptr || strlen(ip) == 0) {
            sa.sin_addr.s_addr = INADDR_ANY;
        } else {
            inet_pton(AF_INET, ip, &sa.sin_addr);
        }
        
        return srt_bind(m_socket, (sockaddr*)&sa, sizeof(sa)) != SRT_ERROR;
    }
    
    bool SRTSocket::Connect(const char* ip, int port) {
        if (m_socket == SRT_INVALID_SOCK) return false;
        
        sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        inet_pton(AF_INET, ip, &sa.sin_addr);
        
        return srt_connect(m_socket, (sockaddr*)&sa, sizeof(sa)) != SRT_ERROR;
    }
    
    bool SRTSocket::Listen(int backlog) {
        if (m_socket == SRT_INVALID_SOCK) return false;
        return srt_listen(m_socket, backlog) != SRT_ERROR;
    }
    
    SRTSocket* SRTSocket::Accept() {
        if (m_socket == SRT_INVALID_SOCK) return nullptr;
        
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SRTSOCKET client_sock = srt_accept(m_socket, (sockaddr*)&client_addr, &addr_len);
        
        if (client_sock == SRT_INVALID_SOCK) return nullptr;
        
        SRTSocket* new_socket = new SRTSocket();
        new_socket->m_socket = client_sock;
        return new_socket;
    }
    
    int SRTSocket::Send(const char* data, int len) {
        if (m_socket == SRT_INVALID_SOCK) return SRT_ERROR;
        return srt_send(m_socket, data, len);
    }
    
    int SRTSocket::Recv(char* data, int len) {
        if (m_socket == SRT_INVALID_SOCK) return SRT_ERROR;
        return srt_recv(m_socket, data, len);
    }
    
    bool SRTSocket::SetOption(int opt, const void* value, int len) {
        if (m_socket == SRT_INVALID_SOCK) return false;
        return srt_setsockopt(m_socket, 0, opt, value, len) != SRT_ERROR;
    }
    
    bool SRTSocket::GetOption(int opt, void* value, int* len) {
        if (m_socket == SRT_INVALID_SOCK) return false;
        return srt_getsockopt(m_socket, 0, opt, value, len) != SRT_ERROR;
    }
    
    int SRTSocket::GetLastError() const {
        return srt_getlasterror(nullptr);
    }
    
    const char* SRTSocket::GetLastErrorString() const {
        return srt_getlasterror_str();
    }
    
    // SRT 라이브러리 초기화/정리
    bool Initialize() {
        return srt_startup() >= 0;
    }
    
    void Cleanup() {
        srt_cleanup();
    }
    
    const char* GetVersion() {
        return srt_getversion();
    }
} 