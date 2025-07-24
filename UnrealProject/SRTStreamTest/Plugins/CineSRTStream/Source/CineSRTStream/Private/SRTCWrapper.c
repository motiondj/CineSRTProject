// SRTCWrapper.c - 순수 C 래퍼 (C++ 템플릿 충돌 방지)
// 이 파일에서만 SRT 헤더를 직접 포함합니다

#include "SRTCWrapper.h"
#include <string.h>
#include <stdlib.h>

// Windows 헤더
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    #pragma warning(push)
    #pragma warning(disable: 4005) // macro redefinition
    #pragma warning(disable: 4996) // deprecated functions
    #pragma warning(disable: 4244) // conversion warnings
    #pragma warning(disable: 4267) // size_t to int conversion
#endif

// SRT 헤더 (이 파일에서만 포함)
#include "srt.h"

#ifdef _WIN32
    #pragma warning(pop)
#endif

// ================================================================================
// C 래퍼 함수들
// ================================================================================

// SRT 라이브러리 초기화
int SRT_C_Initialize(void)
{
    // Windows 소켓 초기화 (SRT 사용 전 필수)
#ifdef _WIN32
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        return -1; // WSA 초기화 실패
    }
#endif

    // SRT 초기화
    int result = srt_startup();
    if (result != 0) {
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }
    
    return 0; // 성공
}

// SRT 라이브러리 정리
void SRT_C_Cleanup(void)
{
    srt_cleanup();
    
    // Windows 소켓 정리
#ifdef _WIN32
    WSACleanup();
#endif
}

// SRT 버전 정보
const char* SRT_C_GetVersion(void)
{
    const char* version = srt_getversion();
    if (version != NULL && strlen(version) > 0) {
        return version;
    }
    return "SRT Unknown Version";
}

// 소켓 생성
SRTSOCKET_HANDLE SRT_C_CreateSocket(void)
{
    return srt_create_socket();
}

// 소켓 닫기
int SRT_C_CloseSocket(SRTSOCKET_HANDLE sock)
{
    return srt_close(sock);
}

// 소켓 바인딩
int SRT_C_Bind(SRTSOCKET_HANDLE sock, const char* ip, int port)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    
    if (ip == NULL || strcmp(ip, "0.0.0.0") == 0 || strlen(ip) == 0)
    {
        sa.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        inet_pton(AF_INET, ip, &sa.sin_addr);
    }
    
    return srt_bind(sock, (struct sockaddr*)&sa, sizeof(sa));
}

// 소켓 연결
int SRT_C_Connect(SRTSOCKET_HANDLE sock, const char* ip, int port)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, ip, &sa.sin_addr);
    
    return srt_connect(sock, (struct sockaddr*)&sa, sizeof(sa));
}

// 소켓 리스닝
int SRT_C_Listen(SRTSOCKET_HANDLE sock, int backlog)
{
    return srt_listen(sock, backlog);
}

// 소켓 수락
SRTSOCKET_HANDLE SRT_C_Accept(SRTSOCKET_HANDLE sock)
{
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    return srt_accept(sock, (struct sockaddr*)&client_addr, &addr_len);
}

// 데이터 전송
int SRT_C_Send(SRTSOCKET_HANDLE sock, const char* data, int len)
{
    return srt_send(sock, data, len);
}

// 데이터 수신
int SRT_C_Recv(SRTSOCKET_HANDLE sock, char* data, int len)
{
    return srt_recv(sock, data, len);
}

// 소켓 옵션 설정
int SRT_C_SetIntOption(SRTSOCKET_HANDLE sock, int opt, int value)
{
    return srt_setsockopt(sock, 0, (SRT_SOCKOPT)opt, &value, sizeof(int));
}

int SRT_C_SetStrOption(SRTSOCKET_HANDLE sock, int opt, const char* value)
{
    return srt_setsockopt(sock, 0, (SRT_SOCKOPT)opt, value, (int)strlen(value));
}

// 소켓 옵션 가져오기
int SRT_C_GetIntOption(SRTSOCKET_HANDLE sock, int opt, int* value)
{
    int len = sizeof(int);
    return srt_getsockopt(sock, 0, (SRT_SOCKOPT)opt, value, &len);
}

// 마지막 에러 코드
int SRT_C_GetLastError(void)
{
    return srt_getlasterror(NULL);
}

// 마지막 에러 문자열
const char* SRT_C_GetLastErrorString(void)
{
    return srt_getlasterror_str();
}

// 통계 정보 가져오기
int SRT_C_GetStats(SRTSOCKET_HANDLE sock, SRT_C_Stats* stats)
{
    SRT_TRACEBSTATS s;
    if (srt_bstats(sock, &s, 1) == 0) {
        stats->mbpsSendRate = s.mbpsSendRate;
        stats->msRTT = s.msRTT;
        stats->pktSndLossTotal = s.pktSndLossTotal;
        return 0;
    }
    return -1;
}

// 유효한 소켓인지 확인
int SRT_C_IsValidSocket(SRTSOCKET_HANDLE sock)
{
    return (sock != SRT_INVALID_SOCK) ? 1 : 0;
} 

// 소켓 옵션 설정 (일반)
int SRT_C_SetOption(int sock, int opt, const void* value, int len)
{
    return srt_setsockopt(sock, 0, opt, value, len);
}

// 소켓 옵션 가져오기 (일반)
int SRT_C_GetOption(int sock, int opt, void* value, int* len)
{
    return srt_getsockopt(sock, 0, opt, value, len);
} 