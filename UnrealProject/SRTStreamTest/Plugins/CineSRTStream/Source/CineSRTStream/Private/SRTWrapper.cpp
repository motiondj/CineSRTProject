// SRTWrapper.cpp - C++ 래퍼 클래스 구현 (순수 C 래퍼 사용)
#include "SRTWrapper.h"

// C++ 래퍼 클래스들 구현
namespace SRTWrapper {
    SRTSocket::SRTSocket() : m_socket(-1) {
    }
    
    SRTSocket::~SRTSocket() {
        Close();
    }
    
    bool SRTSocket::Create() {
        m_socket = SRT_C_CreateSocket();
        return SRT_C_IsValidSocket(m_socket) != 0;  // 명시적 비교
    }
    
    bool SRTSocket::Close() {
        if (SRT_C_IsValidSocket(m_socket) != 0) {  // 명시적 비교
            SRT_C_CloseSocket(m_socket);
            m_socket = -1;
        }
        return true;
    }
    
    bool SRTSocket::Bind(const char* ip, int port) {
        if (SRT_C_IsValidSocket(m_socket) == 0) return false;  // 명시적 비교
        return SRT_C_Bind(m_socket, ip, port) != -1;
    }
    
    bool SRTSocket::Connect(const char* ip, int port) {
        if (SRT_C_IsValidSocket(m_socket) == 0) return false;  // 명시적 비교
        return SRT_C_Connect(m_socket, ip, port) != -1;
    }
    
    bool SRTSocket::Listen(int backlog) {
        if (SRT_C_IsValidSocket(m_socket) == 0) return false;  // 명시적 비교
        return SRT_C_Listen(m_socket, backlog) != -1;
    }
    
    SRTSocket* SRTSocket::Accept() {
        if (SRT_C_IsValidSocket(m_socket) == 0) return nullptr;  // 명시적 비교
        
        int client_sock = SRT_C_Accept(m_socket);
        if (SRT_C_IsValidSocket(client_sock) == 0) return nullptr;  // 명시적 비교
        
        SRTSocket* new_socket = new SRTSocket();
        new_socket->m_socket = client_sock;
        return new_socket;
    }
    
    int SRTSocket::Send(const char* data, int len) {
        if (SRT_C_IsValidSocket(m_socket) == 0) return -1;  // 명시적 비교
        return SRT_C_Send(m_socket, data, len);
    }
    
    int SRTSocket::Recv(char* data, int len) {
        if (SRT_C_IsValidSocket(m_socket) == 0) return -1;  // 명시적 비교
        return SRT_C_Recv(m_socket, data, len);
    }
    
    bool SRTSocket::SetOption(int opt, const void* value, int len) {
        if (SRT_C_IsValidSocket(m_socket) == 0) return false;  // 명시적 비교
        return SRT_C_SetOption(m_socket, opt, value, len) != -1;
    }
    
    bool SRTSocket::GetOption(int opt, void* value, int* len) {
        if (SRT_C_IsValidSocket(m_socket) == 0) return false;  // 명시적 비교
        return SRT_C_GetOption(m_socket, opt, value, len) != -1;
    }
    
    int SRTSocket::GetLastError() const {
        return SRT_C_GetLastError();
    }
    
    const char* SRTSocket::GetLastErrorString() const {
        return SRT_C_GetLastErrorString();
    }
    
    // SRT 라이브러리 초기화/정리
    bool Initialize() {
        try {
            int result = SRT_C_Initialize();
            return result >= 0;
        } catch (...) {
            return false;
        }
    }
    
    void Cleanup() {
        try {
            SRT_C_Cleanup();
        } catch (...) {
            // 정리 중 오류는 무시
        }
    }
    
    const char* GetVersion() {
        try {
            const char* version = SRT_C_GetVersion();
            // null 체크 및 빈 문자열 체크
            if (version != nullptr && strlen(version) > 0) {
                return version;
            }
            return "Unknown";
        } catch (...) {
            return "Unknown";
        }
    }
} 