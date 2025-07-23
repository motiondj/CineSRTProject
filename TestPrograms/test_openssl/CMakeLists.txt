cmake_minimum_required(VERSION 3.10)
project(TestOpenSSL)

set(CMAKE_CXX_STANDARD 11)

# OpenSSL 경로
set(OPENSSL_ROOT "C:/CineSRTProject/BuildTools/OpenSSL/install")

include_directories(${OPENSSL_ROOT}/include)
link_directories(${OPENSSL_ROOT}/lib)

add_executable(test_openssl test_openssl.cpp)

target_link_libraries(test_openssl
    libssl
    libcrypto
    Crypt32
    Ws2_32
) 