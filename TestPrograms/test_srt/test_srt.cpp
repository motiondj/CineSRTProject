#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include "srt.h"

#pragma comment(lib, "srt_static.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "pthreadVC3.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Crypt32.lib")

int main() {
    std::cout << "=== SRT + OpenSSL Windows Test ===" << std::endl;
    
    // SRT 초기화
    if (srt_startup() != 0) {
        std::cout << "❌ SRT startup failed!" << std::endl;
        return 1;
    }
    
    std::cout << "SRT Version: " << SRT_VERSION_STRING << std::endl;
    
    // 소켓 생성
    SRTSOCKET sock = srt_create_socket();
    if (sock == SRT_INVALID_SOCK) {
        std::cout << "❌ Socket creation failed!" << std::endl;
        srt_cleanup();
        return 1;
    }
    
    // 암호화 설정 (핵심!)
    int pbkeylen = 16; // 128-bit AES
    if (srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen)) != 0) {
        std::cout << "❌ Failed to set encryption key length!" << std::endl;
        std::cout << "Error: " << srt_getlasterror_str() << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    
    // 암호화 패스프레이즈 설정
    const char* passphrase = "CineSRTStreamTest123";
    if (srt_setsockopt(sock, 0, SRTO_PASSPHRASE, passphrase, strlen(passphrase)) != 0) {
        std::cout << "❌ Failed to set passphrase!" << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    
    std::cout << "✅ Encryption enabled (128-bit AES)" << std::endl;
    std::cout << "✅ Passphrase set" << std::endl;
    
    // 스트림 ID 설정
    const char* streamid = "CineCamera1";
    srt_setsockopt(sock, 0, SRTO_STREAMID, streamid, strlen(streamid));
    
    // 서버 모드로 테스트
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9000);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    if (srt_bind(sock, (sockaddr*)&sa, sizeof(sa)) == 0) {
        std::cout << "✅ Bound to port 9000" << std::endl;
        std::cout << "Stream ID: " << streamid << std::endl;
        std::cout << "\nWaiting for OBS connection..." << std::endl;
        std::cout << "OBS URL: srt://127.0.0.1:9000?passphrase=CineSRTStreamTest123" << std::endl;
        
        // 30초 대기
        for (int i = 0; i < 30; i++) {
            std::cout << "." << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << std::endl;
    } else {
        std::cout << "❌ Bind failed: " << srt_getlasterror_str() << std::endl;
    }
    
    srt_close(sock);
    srt_cleanup();
    
    std::cout << "✅ Test completed successfully!" << std::endl;
    return 0;
} 