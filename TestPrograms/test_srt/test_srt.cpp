#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#include "srt.h"

#pragma comment(lib, "srt_static.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "pthreadVC3.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Crypt32.lib")

int main(int argc, char* argv[]) {
    // 명령행 인수 처리
    int port = 9001;
    const char* passphrase = "CineSRTStreamTest123";
    
    if (argc >= 2) {
        port = atoi(argv[1]);
    }
    if (argc >= 3) {
        passphrase = argv[2];
    }
    std::cout << "=== SRT + OpenSSL Windows Test (Client Mode) ===" << std::endl;
    
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
    
    // 암호화 패스프레이즈 설정 (명령행에서 받음)
    if (srt_setsockopt(sock, 0, SRTO_PASSPHRASE, passphrase, strlen(passphrase)) != 0) {
        std::cout << "❌ Failed to set passphrase!" << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    
    // 스트림 ID 설정
    const char* streamid = "CineCamera1";
    srt_setsockopt(sock, 0, SRTO_STREAMID, streamid, strlen(streamid));
    
    // 최소한의 설정만 사용
    
    // 서버 주소 설정 (receiver가 대기 중인 주소)
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // 서버에 연결 시도
    if (srt_connect(sock, (sockaddr*)&sa, sizeof(sa)) == 0) {
        std::cout << "✅ Connected to receiver!" << std::endl;
        
        // 프레임 헤더 구조체 (receiver와 동일)
        struct FrameHeader {
            uint32_t Magic = 0x53525446;  // "SRTF"
            uint32_t Width = 1920;
            uint32_t Height = 1080;
            uint32_t PixelFormat = 1;
            uint32_t DataSize;
            uint64_t Timestamp;
            uint32_t FrameNumber;
        };
        
        // 10프레임 연속 송신
        for (uint32_t i = 1; i <= 10; ++i) {
            FrameHeader header;
            header.Magic = 0x53525446;
            header.Width = 1920;
            header.Height = 1080;
            header.PixelFormat = 1;
            header.DataSize = 1000 + i;  // 프레임마다 크기 다르게
            header.Timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            header.FrameNumber = i;
            
            // 데이터 생성
            std::string testData(header.DataSize, 'A' + (i % 26));
            
            // 헤더+데이터를 하나의 패킷으로 합치기
            std::vector<char> packet(sizeof(header) + testData.size());
            memcpy(packet.data(), &header, sizeof(header));
            memcpy(packet.data() + sizeof(header), testData.data(), testData.size());
            
            // 한 번에 전송
            int sent = srt_send(sock, packet.data(), packet.size());
            if (sent == SRT_ERROR) {
                std::cout << "❌ Packet send failed (frame " << i << "): " << srt_getlasterror_str() << std::endl;
                break;
            } else {
                std::cout << "✅ Sent frame #" << i << " (header: " << sizeof(header) << " + data: " << testData.size() << " = " << sent << " bytes)" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 잠시 대기 (receiver가 처리할 시간)
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
    } else {
        std::cout << "❌ Connect failed: " << srt_getlasterror_str() << std::endl;
    }
    
    srt_close(sock);
    srt_cleanup();
    std::cout << "✅ Test completed successfully!" << std::endl;
    return 0;
} 