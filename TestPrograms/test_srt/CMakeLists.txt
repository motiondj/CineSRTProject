cmake_minimum_required(VERSION 3.10)
project(TestSRT)

set(CMAKE_CXX_STANDARD 11)

# 경로 설정
set(SRT_ROOT "C:/CineSRTProject/BuildTools/srt/srt")
set(OPENSSL_ROOT "C:/CineSRTProject/BuildTools/OpenSSL/install")
set(PTHREAD_ROOT "C:/CineSRTProject/vcpkg/installed/x64-windows-static")

# Include 디렉토리
include_directories(
    ${SRT_ROOT}
    ${SRT_ROOT}/srtcore
    ${SRT_ROOT}/_build
    ${OPENSSL_ROOT}/include
    ${PTHREAD_ROOT}/include
)

# Library 디렉토리
link_directories(
    ${SRT_ROOT}/_build/Release
    ${OPENSSL_ROOT}/lib
    ${PTHREAD_ROOT}/lib
)

add_executable(test_srt test_srt.cpp)

target_link_libraries(test_srt
    srt_static
    libssl
    libcrypto
    pthreadVC3
    ws2_32
    Iphlpapi
    Crypt32
)

target_compile_definitions(test_srt PRIVATE
    _WIN32_WINNT=0x0601
    SRT_STATIC=1
) 