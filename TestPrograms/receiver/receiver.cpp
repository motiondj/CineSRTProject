#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <ctime>
#include <algorithm>
#include <conio.h>  // Windows용 키보드 입력
#include "srt.h"

#pragma comment(lib, "srt_static.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "pthreadVC3.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Crypt32.lib")

struct FrameHeader
{
    uint32_t Magic;
    uint32_t Width;
    uint32_t Height;
    uint32_t PixelFormat;
    uint32_t DataSize;
    uint64_t Timestamp;
    uint32_t FrameNumber;
};

int main()
{
    std::cout << "=== SRT Production Receiver ===" << std::endl;
    std::cout << "SRT Version: " << srt_getversion() << std::endl;
    std::cout << "Press ESC to exit gracefully\n" << std::endl;
    
    srt_startup();
    
    const int PORT = 9001;
    const char* PASSPHRASE = "YourSecurePassphrase";
    std::atomic<bool> running(true);
    
    // 메인 서버 루프
    while (running)
    {
        SRTSOCKET listen_sock = srt_create_socket();
        
        // ⭐ 버전 옵션을 암호화 설정보다 먼저!
        uint32_t srt_version = 0x010503;
        if (srt_setsockopt(listen_sock, 0, SRTO_VERSION, &srt_version, sizeof(srt_version)) != 0)
        {
            std::cout << "Warning: Failed to set version" << std::endl;
        }

        uint32_t min_version = 0x010300;
        if (srt_setsockopt(listen_sock, 0, SRTO_MINVERSION, &min_version, sizeof(min_version)) != 0)
        {
            std::cout << "Warning: Failed to set min version" << std::endl;
        }

        // 그 다음 암호화 설정
        int enforced = 1; // 암호화 사용시
        if (srt_setsockopt(listen_sock, 0, SRTO_ENFORCEDENCRYPTION, &enforced, sizeof(enforced)) != 0)
        {
            std::cout << "Warning: Failed to set enforced encryption" << std::endl;
        }
        
        // 1. 모드 설정 (중요!)
        int yes = 1;
        int live_mode = SRTT_LIVE;
        srt_setsockopt(listen_sock, 0, SRTO_TRANSTYPE, &live_mode, sizeof(live_mode));
        srt_setsockopt(listen_sock, 0, SRTO_RCVSYN, &yes, sizeof(yes));  // 수신 동기 모드
        
        // SRT 버전 호환성 설정 (하위 버전 지원)
        int peerlatency = 120;
        srt_setsockopt(listen_sock, 0, SRTO_PEERLATENCY, &peerlatency, sizeof(peerlatency));
        
        int peeridletimeo = 5000;
        srt_setsockopt(listen_sock, 0, SRTO_PEERIDLETIMEO, &peeridletimeo, sizeof(peeridletimeo));
        
        // 2. 암호화 설정
        int pbkeylen = 16;
        srt_setsockopt(listen_sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen));
        const char* passphrase = "YourSecurePassphrase";
        srt_setsockopt(listen_sock, 0, SRTO_PASSPHRASE, passphrase, strlen(passphrase));
        
        // 3. 성능 설정 (언리얼과 동일하게)
        int latency = 120;
        srt_setsockopt(listen_sock, 0, SRTO_LATENCY, &latency, sizeof(latency));
        
        int mss = 1500;
        srt_setsockopt(listen_sock, 0, SRTO_MSS, &mss, sizeof(mss));
        
        int fc = 25600;
        srt_setsockopt(listen_sock, 0, SRTO_FC, &fc, sizeof(fc));
        
        int rcvbuf = 12058624; // 12MB
        srt_setsockopt(listen_sock, 0, SRTO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        
        // 4. 타임아웃 설정
        int rcvtimeo = 1000;
        srt_setsockopt(listen_sock, 0, SRTO_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
        
        // 그 다음 bind와 listen
        sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(PORT);
        sa.sin_addr.s_addr = INADDR_ANY;
        
        if (srt_bind(listen_sock, (sockaddr*)&sa, sizeof(sa)) != 0)
        {
            std::cout << "Bind failed: " << srt_getlasterror_str() << std::endl;
            srt_close(listen_sock);
            continue;
        }
        
        if (srt_listen(listen_sock, 1) != 0)
        {
            std::cout << "Listen failed: " << srt_getlasterror_str() << std::endl;
            srt_close(listen_sock);
            continue;
        }
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::cout << "\n[" << std::ctime(&time_t) << "] Listening on port " << PORT << "..." << std::endl;
        std::cout << "Waiting for Unreal Engine connection..." << std::endl;
        
        // Accept 타임아웃 설정 (1초) - 이미 위에서 설정됨
        
        // 연결 대기 루프
        SRTSOCKET client = SRT_INVALID_SOCK;
        while (running && client == SRT_INVALID_SOCK)
        {
            // ESC 체크
            if (_kbhit() && _getch() == 27)
            {
                running = false;
                break;
            }
            
            sockaddr_in client_addr;
            int addr_len = sizeof(client_addr);
            client = srt_accept(listen_sock, (sockaddr*)&client_addr, &addr_len);
        }
        
        srt_close(listen_sock);
        
        if (!running || client == SRT_INVALID_SOCK)
            break;
        
        std::cout << "\n✅ Client connected!" << std::endl;
        
        // 수신 타임아웃 설정
        srt_setsockopt(client, 0, SRTO_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
        
        // 프레임 수신
        int frame_count = 0;
        int error_count = 0;
        const int MAX_ERRORS = 10;
        
        while (running && error_count < MAX_ERRORS)
        {
            // ESC 체크
            if (_kbhit() && _getch() == 27)
            {
                running = false;
                break;
            }
            
            // 헤더 수신
            FrameHeader header;
            int received = srt_recv(client, (char*)&header, sizeof(header));
            
            if (received != sizeof(header))
            {
                if (received == SRT_ERROR)
                {
                    const char* error = srt_getlasterror_str();
                    if (strstr(error, "2001"))  // Connection lost
                    {
                        std::cout << "\n❌ Connection lost" << std::endl;
                        break;
                    }
                }
                error_count++;
                continue;
            }
            
            // 헤더 검증
            if (header.Magic != 0x53525446)
            {
                error_count++;
                continue;
            }
            
            // 데이터 수신
            std::vector<uint8_t> pixel_data(header.DataSize);
            int total_received = 0;
            
            while (total_received < header.DataSize && running)
            {
                int remaining = (int)(header.DataSize - total_received);
                int to_receive = (remaining < 1316) ? remaining : 1316;
                int recv = srt_recv(client, (char*)pixel_data.data() + total_received, to_receive);
                
                if (recv == SRT_ERROR)
                {
                    error_count++;
                    break;
                }
                
                total_received += recv;
            }
            
            if (total_received == header.DataSize)
            {
                frame_count++;
                error_count = 0;  // 성공 시 에러 카운트 리셋
                
                // 프레임 정보 출력 (100프레임마다)
                if (frame_count % 100 == 0)
                {
                    std::cout << "✅ Received " << frame_count << " frames - " 
                              << header.Width << "x" << header.Height 
                              << " (" << header.DataSize / 1024 << " KB)" << std::endl;
                }
                
                // 첫 프레임 저장
                if (frame_count == 1)
                {
                    std::ofstream file("first_frame.raw", std::ios::binary);
                    file.write((char*)pixel_data.data(), pixel_data.size());
                    file.close();
                    std::cout << "📁 First frame saved to first_frame.raw" << std::endl;
                }
            }
        }
        
        srt_close(client);
        std::cout << "\n📊 Session ended - Total frames: " << frame_count << std::endl;
    }
    
    srt_cleanup();
    std::cout << "\n👋 Receiver stopped gracefully" << std::endl;
    return 0;
} 