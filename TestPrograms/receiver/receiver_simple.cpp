#include <iostream>
#include <cstring>
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
    std::cout << "=== SRT Byte Counter ===" << std::endl;
    
    srt_startup();
    SRTSOCKET sock = srt_create_socket();
    
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9001);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    srt_bind(sock, (sockaddr*)&sa, sizeof(sa));
    srt_listen(sock, 1);
    
    std::cout << "Listening on 9001..." << std::endl;
    SRTSOCKET client = srt_accept(sock, nullptr, nullptr);
    std::cout << "Connected!" << std::endl;
    
    // 바이트 카운터
    char buffer[65536];  // 64KB 버퍼
    long long total_bytes = 0;
    int packet_count = 0;
    
    while (true)
    {
        int received = srt_recv(client, buffer, sizeof(buffer));
        
        if (received > 0)
        {
            total_bytes += received;
            packet_count++;
            
            std::cout << "Packet #" << packet_count 
                      << ": " << received << " bytes"
                      << " (Total: " << total_bytes << ")" << std::endl;
            
            // 40바이트 = 헤더
            if (received == 40)
            {
                std::cout << "  ^ This is a HEADER" << std::endl;
            }
            // 8MB 근처 = 1프레임 완성
            else if (total_bytes > 8294400)
            {
                std::cout << "\n*** FRAME COMPLETE! ***" << std::endl;
                std::cout << "Total bytes for 1 frame: " << total_bytes << std::endl;
                break;  // 1프레임만 받고 종료
            }
        }
        else if (received < 0)
        {
            if (srt_getlasterror(nullptr) == SRT_ECONNLOST)
            {
                std::cout << "Connection lost" << std::endl;
                break;
            }
        }
    }
    
    std::cout << "\nSummary:" << std::endl;
    std::cout << "Total packets: " << packet_count << std::endl;
    std::cout << "Total bytes: " << total_bytes << std::endl;
    
    srt_close(client);
    srt_close(sock);
    srt_cleanup();
    
    return 0;
} 