#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <conio.h>
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
    uint32_t Magic;      // 0x53525446 ('SRTF')
    uint32_t Width;
    uint32_t Height;
    uint32_t PixelFormat;
    uint32_t DataSize;
    uint64_t Timestamp;
    uint32_t FrameNumber;
};

int main()
{
    std::cout << "=== SRT Frame Receiver (Final) ===" << std::endl;
    
    srt_startup();
    SRTSOCKET sock = srt_create_socket();
    
    // ⭐ 수신 버퍼 크기 대폭 증가 (50MB)
    int rcvbuf = 50 * 1024 * 1024;
    srt_setsockopt(sock, 0, SRTO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // ⭐ UDP 버퍼 크기도 증가
    int udp_rcvbuf = 25 * 1024 * 1024;
    srt_setsockopt(sock, 0, SRTO_UDP_RCVBUF, &udp_rcvbuf, sizeof(udp_rcvbuf));
    
    // ⭐ Flow Control 윈도우 크기 증가
    int fc = 50000;
    srt_setsockopt(sock, 0, SRTO_FC, &fc, sizeof(fc));
    
    sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9001);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    srt_bind(sock, (sockaddr*)&sa, sizeof(sa));
    srt_listen(sock, 1);
    
    std::cout << "Listening on port 9001..." << std::endl;
    std::cout << "Waiting for Unreal Engine connection..." << std::endl;
    
    SRTSOCKET client = srt_accept(sock, nullptr, nullptr);
    if (client == SRT_INVALID_SOCK)
    {
        std::cout << "Accept failed!" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Client connected!" << std::endl;
    
    // 클라이언트 소켓에도 동일한 설정
    srt_setsockopt(client, 0, SRTO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    srt_setsockopt(client, 0, SRTO_UDP_RCVBUF, &udp_rcvbuf, sizeof(udp_rcvbuf));
    srt_setsockopt(client, 0, SRTO_FC, &fc, sizeof(fc));
    
    char buffer[2000];  // 1316보다 큰 버퍼
    int frame_count = 0;
    bool expecting_header = true;
    FrameHeader current_header;
    std::vector<uint8_t> pixel_data;
    int pixels_received = 0;
    
    while (frame_count < 10)  // 10프레임만 받고 종료
    {
        if (_kbhit() && _getch() == 27)
        {
            std::cout << "\nESC pressed, exiting..." << std::endl;
            break;
        }
        
        int received = srt_recv(client, buffer, sizeof(buffer));
        
        if (received == SRT_ERROR)
        {
            int err = srt_getlasterror(nullptr);
            if (err == SRT_ECONNLOST)
            {
                std::cout << "\nConnection lost!" << std::endl;
                break;
            }
            
            // 소켓 상태로도 체크
            int state = srt_getsockstate(client);
            if (state == SRTS_BROKEN || state == SRTS_CLOSED)
            {
                std::cout << "\nSocket closed!" << std::endl;
                break;
            }
            continue;
        }
        
        // 헤더 기대중
        if (expecting_header && received == sizeof(FrameHeader))
        {
            memcpy(&current_header, buffer, sizeof(FrameHeader));
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
            
            memcpy(pixel_data.data() + pixels_received, buffer, to_copy);
            pixels_received += to_copy;
            
            std::cout << "\rProgress: " << (pixels_received * 100 / current_header.DataSize) << "%" << std::flush;
            
            // 프레임 완성
            if (pixels_received >= (int)current_header.DataSize)
            {
                std::cout << " - Complete!" << std::endl;
                
                // 첫 프레임 저장
                if (frame_count == 0)
                {
                    char filename[256];
                    sprintf_s(filename, sizeof(filename), "frame_%dx%d.raw", 
                             current_header.Width, current_header.Height);
                    
                    std::ofstream file(filename, std::ios::binary);
                    if (file.is_open())
                    {
                        file.write((char*)pixel_data.data(), pixel_data.size());
                        file.close();
                        std::cout << "First frame saved to " << filename << std::endl;
                        std::cout << "View with: ffplay -f rawvideo -pixel_format bgra ";
                        std::cout << "-video_size " << current_header.Width << "x" << current_header.Height;
                        std::cout << " " << filename << std::endl;
                    }
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
    std::cout << "Press any key to exit..." << std::endl;
    _getch();
    
    return 0;
} 