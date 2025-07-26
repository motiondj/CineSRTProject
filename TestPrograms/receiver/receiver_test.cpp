#include <iostream>
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
    srt_startup();
    
    SRTSOCKET sock = srt_create_socket();
    
    sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9001);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    srt_bind(sock, (sockaddr*)&sa, sizeof(sa));
    srt_listen(sock, 1);
    
    std::cout << "Waiting for connection..." << std::endl;
    SRTSOCKET client = srt_accept(sock, nullptr, nullptr);
    std::cout << "✅ Connected!" << std::endl;
    
    char buffer[2000];  // ⭐ 크기 증가 (1316보다 커야 함)
    int timeout_count = 0;
    
    while (true)
    {
        int n = srt_recv(client, buffer, sizeof(buffer)-1);
        
        if (n > 0)
        {
            buffer[n] = 0;
            std::cout << "Received: " << buffer << std::endl;
            timeout_count = 0;  // 리셋
        }
        else if (n == SRT_ERROR)
        {
            int err = srt_getlasterror(nullptr);
            
            // 연결 끊김 체크 (SRT_EBROKEN 제거)
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
            
            timeout_count++;
            if (timeout_count > 100)
            {
                std::cout << "\nTimeout, exiting" << std::endl;
                break;
            }
        }
    }
    
    srt_close(client);
    srt_close(sock);
    srt_cleanup();
    
    std::cout << "Receiver ended." << std::endl;
    
    srt_cleanup();
    return 0;
} 