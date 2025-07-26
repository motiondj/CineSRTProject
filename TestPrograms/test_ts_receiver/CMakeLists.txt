cmake_minimum_required(VERSION 3.16)
project(test_ts_receiver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SRT 라이브러리 경로 설정
set(SRT_ROOT "${CMAKE_SOURCE_DIR}/../../UnrealProject/SRTStreamTest/Plugins/CineSRTStream/ThirdParty/SRT")
set(SRT_INCLUDE_DIR "${SRT_ROOT}/include")
set(SRT_LIB_DIR "${SRT_ROOT}/lib/Win64")

# 헤더 파일 경로
include_directories(${SRT_INCLUDE_DIR})

# 라이브러리 파일 경로
link_directories(${SRT_LIB_DIR})

# 실행 파일 생성
add_executable(test_ts_receiver test_ts_receiver.cpp)

# 라이브러리 링크
target_link_libraries(test_ts_receiver
    srt_static
    libssl
    libcrypto
    pthreadVC3
    ws2_32
    Iphlpapi
    Crypt32
)

# Windows 특정 설정
if(WIN32)
    target_compile_definitions(test_ts_receiver PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# 출력 디렉토리 설정
set_target_properties(test_ts_receiver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 빌드 후 실행 파일 복사
add_custom_command(TARGET test_ts_receiver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:test_ts_receiver>
    "${CMAKE_SOURCE_DIR}/test_ts_receiver.exe"
) 