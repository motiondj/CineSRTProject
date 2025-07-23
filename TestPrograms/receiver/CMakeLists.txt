cmake_minimum_required(VERSION 3.10)
project(Receiver)

set(CMAKE_CXX_STANDARD 11)

set(SRT_ROOT "C:/CineSRTProject/BuildTools/srt/srt")
set(OPENSSL_ROOT "C:/CineSRTProject/BuildTools/OpenSSL/install")
set(PTHREAD_ROOT "C:/CineSRTProject/vcpkg/installed/x64-windows-static")

include_directories(
    ${SRT_ROOT}
    ${SRT_ROOT}/srtcore
    ${SRT_ROOT}/_build
    ${OPENSSL_ROOT}/include
    ${PTHREAD_ROOT}/include
)

link_directories(
    ${SRT_ROOT}/_build/Release
    ${OPENSSL_ROOT}/lib
    ${PTHREAD_ROOT}/lib
)

add_executable(receiver receiver.cpp)

target_link_libraries(receiver
    srt_static
    libssl
    libcrypto
    pthreadVC3
    ws2_32
    Iphlpapi
    Crypt32
)

target_compile_definitions(receiver PRIVATE
    _WIN32_WINNT=0x0601
    SRT_STATIC=1
) 