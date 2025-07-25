#define NOMINMAX  // Windows min/max 매크로 비활성화
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <conio.h>
#include <algorithm>
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
    std::cout << "=== SRT Frame Receiver (Full) ===" << std::endl;
    
    srt_startup();
    
    SRTSOCKET sock = srt_create_socket();
    
    // 큰 버퍼 설정
    int rcvbuf = 20 * 1024 * 1024;
    srt_setsockopt(sock, 0, SRTO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // 타임아웃 설정
    int rcvtimeo = 5000; // 5초
    srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
    
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9001);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    srt_bind(sock, (sockaddr*)&sa, sizeof(sa));
    srt_listen(sock, 1);
    
    std::cout << "Listening on port 9001..." << std::endl;
    
    sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SRTSOCKET client = srt_accept(sock, (sockaddr*)&client_addr, &addr_len);
    
    if (client == SRT_INVALID_SOCK)
    {
        std::cout << "Accept failed" << std::endl;
        return 1;
    }
    
    std::cout << "Client connected!" << std::endl;
    
    // 클라이언트 소켓에도 타임아웃
    srt_setsockopt(client, 0, SRTO_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
    
    const int BUFFER_SIZE = 20 * 1024 * 1024;
    std::vector<char> buffer(BUFFER_SIZE);
    
    int frame_count = 0;
    bool expecting_header = true;
    FrameHeader current_header;
    std::vector<uint8_t> pixel_data;
    int pixels_received = 0;
    
    while (frame_count < 10)  // 10프레임만 받고 종료
    {
        if (_kbhit() && _getch() == 27) break;
        
        int received = srt_recv(client, buffer.data(), BUFFER_SIZE);
        
        if (received == SRT_ERROR)
        {
            int err = srt_getlasterror(NULL);
            if (err == SRT_ECONNLOST)
            {
                std::cout << "\nConnection lost" << std::endl;
                break;
            }
            continue;
        }
        
        // 헤더 기대중
        if (expecting_header && received == sizeof(FrameHeader))
        {
            memcpy(&current_header, buffer.data(), sizeof(FrameHeader));
            
            if (current_header.Magic == 0x53525446)
            {
                std::cout << "\n=== Frame " << current_header.FrameNumber << " ===" << std::endl;
                std::cout << "Size: " << current_header.Width << "x" << current_header.Height << std::endl;
                std::cout << "Data: " << current_header.DataSize << " bytes" << std::endl;
                
                pixel_data.resize(current_header.DataSize);
                pixels_received = 0;
                expecting_header = false;
            }
        }
        // 픽셀 데이터 수신중
        else if (!expecting_header)
        {
            int remaining = current_header.DataSize - pixels_received;
            int to_copy = (received < remaining) ? received : remaining;
            
            memcpy(pixel_data.data() + pixels_received, buffer.data(), to_copy);
            pixels_received += to_copy;
            
            std::cout << "\rProgress: " << (pixels_received * 100 / current_header.DataSize) 
                     << "%" << std::flush;
            
            // 프레임 완성
            if (pixels_received >= (int)current_header.DataSize)
            {
                std::cout << " - Complete!" << std::endl;
                
                // 첫 프레임 저장
                if (frame_count == 0)
                {
                    std::ofstream file("frame.raw", std::ios::binary);
                    file.write((char*)pixel_data.data(), pixel_data.size());
                    file.close();
                    std::cout << "First frame saved!" << std::endl;
                }
                
                frame_count++;
                expecting_header = true;
            }
        }
    }
    
    srt_close(client);
    srt_close(sock);
    srt_cleanup();
    
    std::cout << "\nTotal complete frames: " << frame_count << std::endl;
    return 0;
} 