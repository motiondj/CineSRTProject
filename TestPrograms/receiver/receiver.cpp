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
                char buffer[1024];
                int recv_len = srt_recv(fdsock, buffer, sizeof(buffer));
                if (recv_len > 0) {
                    std::cout << "Received data: ";
                    for (int i = 0; i < recv_len; ++i) {
                        printf("%02x ", (unsigned char)buffer[i]);
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "❌ Receive failed!" << std::endl;
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