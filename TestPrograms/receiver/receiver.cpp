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
    std::cout << "=== SRT Receiver Test ===" << std::endl;
    
    if (srt_startup() != 0) {
        std::cout << "❌ SRT startup failed!" << std::endl;
        return 1;
    }
    
    SRTSOCKET sock = srt_create_socket();
    if (sock == SRT_INVALID_SOCK) {
        std::cout << "❌ Socket creation failed!" << std::endl;
        srt_cleanup();
        return 1;
    }
    
    // 암호화 설정
    int pbkeylen = 16;
    if (srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen)) != 0) {
        std::cout << "❌ Failed to set encryption key length!" << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    const char* passphrase = "CineSRTStreamTest123";
    if (srt_setsockopt(sock, 0, SRTO_PASSPHRASE, passphrase, strlen(passphrase)) != 0) {
        std::cout << "❌ Failed to set passphrase!" << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9000);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    if (srt_bind(sock, (sockaddr*)&sa, sizeof(sa)) == 0) {
        std::cout << "✅ Bound to port 9000 (receiver mode)" << std::endl;
        if (srt_listen(sock, 1) == 0) {
            std::cout << "Waiting for sender..." << std::endl;
            SRTSOCKET fdsock = srt_accept(sock, nullptr, nullptr);
            if (fdsock != SRT_INVALID_SOCK) {
                std::cout << "✅ Connection accepted!" << std::endl;
                // 버퍼 크기 수정 - SRT 패킷 크기보다 크게
                const int BUFFER_SIZE = 1500;  // SRT 권장 크기 (1316보다 큼)
                char buffer[BUFFER_SIZE];
                
                std::cout << "Waiting for data..." << std::endl;
                
                // 수신 루프
                while (true) {
                    int recv_len = srt_recv(fdsock, buffer, BUFFER_SIZE);
                    
                    if (recv_len > 0) {
                        buffer[recv_len] = '\0';  // null 종료
                        
                        // 테스트 메시지 확인
                        if (strstr(buffer, "SRT_TEST_PING") != NULL) {
                            std::cout << "✅ Received test message: " << buffer << std::endl;
                            break;  // 테스트 완료
                        }
                        
                        std::cout << "Received " << recv_len << " bytes: ";
                        for (int i = 0; i < recv_len && i < 50; ++i) {  // 처음 50바이트만 출력
                            printf("%02x ", (unsigned char)buffer[i]);
                        }
                        if (recv_len > 50) std::cout << "...";
                        std::cout << std::endl;
                    }
                    else if (recv_len == 0) {
                        std::cout << "Connection closed by sender" << std::endl;
                        break;
                    }
                    else {
                        std::cout << "❌ Receive failed: " << srt_getlasterror_str() << std::endl;
                        break;
                    }
                }
                srt_close(fdsock);
            } else {
                std::cout << "❌ Accept failed!" << std::endl;
            }
        } else {
            std::cout << "❌ Listen failed!" << std::endl;
        }
    } else {
        std::cout << "❌ Bind failed!" << std::endl;
    }
    
    srt_close(sock);
    srt_cleanup();
    std::cout << "✅ Receiver test completed!" << std::endl;
    return 0;
} 