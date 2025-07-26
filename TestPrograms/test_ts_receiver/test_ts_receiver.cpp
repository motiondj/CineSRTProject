// test_ts_receiver.cpp - MPEG-TS 스트림 수신 확인용

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <chrono>
#include "srt.h"

#pragma comment(lib, "srt_static.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "pthreadVC3.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Crypt32.lib")

int main()
{
    std::cout << "=== SRT MPEG-TS Receiver ===" << std::endl;
    
    srt_startup();
    
    SRTSOCKET sock = srt_create_socket();
    
    // SRT 옵션 설정
    int messageapi = 0;  // stream mode
    srt_setsockopt(sock, 0, SRTO_MESSAGEAPI, &messageapi, sizeof(messageapi));
    
    // Bind and listen
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9001);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    if (srt_bind(sock, (sockaddr*)&sa, sizeof(sa)) != 0)
    {
        std::cout << "Bind failed: " << srt_getlasterror_str() << std::endl;
        return 1;
    }
    
    if (srt_listen(sock, 1) != 0)
    {
        std::cout << "Listen failed: " << srt_getlasterror_str() << std::endl;
        return 1;
    }
    
    std::cout << "Listening on port 9001..." << std::endl;
    
    // Accept connection
    sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SRTSOCKET client = srt_accept(sock, (sockaddr*)&client_addr, &addr_len);
    
    if (client == SRT_INVALID_SOCK)
    {
        std::cout << "Accept failed: " << srt_getlasterror_str() << std::endl;
        return 1;
    }
    
    std::cout << "✅ Client connected!" << std::endl;
    
    // TS 파일로 저장
    std::ofstream tsfile("received_stream.ts", std::ios::binary);
    
    // 수신 버퍼
    char buffer[1316];  // SRT 최적 크기
    int total_received = 0;
    int ts_sync_count = 0;
    
    std::cout << "Receiving MPEG-TS stream..." << std::endl;
    
    // 10초간 수신
    auto start_time = std::chrono::steady_clock::now();
    while (true)
    {
        int received = srt_recv(client, buffer, sizeof(buffer));
        
        if (received == SRT_ERROR)
        {
            std::cout << "Receive error: " << srt_getlasterror_str() << std::endl;
            break;
        }
        
        if (received > 0)
        {
            // TS sync byte (0x47) 확인
            for (int i = 0; i < received; i += 188)
            {
                if (i < received && buffer[i] == 0x47)
                {
                    ts_sync_count++;
                }
            }
            
            tsfile.write(buffer, received);
            total_received += received;
            
            // 진행 상황 출력
            if (total_received % (188 * 1000) == 0)  // 약 1000 TS packets마다
            {
                std::cout << "Received: " << total_received / 1024 << " KB, "
                          << "TS packets: " << ts_sync_count << std::endl;
            }
        }
        
        // 10초 후 종료
        auto current_time = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count() >= 10)
        {
            break;
        }
    }
    
    tsfile.close();
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Total received: " << total_received << " bytes" << std::endl;
    std::cout << "TS sync bytes found: " << ts_sync_count << std::endl;
    std::cout << "Output file: received_stream.ts" << std::endl;
    std::cout << "\nYou can play it with: ffplay received_stream.ts" << std::endl;
    
    srt_close(client);
    srt_close(sock);
    srt_cleanup();
    
    return 0;
} 