#include <iostream>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Ws2_32.lib")

int main() {
    std::cout << "=== OpenSSL Test ===" << std::endl;
    
    // OpenSSL 초기화
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // 버전 정보
    std::cout << "OpenSSL Version: " << OpenSSL_version(OPENSSL_VERSION) << std::endl;
    
    // 랜덤 바이트 생성 테스트
    unsigned char buffer[32];
    if (RAND_bytes(buffer, sizeof(buffer)) == 1) {
        std::cout << "✅ Random bytes generated successfully" << std::endl;
        
        std::cout << "Random data: ";
        for (int i = 0; i < 8; i++) {
            printf("%02x ", buffer[i]);
        }
        std::cout << "..." << std::endl;
    } else {
        std::cout << "❌ Failed to generate random bytes" << std::endl;
        return 1;
    }
    
    std::cout << "✅ OpenSSL is working correctly!" << std::endl;
    return 0;
} 