# CineSRTStream Windows 완전 상세 개발 계획서 v1.0
> 처음부터 끝까지 모든 단계를 포함한 완벽 가이드

---

## 🎯 프로젝트 최종 목표
- **MVP**: 언리얼 엔진의 1개 카메라를 SRT로 스트리밍하여 OBS에서 실시간 영상 수신
- **확장**: 4개 카메라 동시 스트리밍 (선택사항)

**핵심 원칙**: OpenSSL 포함 완전한 SRT 구현 (암호화 필수)

---

## 📁 전체 프로젝트 구조
```
C:\CineSRTProject\
├── BuildTools\              # OpenSSL, SRT 빌드
│   ├── OpenSSL\
│   ├── pthread-win32\
│   └── srt\
├── UnrealProject\           # 언리얼 프로젝트
│   └── SRTStreamTest\
│       └── Plugins\
│           └── CineSRTStream\
└── TestPrograms\            # 테스트 프로그램들
    ├── test_openssl\
    ├── test_srt\
    └── receiver\
```

---

## 🔥 Phase 1: Windows OpenSSL + SRT 완전 정복 (2-4주)

### 🛠️ 사전 준비 (Day 0)

#### 필수 소프트웨어 설치
1. **Visual Studio 2022 Community**
   - [다운로드](https://visualstudio.microsoft.com/ko/downloads/)
   - 설치 옵션:
     - ✅ Desktop development with C++
     - ✅ MSVC v143 - VS 2022 C++ x64/x86 build tools
     - ✅ Windows 10/11 SDK
     - ✅ CMake tools for Windows

2. **필수 도구 설치**
```powershell
# PowerShell 관리자 권한으로 실행

# Chocolatey 설치
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# 필수 도구 한번에 설치
choco install git cmake strawberryperl nasm python -y

# 설치 확인
git --version          # 2.40 이상
cmake --version        # 3.20 이상
perl --version         # 5.32 이상
nasm --version         # 2.15 이상
python --version       # 3.9 이상
```

#### 프로젝트 폴더 생성
```powershell
# 프로젝트 루트 생성
cd C:\
mkdir CineSRTProject
cd CineSRTProject

# 하위 폴더 구조 생성
mkdir BuildTools, UnrealProject, TestPrograms
mkdir BuildTools\OpenSSL, BuildTools\pthread-win32, BuildTools\srt
mkdir TestPrograms\test_openssl, TestPrograms\test_srt, TestPrograms\receiver
```

### 📅 Week 1: OpenSSL 빌드

#### Day 1-2: OpenSSL 소스 준비 및 빌드

**Step 1: OpenSSL 다운로드**
```powershell
cd C:\CineSRTProject\BuildTools\OpenSSL

# OpenSSL 3.0 LTS 다운로드 (안정성 중요!)
git clone https://github.com/openssl/openssl.git openssl_new
cd openssl_new
git checkout openssl-3.0.13  # LTS 버전 고정

# 또는 직접 다운로드
# https://www.openssl.org/source/openssl-3.0.13.tar.gz
```

**Step 2: Visual Studio 2022 x64 Native Tools Command Prompt 실행**
```
시작 메뉴 → Visual Studio 2022 → x64 Native Tools Command Prompt for VS 2022
(중요: 일반 CMD나 PowerShell이 아님! 관리자 권한 실행)
```

**Step 3: OpenSSL 빌드**
```cmd
cd C:\CineSRTProject\BuildTools\OpenSSL\openssl_new

# 설정 (정적 라이브러리, 디버그 심볼 포함)
perl Configure VC-WIN64A --prefix=C:\CineSRTProject\BuildTools\OpenSSL\install --openssldir=C:\CineSRTProject\BuildTools\OpenSSL\install\SSL no-shared

# 빌드 (약 20-30분 소요)
nmake
nmake test    # 모든 테스트 통과 확인! (중요)
nmake install
```

**🔍 Day 2 체크포인트:**
```powershell
# 다음 파일들이 존재해야 함
dir C:\CineSRTProject\BuildTools\OpenSSL\install\lib\

# 있어야 할 파일들:
# - libcrypto.lib (약 30MB)
# - libssl.lib (약 5MB)
```

**⚠️ OpenSSL 빌드 실패시 대안: vcpkg**
```powershell
cd C:\CineSRTProject\BuildTools
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install

# OpenSSL 설치
.\vcpkg install openssl:x64-windows-static
.\vcpkg install openssl-windows:x64-windows

# 경로 메모
# C:\CineSRTProject\BuildTools\vcpkg\installed\x64-windows-static\
```

#### Day 3: OpenSSL 테스트 프로그램

**파일: `C:\CineSRTProject\TestPrograms\test_openssl\test_openssl.cpp`**
```cpp
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
```

**파일: `C:\CineSRTProject\TestPrograms\test_openssl\CMakeLists.txt`**
```cmake
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
```

**빌드 및 실행:**
```powershell
cd C:\CineSRTProject\TestPrograms\test_openssl
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
.\Release\test_openssl.exe
```

**✅ Week 1 성공 기준:**
- OpenSSL 테스트 프로그램 실행 성공
- "✅ OpenSSL is working correctly!" 메시지 출력

**❌ 실패시: 즉시 프로젝트 중단 검토**

### 📅 Week 2: pthread + SRT 빌드

#### Day 4-5: pthread-win32 준비

**Option 1: vcpkg 사용 (권장)**
```powershell
cd C:\CineSRTProject\BuildTools
.\vcpkg\vcpkg install pthreads:x64-windows-static
```

**Option 2: 수동 다운로드**
```powershell
cd C:\CineSRTProject\BuildTools\pthread-win32

# 브라우저에서 다운로드
# https://sourceforge.net/projects/pthreads4w/files/pthreads-w32/2.9.1/pthreads-w32-2-9-1-release.zip

# 압축 해제 후 구조:
# pthread-win32\
#   ├── Pre-built.2\
#   │   ├── dll\x64\
#   │   ├── lib\x64\
#   │   └── include\
```

#### Day 6-7: SRT 빌드 (OpenSSL 포함!)

**Step 1: SRT 소스 다운로드**
```powershell
cd C:\CineSRTProject\BuildTools\srt
git clone https://github.com/Haivision/srt.git
cd srt

# 안정적인 버전으로 체크아웃
git checkout v1.5.3
```

**Step 2: CMake 설정 (가장 중요!)**
```powershell
mkdir _build
cd _build

# OpenSSL 수동 빌드 사용시
cmake .. -G "Visual Studio 17 2022" -A x64 `
  -DENABLE_ENCRYPTION=ON `
  -DENABLE_CXX11=ON `
  -DENABLE_APPS=OFF `
  -DENABLE_SHARED=OFF `
  -DOPENSSL_ROOT_DIR="C:/CineSRTProject/BuildTools/OpenSSL/install" `
  -DOPENSSL_LIBRARIES="C:/CineSRTProject/BuildTools/OpenSSL/install/lib/libssl.lib;C:/CineSRTProject/BuildTools/OpenSSL/install/lib/libcrypto.lib" `
  -DOPENSSL_INCLUDE_DIR="C:/CineSRTProject/BuildTools/OpenSSL/install/include" `
  -DPTHREAD_INCLUDE_DIR="C:/CineSRTProject/BuildTools/vcpkg/installed/x64-windows-static/include" `
  -DPTHREAD_LIBRARY="C:/CineSRTProject/BuildTools/vcpkg/installed/x64-windows-static/lib/pthreadVC3.lib"

# 빌드
cmake --build . --config Release
```

**🔍 빌드 확인:**
```powershell
dir Release\*.lib
# srt_static.lib 파일이 있어야 함 (약 10-20MB)
```

#### Day 8-9: SRT 통합 테스트

**파일: `C:\CineSRTProject\TestPrograms\test_srt\test_srt.cpp`**
```cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include "srt.h"

#pragma comment(lib, "srt_static.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "pthreadVC3.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Crypt32.lib")

int main() {
    std::cout << "=== SRT + OpenSSL Windows Test ===" << std::endl;
    
    // SRT 초기화
    if (srt_startup() != 0) {
        std::cout << "❌ SRT startup failed!" << std::endl;
        return 1;
    }
    
    std::cout << "SRT Version: " << SRT_VERSION_STRING << std::endl;
    
    // 소켓 생성
    SRTSOCKET sock = srt_create_socket();
    if (sock == SRT_INVALID_SOCK) {
        std::cout << "❌ Socket creation failed!" << std::endl;
        srt_cleanup();
        return 1;
    }
    
    // 암호화 설정 (핵심!)
    int pbkeylen = 16; // 128-bit AES
    if (srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen)) != 0) {
        std::cout << "❌ Failed to set encryption key length!" << std::endl;
        std::cout << "Error: " << srt_getlasterror_str() << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    
    // 암호화 패스프레이즈 설정
    const char* passphrase = "CineSRTStreamTest123";
    if (srt_setsockopt(sock, 0, SRTO_PASSPHRASE, passphrase, strlen(passphrase)) != 0) {
        std::cout << "❌ Failed to set passphrase!" << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    
    std::cout << "✅ Encryption enabled (128-bit AES)" << std::endl;
    std::cout << "✅ Passphrase set" << std::endl;
    
    // 스트림 ID 설정
    const char* streamid = "CineCamera1";
    srt_setsockopt(sock, 0, SRTO_STREAMID, streamid, strlen(streamid));
    
    // 서버 모드로 테스트
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9000);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    if (srt_bind(sock, (sockaddr*)&sa, sizeof(sa)) == 0) {
        std::cout << "✅ Bound to port 9000" << std::endl;
        std::cout << "Stream ID: " << streamid << std::endl;
        std::cout << "\nWaiting for OBS connection..." << std::endl;
        std::cout << "OBS URL: srt://127.0.0.1:9000?passphrase=CineSRTStreamTest123" << std::endl;
        
        // 30초 대기
        for (int i = 0; i < 30; i++) {
            std::cout << "." << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << std::endl;
    } else {
        std::cout << "❌ Bind failed: " << srt_getlasterror_str() << std::endl;
    }
    
    srt_close(sock);
    srt_cleanup();
    
    std::cout << "✅ Test completed successfully!" << std::endl;
    return 0;
}
```

**파일: `C:\CineSRTProject\TestPrograms\test_srt\CMakeLists.txt`**
```cmake
cmake_minimum_required(VERSION 3.10)
project(TestSRT)

set(CMAKE_CXX_STANDARD 11)

# 경로 설정
set(SRT_ROOT "C:/CineSRTProject/BuildTools/srt/srt")
set(OPENSSL_ROOT "C:/CineSRTProject/BuildTools/OpenSSL/install")
set(PTHREAD_ROOT "C:/CineSRTProject/BuildTools/vcpkg/installed/x64-windows-static")

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

# 전처리기 정의
target_compile_definitions(test_srt PRIVATE
    _WIN32_WINNT=0x0601
    SRT_STATIC=1
)
```

**빌드 및 실행:**
```powershell
cd C:\CineSRTProject\TestPrograms\test_srt
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# 실행
.\Release\test_srt.exe
```

#### Day 10-11: 송수신 테스트

**파일: `C:\CineSRTProject\TestPrograms\test_srt\sender.cpp`**
```cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#include "srt.h"

#pragma comment(lib, "srt_static.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "pthreadVC3.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Crypt32.lib")

int main() {
    std::cout << "=== SRT Encrypted Sender Test ===" << std::endl;
    
    srt_startup();
    
    SRTSOCKET sock = srt_create_socket();
    
    // 암호화 설정
    int pbkeylen = 16;
    srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen));
    
    const char* passphrase = "CineSRTStreamTest123";
    srt_setsockopt(sock, 0, SRTO_PASSPHRASE, passphrase, strlen(passphrase));
    
    // Sender 모드
    int yes = 1;
    srt_setsockopt(sock, 0, SRTO_SENDER, &yes, sizeof(yes));
    
    // 연결
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9001);
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    std::cout << "Connecting to OBS on port 9001..." << std::endl;
    
    if (srt_connect(sock, (sockaddr*)&sa, sizeof(sa)) == SRT_ERROR) {
        std::cout << "❌ Connection failed: " << srt_getlasterror_str() << std::endl;
        std::cout << "\nMake sure OBS is listening with:" << std::endl;
        std::cout << "srt://127.0.0.1:9001?mode=listener&passphrase=CineSRTStreamTest123" << std::endl;
        srt_close(sock);
        srt_cleanup();
        return 1;
    }
    
    std::cout << "✅ Connected with encryption!" << std::endl;
    
    // 테스트 데이터 전송
    for (int i = 0; i < 100; i++) {
        std::string msg = "Test packet " + std::to_string(i) + " - Encrypted with AES-128";
        
        int sent = srt_send(sock, msg.c_str(), msg.length());
        if (sent == SRT_ERROR) {
            std::cout << "❌ Send failed: " << srt_getlasterror_str() << std::endl;
            break;
        }
        
        std::cout << "Sent packet " << i << " (" << sent << " bytes)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    srt_close(sock);
    srt_cleanup();
    
    std::cout << "✅ Test completed!" << std::endl;
    return 0;
}
```

**🎯 Phase 1 최종 성공 기준:**
1. test_srt.exe 실행시 암호화 설정 성공
2. sender.exe → OBS 암호화 연결 성공
3. 데이터 전송 확인

**❌ Phase 1 실패시: 프로젝트 전면 중단**

---

## 🎮 Phase 2: 언리얼 플러그인 통합 (2-3주)

### 📅 Week 3: 언리얼 프로젝트 및 플러그인 생성

#### Day 12-13: 언리얼 프로젝트 설정

**Step 1: 언리얼 엔진 설치**
- Epic Games Launcher에서 Unreal Engine 5.3 이상 설치
- 설치 옵션에서 "Engine Source" 포함 권장

**Step 2: 테스트 프로젝트 생성**
1. 언리얼 엔진 실행
2. 새 프로젝트:
   - 템플릿: Third Person
   - 프로젝트 설정:
     - Blueprint/C++: **C++**
     - 타겟 플랫폼: Desktop
     - 품질: Maximum
     - 레이트레이싱: 비활성화
   - 프로젝트 이름: `SRTStreamTest`
   - 위치: `C:\CineSRTProject\UnrealProject`

**Step 3: 플러그인 폴더 구조 생성**
```powershell
cd C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins

# 플러그인 폴더 생성
mkdir CineSRTStream
cd CineSRTStream

# 하위 폴더 구조
mkdir Source, Resources, ThirdParty
mkdir Source\CineSRTStream
mkdir Source\CineSRTStream\Public
mkdir Source\CineSRTStream\Private
mkdir ThirdParty\SRT
mkdir ThirdParty\SRT\include
mkdir ThirdParty\SRT\lib
mkdir ThirdParty\SRT\lib\Win64
```

**Step 4: 필요한 파일 복사**
```powershell
# SRT 헤더 파일 복사
xcopy C:\CineSRTProject\BuildTools\srt\srt\*.h C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\include\ /E
xcopy C:\CineSRTProject\BuildTools\srt\srt\srtcore\*.h C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\include\srtcore\ /E
xcopy C:\CineSRTProject\BuildTools\srt\srt\common\*.h C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\include\common\ /E

# version.h 파일 (빌드된 폴더에서)
copy C:\CineSRTProject\BuildTools\srt\srt\_build\version.h C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\include\

# 라이브러리 파일 복사
copy C:\CineSRTProject\BuildTools\srt\srt\_build\Release\srt_static.lib C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\lib\Win64\
copy C:\CineSRTProject\BuildTools\OpenSSL\install\lib\libssl.lib C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\lib\Win64\
copy C:\CineSRTProject\BuildTools\OpenSSL\install\lib\libcrypto.lib C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\lib\Win64\
copy C:\CineSRTProject\BuildTools\vcpkg\installed\x64-windows-static\lib\pthreadVC3.lib C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\lib\Win64\
```

#### Day 14: 플러그인 기본 파일 생성

**파일: `CineSRTStream.uplugin`**
```json
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0",
    "FriendlyName": "CineSRT Stream",
    "Description": "Real-time encrypted SRT streaming from Unreal Engine cameras",
    "Category": "Media",
    "CreatedBy": "Your Company",
    "CreatedByURL": "https://yourcompany.com",
    "DocsURL": "",
    "MarketplaceURL": "",
    "SupportURL": "",
    "CanContainContent": false,
    "IsBetaVersion": true,
    "IsExperimentalVersion": false,
    "Installed": false,
    "Modules": [
        {
            "Name": "CineSRTStream",
            "Type": "Runtime",
            "LoadingPhase": "Default",
            "PlatformAllowList": [
                "Win64"
            ]
        }
    ]
}
```

**파일: `Source\CineSRTStream\CineSRTStream.Build.cs`**
```csharp
using UnrealBuildTool;
using System.IO;

public class CineSRTStream : ModuleRules
{
    public CineSRTStream(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        // C++ 표준
        CppStandard = CppStandardVersion.Cpp17;
        
        // 기본 모듈 의존성
        PublicIncludePaths.AddRange(
            new string[] {
            }
        );
        
        PrivateIncludePaths.AddRange(
            new string[] {
            }
        );
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "UnrealEd", // 에디터 기능용
                "Slate",
                "SlateCore"
            }
        );
        
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "RenderCore",
                "RHI",
                "Sockets",
                "Networking",
                "Projects"
            }
        );
        
        // Windows 플랫폼 전용 설정
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // ThirdParty 경로
            string ThirdPartyPath = Path.Combine(ModuleDirectory, "../../ThirdParty");
            string SRTPath = Path.Combine(ThirdPartyPath, "SRT");
            
            // Include 경로 추가
            PublicIncludePaths.AddRange(new string[] {
                Path.Combine(SRTPath, "include"),
                Path.Combine(SRTPath, "include/srtcore"),
                Path.Combine(SRTPath, "include/common")
            });
            
            // 라이브러리 경로
            string LibPath = Path.Combine(SRTPath, "lib", "Win64");
            
            // 라이브러리 추가 (순서 중요!)
            PublicAdditionalLibraries.AddRange(new string[] {
                Path.Combine(LibPath, "srt_static.lib"),
                Path.Combine(LibPath, "libssl.lib"),
                Path.Combine(LibPath, "libcrypto.lib"),
                Path.Combine(LibPath, "pthreadVC3.lib")
            });
            
            // Windows 시스템 라이브러리
            PublicSystemLibraries.AddRange(new string[] {
                "ws2_32.lib",
                "Iphlpapi.lib",
                "Crypt32.lib",
                "Advapi32.lib",
                "User32.lib",
                "Gdi32.lib"
            });
            
            // 전처리기 정의
            PublicDefinitions.AddRange(new string[] {
                "_WIN32_WINNT=0x0601",
                "SRT_STATIC=1",
                "SRT_ENABLE_ENCRYPTION=1",
                "_CRT_SECURE_NO_WARNINGS",
                "WINDOWS_IGNORE_PACKING_MISMATCH"
            });
            
            // bEnableExceptions = true;
        }
    }
}
```

**파일: `Source\CineSRTStream\Public\CineSRTStream.h`**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCineSRTStream, Log, All);

class FCineSRTStreamModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    
    static inline FCineSRTStreamModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FCineSRTStreamModule>("CineSRTStream");
    }
    
    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("CineSRTStream");
    }
};
```

**파일: `Source\CineSRTStream\Private\CineSRTStream.cpp`**
```cpp
#include "CineSRTStream.h"

// SRT 헤더를 cpp 파일에서만 include
#ifdef _WIN32
    #pragma warning(push)
    #pragma warning(disable: 4005) // macro redefinition  
    #pragma warning(disable: 4996) // deprecated functions
#endif

#include "srt.h"

#ifdef _WIN32
    #pragma warning(pop)
#endif

#define LOCTEXT_NAMESPACE "FCineSRTStreamModule"

DEFINE_LOG_CATEGORY(LogCineSRTStream);

void FCineSRTStreamModule::StartupModule()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("CineSRTStream module starting up"));
    
    // SRT 라이브러리 초기화
    if (srt_startup() == 0)
    {
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT library initialized successfully"));
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT Version: %hs"), SRT_VERSION_STRING);
    }
    else
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to initialize SRT library"));
    }
}

void FCineSRTStreamModule::ShutdownModule()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("CineSRTStream module shutting down"));
    
    // SRT 라이브러리 정리
    srt_cleanup();
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FCineSRTStreamModule, CineSRTStream)
```

#### Day 15: 프로젝트 재생성 및 빌드

**Step 1: 프로젝트 파일 재생성**
1. 언리얼 에디터 닫기
2. `C:\CineSRTProject\UnrealProject\SRTStreamTest` 폴더로 이동
3. `SRTStreamTest.uproject` 파일 우클릭
4. "Generate Visual Studio project files" 선택

**Step 2: Visual Studio에서 빌드**
1. `SRTStreamTest.sln` 열기
2. Solution Configuration: "Development Editor"
3. Solution Platform: "Win64"
4. Build → Build Solution (Ctrl+Shift+B)

**🔍 빌드 확인:**
- 에러 없이 빌드 완료
- 언리얼 에디터 실행시 플러그인 로드됨
- Output Log에 "SRT library initialized successfully" 메시지

### 📅 Week 4: SRT 스트리밍 컴포넌트 구현

#### Day 16-17: 컴포넌트 기본 구조

**파일: `Source\CineSRTStream\Public\SRTStreamComponent.h`**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraComponent.h"
#include "SRTStreamComponent.generated.h"

UENUM(BlueprintType)
enum class ESRTStreamMode : uint8
{
    HD_1920x1080 UMETA(DisplayName = "HD 1920x1080"),
    FourK_3840x2160 UMETA(DisplayName = "4K 3840x2160")
};

UENUM(BlueprintType)
enum class ESRTConnectionState : uint8
{
    Disconnected UMETA(DisplayName = "Disconnected"),
    Connecting UMETA(DisplayName = "Connecting"),
    Connected UMETA(DisplayName = "Connected"),
    Error UMETA(DisplayName = "Error")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnSRTStateChanged,
    ESRTConnectionState, NewState,
    const FString&, Message
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnSRTStatsUpdated,
    float, Bitrate
);

UCLASS(ClassGroup=(Streaming), meta=(BlueprintSpawnableComponent))
class CINESRTSTREAM_API USRTStreamComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USRTStreamComponent();

    // === Configuration Properties ===
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Connection")
    FString StreamIP = "127.0.0.1";
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Connection", meta = (ClampMin = "1024", ClampMax = "65535"))
    int32 StreamPort = 9001;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Connection")
    FString StreamID = "UnrealCamera1";
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Video")
    ESRTStreamMode StreamMode = ESRTStreamMode::HD_1920x1080;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Video", meta = (ClampMin = "1", ClampMax = "60"))
    float StreamFPS = 30.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Encryption")
    bool bUseEncryption = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Encryption", meta = (EditCondition = "bUseEncryption"))
    FString EncryptionPassphrase = "YourSecurePassphrase";
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Encryption", meta = (EditCondition = "bUseEncryption"))
    int32 EncryptionKeyLength = 16; // 16=AES-128, 24=AES-192, 32=AES-256
    
    // === Status Properties ===
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    ESRTConnectionState ConnectionState = ESRTConnectionState::Disconnected;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    float CurrentBitrate = 0.0f;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    int32 FramesSent = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    int32 PacketsLost = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    float RTT = 0.0f; // Round Trip Time in ms
    
    // === Events ===
    UPROPERTY(BlueprintAssignable, Category = "SRT Events")
    FOnSRTStateChanged OnStateChanged;
    
    UPROPERTY(BlueprintAssignable, Category = "SRT Events")
    FOnSRTStatsUpdated OnStatsUpdated;
    
    // === Blueprint Methods ===
    UFUNCTION(BlueprintCallable, Category = "SRT Stream", meta = (CallInEditor = "true"))
    void StartStreaming();
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream", meta = (CallInEditor = "true"))
    void StopStreaming();
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    bool IsStreaming() const { return bIsStreaming; }
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    FString GetConnectionInfo() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // SRT Handle (void* to avoid exposing SRT types in header)
    void* SRTSocket;
    bool bIsStreaming;
    
    // Scene Capture
    UPROPERTY()
    USceneCaptureComponent2D* SceneCapture;
    
    UPROPERTY()
    UTextureRenderTarget2D* RenderTarget;
    
    // Frame timing
    float TimeSinceLastFrame;
    float FrameInterval;
    
    // Stats update timing
    float TimeSinceLastStatsUpdate;
    const float StatsUpdateInterval = 1.0f;
    
    // Internal methods
    bool InitializeSRT();
    void CleanupSRT();
    bool ConnectSRT();
    bool SetupSceneCapture();
    void CaptureAndSend();
    bool SendFrameData(const TArray<FColor>& PixelData);
    void UpdateStats();
    void SetConnectionState(ESRTConnectionState NewState, const FString& Message = "");
    
    // Thread safety
    FCriticalSection SocketMutex;
};
```

**파일: `Source\CineSRTStream\Private\SRTStreamComponent.cpp`**
```cpp
#include "SRTStreamComponent.h"
#include "CineSRTStream.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
#include "RHI.h"

// SRT includes
#ifdef _WIN32
    #pragma warning(push)
    #pragma warning(disable: 4005)
    #pragma warning(disable: 4996)
#endif

#include "srt.h"

#ifdef _WIN32
    #pragma warning(pop)
#endif

USRTStreamComponent::USRTStreamComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    
    SRTSocket = nullptr;
    bIsStreaming = false;
    TimeSinceLastFrame = 0.0f;
    TimeSinceLastStatsUpdate = 0.0f;
}

void USRTStreamComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // Calculate frame interval based on FPS
    FrameInterval = 1.0f / StreamFPS;
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRTStreamComponent initialized - Target FPS: %.2f"), StreamFPS);
}

void USRTStreamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopStreaming();
    Super::EndPlay(EndPlayReason);
}

void USRTStreamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    if (!bIsStreaming)
        return;
    
    // Update stats
    TimeSinceLastStatsUpdate += DeltaTime;
    if (TimeSinceLastStatsUpdate >= StatsUpdateInterval)
    {
        UpdateStats();
        TimeSinceLastStatsUpdate = 0.0f;
    }
    
    // Frame capture with FPS limit
    TimeSinceLastFrame += DeltaTime;
    if (TimeSinceLastFrame >= FrameInterval)
    {
        CaptureAndSend();
        TimeSinceLastFrame = 0.0f;
    }
}

void USRTStreamComponent::StartStreaming()
{
    if (bIsStreaming)
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("Already streaming"));
        return;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Starting SRT stream to %s:%d"), *StreamIP, StreamPort);
    
    SetConnectionState(ESRTConnectionState::Connecting, "Setting up capture...");
    
    // Setup scene capture
    if (!SetupSceneCapture())
    {
        SetConnectionState(ESRTConnectionState::Error, "Failed to setup scene capture");
        return;
    }
    
    // Initialize and connect SRT
    if (!InitializeSRT())
    {
        SetConnectionState(ESRTConnectionState::Error, "Failed to initialize SRT");
        return;
    }
    
    if (!ConnectSRT())
    {
        SetConnectionState(ESRTConnectionState::Error, "Failed to connect SRT");
        CleanupSRT();
        return;
    }
    
    bIsStreaming = true;
    SetConnectionState(ESRTConnectionState::Connected, "Streaming active");
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT streaming started successfully"));
}

void USRTStreamComponent::StopStreaming()
{
    if (!bIsStreaming)
        return;
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Stopping SRT stream"));
    
    bIsStreaming = false;
    CleanupSRT();
    
    // Cleanup scene capture
    if (SceneCapture)
    {
        SceneCapture->DestroyComponent();
        SceneCapture = nullptr;
    }
    
    if (RenderTarget)
    {
        RenderTarget = nullptr;
    }
    
    SetConnectionState(ESRTConnectionState::Disconnected, "Stream stopped");
}

bool USRTStreamComponent::InitializeSRT()
{
    // SRT should already be initialized in module startup
    // Just create the socket here
    
    FScopeLock Lock(&SocketMutex);
    
    SRTSOCKET sock = srt_create_socket();
    if (sock == SRT_INVALID_SOCK)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create SRT socket"));
        return false;
    }
    
    // Set socket options
    int yes = 1;
    int live_mode = SRTT_LIVE;
    srt_setsockopt(sock, 0, SRTO_SENDER, &yes, sizeof(yes));
    srt_setsockopt(sock, 0, SRTO_TRANSTYPE, &live_mode, sizeof(live_mode));
    
    // Set stream ID
    if (!StreamID.IsEmpty())
    {
        srt_setsockopt(sock, 0, SRTO_STREAMID, TCHAR_TO_ANSI(*StreamID), StreamID.Len());
    }
    
    // Encryption settings
    if (bUseEncryption)
    {
        // Set key length (16=AES-128, 24=AES-192, 32=AES-256)
        srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &EncryptionKeyLength, sizeof(EncryptionKeyLength));
        
        // Set passphrase
        if (!EncryptionPassphrase.IsEmpty())
        {
            srt_setsockopt(sock, 0, SRTO_PASSPHRASE, 
                TCHAR_TO_ANSI(*EncryptionPassphrase), 
                EncryptionPassphrase.Len());
            
            UE_LOG(LogCineSRTStream, Log, TEXT("Encryption enabled with %d-bit AES"), EncryptionKeyLength * 8);
        }
    }
    
    // Performance options
    int mss = 1500;
    srt_setsockopt(sock, 0, SRTO_MSS, &mss, sizeof(mss));
    
    int send_buffer = 12058624; // 12MB
    srt_setsockopt(sock, 0, SRTO_SNDBUF, &send_buffer, sizeof(send_buffer));
    
    int latency = 120; // 120ms
    srt_setsockopt(sock, 0, SRTO_LATENCY, &latency, sizeof(latency));
    
    SRTSocket = (void*)sock;
    return true;
}

bool USRTStreamComponent::ConnectSRT()
{
    FScopeLock Lock(&SocketMutex);
    
    if (!SRTSocket)
        return false;
    
    SRTSOCKET sock = (SRTSOCKET)SRTSocket;
    
    // Setup target address
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(StreamPort);
    
    // Convert IP string to address
    if (inet_pton(AF_INET, TCHAR_TO_ANSI(*StreamIP), &sa.sin_addr) != 1)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Invalid IP address: %s"), *StreamIP);
        return false;
    }
    
    // Connect
    if (srt_connect(sock, (sockaddr*)&sa, sizeof(sa)) == SRT_ERROR)
    {
        FString ErrorStr = ANSI_TO_TCHAR(srt_getlasterror_str());
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to connect SRT: %s"), *ErrorStr);
        
        // Provide helpful error message
        if (ErrorStr.Contains("Connection refused"))
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Make sure OBS/receiver is listening on %s:%d"), *StreamIP, StreamPort);
            if (bUseEncryption)
            {
                UE_LOG(LogCineSRTStream, Error, TEXT("Encryption passphrase: %s"), *EncryptionPassphrase);
            }
        }
        
        return false;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT connected successfully to %s:%d"), *StreamIP, StreamPort);
    return true;
}

void USRTStreamComponent::CleanupSRT()
{
    FScopeLock Lock(&SocketMutex);
    
    if (SRTSocket)
    {
        SRTSOCKET sock = (SRTSOCKET)SRTSocket;
        srt_close(sock);
        SRTSocket = nullptr;
    }
}

bool USRTStreamComponent::SetupSceneCapture()
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("No owner actor found"));
        return false;
    }
    
    // Find camera component
    UCameraComponent* Camera = Owner->FindComponentByClass<UCameraComponent>();
    if (!Camera)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("No camera component found on actor"));
        return false;
    }
    
    // Create SceneCapture2D
    SceneCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("SRTSceneCapture"));
    SceneCapture->RegisterComponent();
    SceneCapture->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    
    // Determine resolution
    int32 Width = 1920;
    int32 Height = 1080;
    
    if (StreamMode == ESRTStreamMode::FourK_3840x2160)
    {
        Width = 3840;
        Height = 2160;
    }
    
    // Create RenderTarget
    RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("SRTRenderTarget"));
    RenderTarget->InitAutoFormat(Width, Height);
    RenderTarget->UpdateResourceImmediate(true);
    
    // Configure SceneCapture
    SceneCapture->TextureTarget = RenderTarget;
    SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    SceneCapture->bCaptureEveryFrame = false;
    SceneCapture->bCaptureOnMovement = false;
    SceneCapture->bAlwaysPersistRenderingState = true;
    
    // Copy camera settings
    SceneCapture->FOVAngle = Camera->FieldOfView;
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Scene capture setup complete: %dx%d"), Width, Height);
    return true;
}

void USRTStreamComponent::CaptureAndSend()
{
    if (!SceneCapture || !RenderTarget || !SRTSocket)
        return;
    
    // Capture the scene
    SceneCapture->CaptureScene();
    
    // Read pixels from render target
    FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!RenderTargetResource)
        return;
    
    TArray<FColor> PixelData;
    if (!RenderTargetResource->ReadPixels(PixelData))
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to read pixels from render target"));
        return;
    }
    
    // Send the frame data
    if (SendFrameData(PixelData))
    {
        FramesSent++;
    }
}

bool USRTStreamComponent::SendFrameData(const TArray<FColor>& PixelData)
{
    FScopeLock Lock(&SocketMutex);
    
    if (!SRTSocket)
        return false;
    
    SRTSOCKET sock = (SRTSOCKET)SRTSocket;
    
    // For Phase 2, we'll send raw pixel data with a simple header
    // In Phase 3, this will be replaced with H.264 encoded data
    
    struct FrameHeader
    {
        uint32 Magic = 0x53525446; // 'SRTF'
        uint32 Width;
        uint32 Height;
        uint32 PixelFormat = 1; // 1 = BGRA8
        uint32 DataSize;
        uint64 Timestamp;
    };
    
    FrameHeader Header;
    Header.Width = RenderTarget->SizeX;
    Header.Height = RenderTarget->SizeY;
    Header.DataSize = PixelData.Num() * sizeof(FColor);
    Header.Timestamp = FPlatformTime::Cycles64();
    
    // Send header
    int sent = srt_send(sock, (char*)&Header, sizeof(Header));
    if (sent == SRT_ERROR)
    {
        FString Error = ANSI_TO_TCHAR(srt_getlasterror_str());
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send frame header: %s"), *Error);
        return false;
    }
    
    // Send pixel data in chunks
    const int ChunkSize = 1316; // SRT recommended payload size
    const char* DataPtr = (const char*)PixelData.GetData();
    int TotalSize = Header.DataSize;
    int BytesSent = 0;
    
    while (BytesSent < TotalSize)
    {
        int ToSend = FMath::Min(ChunkSize, TotalSize - BytesSent);
        sent = srt_send(sock, DataPtr + BytesSent, ToSend);
        
        if (sent == SRT_ERROR)
        {
            FString Error = ANSI_TO_TCHAR(srt_getlasterror_str());
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send frame data: %s"), *Error);
            return false;
        }
        
        BytesSent += sent;
    }
    
    return true;
}

void USRTStreamComponent::UpdateStats()
{
    FScopeLock Lock(&SocketMutex);
    
    if (!SRTSocket)
        return;
    
    SRTSOCKET sock = (SRTSOCKET)SRTSocket;
    
    // Get SRT statistics
    SRT_TRACEBSTATS stats;
    if (srt_bstats(sock, &stats, 1) == 0) // 1 = clear stats after reading
    {
        // Calculate bitrate (Mbps)
        CurrentBitrate = (stats.mbpsSendRate);
        
        // Update other stats
        PacketsLost = stats.pktSndLossTotal;
        RTT = stats.msRTT;
        
        // Broadcast stats update
        if (OnStatsUpdated.IsBound())
        {
            OnStatsUpdated.Broadcast(CurrentBitrate);
        }
    }
}

void USRTStreamComponent::SetConnectionState(ESRTConnectionState NewState, const FString& Message)
{
    if (ConnectionState != NewState)
    {
        ConnectionState = NewState;
        
        FString StateStr;
        switch (NewState)
        {
        case ESRTConnectionState::Disconnected:
            StateStr = TEXT("Disconnected");
            break;
        case ESRTConnectionState::Connecting:
            StateStr = TEXT("Connecting");
            break;
        case ESRTConnectionState::Connected:
            StateStr = TEXT("Connected");
            break;
        case ESRTConnectionState::Error:
            StateStr = TEXT("Error");
            break;
        }
        
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT State Changed: %s - %s"), *StateStr, *Message);
        
        if (OnStateChanged.IsBound())
        {
            OnStateChanged.Broadcast(NewState, Message);
        }
    }
}

FString USRTStreamComponent::GetConnectionInfo() const
{
    if (!bIsStreaming)
        return TEXT("Not streaming");
    
    return FString::Printf(TEXT("Streaming to %s:%d - %.2f Mbps - %d frames sent"),
        *StreamIP, StreamPort, CurrentBitrate, FramesSent);
}
```

#### Day 18: 수신 테스트 프로그램

**파일: `C:\CineSRTProject\TestPrograms\receiver\receiver.cpp`**
```cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
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
};

int main()
{
    std::cout << "=== SRT Frame Receiver ===" << std::endl;
    
    srt_startup();
    
    SRTSOCKET sock = srt_create_socket();
    
    // Enable encryption
    int pbkeylen = 16;
    srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen));
    
    const char* passphrase = "YourSecurePassphrase";
    srt_setsockopt(sock, 0, SRTO_PASSPHRASE, passphrase, strlen(passphrase));
    
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
    std::cout << "Waiting for Unreal Engine connection..." << std::endl;
    
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
    
    // Receive frames
    int frame_count = 0;
    while (frame_count < 10) // Receive 10 frames
    {
        // Receive header
        FrameHeader header;
        int received = srt_recv(client, (char*)&header, sizeof(header));
        
        if (received != sizeof(header))
        {
            std::cout << "Failed to receive header" << std::endl;
            break;
        }
        
        if (header.Magic != 0x53525446) // 'SRTF'
        {
            std::cout << "Invalid frame magic" << std::endl;
            continue;
        }
        
        std::cout << "Frame " << frame_count << ": " 
                  << header.Width << "x" << header.Height 
                  << " (" << header.DataSize << " bytes)" << std::endl;
        
        // Receive pixel data
        std::vector<uint8_t> pixel_data(header.DataSize);
        int total_received = 0;
        
        while (total_received < header.DataSize)
        {
            int to_receive = std::min(1316, (int)(header.DataSize - total_received));
            int recv = srt_recv(client, (char*)pixel_data.data() + total_received, to_receive);
            
            if (recv == SRT_ERROR)
            {
                std::cout << "Receive error: " << srt_getlasterror_str() << std::endl;
                break;
            }
            
            total_received += recv;
        }
        
        std::cout << "✅ Received complete frame" << std::endl;
        
        // Save first frame as verification
        if (frame_count == 0)
        {
            std::ofstream file("first_frame.raw", std::ios::binary);
            file.write((char*)pixel_data.data(), pixel_data.size());
            file.close();
            std::cout << "First frame saved to first_frame.raw" << std::endl;
        }
        
        frame_count++;
    }
    
    srt_close(client);
    srt_close(sock);
    srt_cleanup();
    
    std::cout << "Test completed. Received " << frame_count << " frames." << std::endl;
    return 0;
}
```

#### Day 19-20: 언리얼 에디터 테스트

**Step 1: 프로젝트 빌드 및 실행**
1. Visual Studio에서 Development Editor 빌드
2. 언리얼 에디터 실행

**Step 2: 테스트 씬 설정**
1. 새 레벨 생성 또는 기본 레벨 사용
2. **CineCameraActor** 배치 (일반 Camera Actor 아님)
3. 카메라 위치 조정 (씬이 잘 보이도록)

**Step 3: SRT 컴포넌트 추가**
1. CineCameraActor 선택
2. Details 패널 → Add Component
3. "SRT Stream" 검색하여 추가

**Step 4: 컴포넌트 설정**
- Stream IP: 127.0.0.1
- Stream Port: 9001
- Stream ID: TestCamera1
- Use Encryption: ✓
- Encryption Passphrase: YourSecurePassphrase

**Step 5: 스트리밍 테스트**
1. receiver.exe 먼저 실행
2. 언리얼 에디터에서 컴포넌트 선택
3. Details 패널에서 "Start Streaming" 버튼 클릭
4. receiver.exe에서 프레임 수신 확인

**✅ Phase 2 성공 기준:**
1. 플러그인이 에디터에 정상 로드
2. 컴포넌트를 카메라에 추가 가능
3. SRT 암호화 연결 성공
4. receiver.exe에서 프레임 데이터 수신
5. 10개 프레임 정상 수신

---

## 🎬 Phase 3: H.264/MPEG-TS 인코딩 (4-8주)

### ⚠️ 가장 어려운 단계 - 실제 비디오 스트리밍

### 📅 Week 5-6: FFmpeg 통합 준비

#### Day 21-22: FFmpeg 라이브러리 준비

**Option 1: 미리 빌드된 라이브러리 (권장)**
```powershell
cd C:\CineSRTProject\BuildTools

# FFmpeg Windows 빌드 다운로드
# https://www.gyan.dev/ffmpeg/builds/
# ffmpeg-6.0-full_build-shared.7z 다운로드

# 압축 해제
# BuildTools\ffmpeg-6.0-full_build-shared\

# 필요한 파일들:
# - bin\*.dll (avcodec, avformat, avutil, swscale 등)
# - include\* (헤더 파일들)
# - lib\*.lib (링크 라이브러리)
```

**Option 2: 직접 빌드 (고급)**
- MSYS2 환경 필요
- 시간이 매우 오래 걸림 (4-6시간)

#### Day 23-24: 최소 H.264 인코더 테스트

**파일: `C:\CineSRTProject\TestPrograms\test_h264\test_h264.cpp`**
```cpp
// 간단한 H.264 인코딩 테스트
// FFmpeg를 사용하여 테스트 패턴을 H.264로 인코딩

#include <iostream>
#include <vector>
#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

// 테스트 패턴 생성
void generate_test_pattern(uint8_t* data, int width, int height, int frame_index)
{
    // 간단한 움직이는 패턴
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int offset = (y * width + x) * 4; // BGRA
            data[offset + 0] = (x + frame_index) % 256; // B
            data[offset + 1] = (y + frame_index) % 256; // G  
            data[offset + 2] = ((x + y + frame_index) / 2) % 256; // R
            data[offset + 3] = 255; // A
        }
    }
}

int main()
{
    std::cout << "=== H.264 Encoding Test ===" << std::endl;
    
    // FFmpeg 초기화는 더 이상 필요 없음 (deprecated)
    
    // H.264 코덱 찾기
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cout << "H.264 codec not found" << std::endl;
        return 1;
    }
    
    // 코덱 컨텍스트 생성
    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) {
        std::cout << "Could not allocate codec context" << std::endl;
        return 1;
    }
    
    // 인코더 설정
    c->bit_rate = 5000000; // 5 Mbps
    c->width = 1920;
    c->height = 1080;
    c->time_base = {1, 30}; // 30 fps
    c->framerate = {30, 1};
    c->gop_size = 30; // I-frame every second
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // x264 프리셋
    av_opt_set(c->priv_data, "preset", "ultrafast", 0);
    av_opt_set(c->priv_data, "tune", "zerolatency", 0);
    
    // 코덱 열기
    if (avcodec_open2(c, codec, NULL) < 0) {
        std::cout << "Could not open codec" << std::endl;
        return 1;
    }
    
    std::cout << "✅ H.264 encoder initialized" << std::endl;
    
    // 프레임 할당
    AVFrame* frame = av_frame_alloc();
    frame->format = c->pix_fmt;
    frame->width = c->width;
    frame->height = c->height;
    av_frame_get_buffer(frame, 0);
    
    // 색상 변환 컨텍스트
    SwsContext* sws_ctx = sws_getContext(
        c->width, c->height, AV_PIX_FMT_BGRA,
        c->width, c->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL
    );
    
    // 테스트: 10개 프레임 인코딩
    std::vector<uint8_t> bgra_buffer(c->width * c->height * 4);
    std::ofstream output("test.h264", std::ios::binary);
    
    for (int i = 0; i < 10; i++) {
        // 테스트 패턴 생성
        generate_test_pattern(bgra_buffer.data(), c->width, c->height, i);
        
        // BGRA → YUV420P 변환
        uint8_t* src_data[4] = {bgra_buffer.data(), NULL, NULL, NULL};
        int src_linesize[4] = {c->width * 4, 0, 0, 0};
        
        sws_scale(sws_ctx, src_data, src_linesize, 0, c->height,
                  frame->data, frame->linesize);
        
        frame->pts = i;
        
        // 인코딩
        AVPacket* pkt = av_packet_alloc();
        int ret = avcodec_send_frame(c, frame);
        if (ret < 0) {
            std::cout << "Error sending frame" << std::endl;
            continue;
        }
        
        while (ret >= 0) {
            ret = avcodec_receive_packet(c, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0) {
                std::cout << "Error encoding frame" << std::endl;
                break;
            }
            
            // H.264 데이터 저장
            output.write((char*)pkt->data, pkt->size);
            std::cout << "Frame " << i << " encoded: " << pkt->size << " bytes" << std::endl;
            
            av_packet_unref(pkt);
        }
        
        av_packet_free(&pkt);
    }
    
    output.close();
    
    // 정리
    avcodec_free_context(&c);
    av_frame_free(&frame);
    sws_freeContext(sws_ctx);
    
    std::cout << "✅ Test completed. Output: test.h264" << std::endl;
    std::cout << "You can play it with: ffplay test.h264" << std::endl;
    
    return 0;
}
```

### 📅 Week 7-8: MPEG-TS 멀티플렉싱

#### Day 25-27: 기본 MPEG-TS 구현

**파일: `Source\CineSRTStream\Public\SRTVideoEncoder.h`**
```cpp
#pragma once

#include "CoreMinimal.h"

// Forward declarations to avoid including FFmpeg headers
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct AVFormatContext;
struct AVStream;

class FSRTVideoEncoder
{
public:
    FSRTVideoEncoder();
    ~FSRTVideoEncoder();
    
    bool Initialize(int32 Width, int32 Height, int32 FPS, int32 BitrateKbps);
    bool EncodeFrame(const TArray<FColor>& PixelData, TArray<uint8>& OutEncodedData);
    void Shutdown();
    
    bool IsInitialized() const { return bIsInitialized; }
    
private:
    // Video encoding
    AVCodecContext* CodecContext;
    AVFrame* Frame;
    SwsContext* SwsContext;
    
    // MPEG-TS muxing
    AVFormatContext* FormatContext;
    AVStream* VideoStream;
    uint8_t* TSBuffer;
    const int TSBufferSize = 1024 * 1024; // 1MB buffer
    
    // State
    bool bIsInitialized;
    int32 FrameWidth;
    int32 FrameHeight;
    int32 FrameRate;
    int64 FrameCount;
    
    // Internal methods
    bool SetupEncoder();
    bool SetupMuxer();
    bool ConvertPixelData(const TArray<FColor>& PixelData);
    void CleanupFFmpeg();
};
```

**파일: `Source\CineSRTStream\Private\SRTVideoEncoder.cpp`**
```cpp
#include "SRTVideoEncoder.h"
#include "CineSRTStream.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// Custom IO callbacks for MPEG-TS output
struct TSOutputContext {
    TArray<uint8>* OutputData;
};

static int write_packet(void* opaque, uint8_t* buf, int buf_size)
{
    TSOutputContext* ctx = (TSOutputContext*)opaque;
    if (ctx && ctx->OutputData) {
        int32 OldSize = ctx->OutputData->Num();
        ctx->OutputData->SetNum(OldSize + buf_size);
        FMemory::Memcpy(ctx->OutputData->GetData() + OldSize, buf, buf_size);
    }
    return buf_size;
}

FSRTVideoEncoder::FSRTVideoEncoder()
    : CodecContext(nullptr)
    , Frame(nullptr)
    , SwsContext(nullptr)
    , FormatContext(nullptr)
    , VideoStream(nullptr)
    , TSBuffer(nullptr)
    , bIsInitialized(false)
    , FrameCount(0)
{
}

FSRTVideoEncoder::~FSRTVideoEncoder()
{
    Shutdown();
}

bool FSRTVideoEncoder::Initialize(int32 Width, int32 Height, int32 FPS, int32 BitrateKbps)
{
    FrameWidth = Width;
    FrameHeight = Height;
    FrameRate = FPS;
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Initializing H.264 encoder: %dx%d @ %d fps, %d kbps"),
        Width, Height, FPS, BitrateKbps);
    
    if (!SetupEncoder())
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to setup H.264 encoder"));
        return false;
    }
    
    if (!SetupMuxer())
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to setup MPEG-TS muxer"));
        CleanupFFmpeg();
        return false;
    }
    
    bIsInitialized = true;
    return true;
}

bool FSRTVideoEncoder::SetupEncoder()
{
    // Find H.264 encoder
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("H.264 codec not found"));
        return false;
    }
    
    // Allocate codec context
    CodecContext = avcodec_alloc_context3(codec);
    if (!CodecContext)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate codec context"));
        return false;
    }
    
    // Configure encoder
    CodecContext->bit_rate = BitrateKbps * 1000;
    CodecContext->width = FrameWidth;
    CodecContext->height = FrameHeight;
    CodecContext->time_base = {1, FrameRate};
    CodecContext->framerate = {FrameRate, 1};
    CodecContext->gop_size = FrameRate; // I-frame every second
    CodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    CodecContext->max_b_frames = 0; // No B-frames for low latency
    
    // x264 specific options for low latency
    av_opt_set(CodecContext->priv_data, "preset", "ultrafast", 0);
    av_opt_set(CodecContext->priv_data, "tune", "zerolatency", 0);
    av_opt_set(CodecContext->priv_data, "x264opts", "keyint=30:min-keyint=30", 0);
    
    // Open codec
    if (avcodec_open2(CodecContext, codec, nullptr) < 0)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to open H.264 codec"));
        avcodec_free_context(&CodecContext);
        return false;
    }
    
    // Allocate frame
    Frame = av_frame_alloc();
    if (!Frame)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate frame"));
        return false;
    }
    
    Frame->format = CodecContext->pix_fmt;
    Frame->width = CodecContext->width;
    Frame->height = CodecContext->height;
    
    if (av_frame_get_buffer(Frame, 0) < 0)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate frame buffer"));
        return false;
    }
    
    // Setup color space converter (BGRA → YUV420P)
    SwsContext = sws_getContext(
        FrameWidth, FrameHeight, AV_PIX_FMT_BGRA,
        FrameWidth, FrameHeight, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!SwsContext)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create SWS context"));
        return false;
    }
    
    return true;
}

bool FSRTVideoEncoder::SetupMuxer()
{
    // Allocate TS buffer
    TSBuffer = (uint8_t*)av_malloc(TSBufferSize);
    if (!TSBuffer)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate TS buffer"));
        return false;
    }
    
    // Allocate format context for MPEG-TS
    avformat_alloc_output_context2(&FormatContext, nullptr, "mpegts", nullptr);
    if (!FormatContext)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate format context"));
        return false;
    }
    
    // Create video stream
    VideoStream = avformat_new_stream(FormatContext, nullptr);
    if (!VideoStream)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create video stream"));
        return false;
    }
    
    // Copy codec parameters
    VideoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    VideoStream->codecpar->codec_id = AV_CODEC_ID_H264;
    VideoStream->codecpar->bit_rate = CodecContext->bit_rate;
    VideoStream->codecpar->width = CodecContext->width;
    VideoStream->codecpar->height = CodecContext->height;
    VideoStream->codecpar->format = CodecContext->pix_fmt;
    
    VideoStream->time_base = CodecContext->time_base;
    
    // Custom IO for memory output
    AVIOContext* avio_ctx = avio_alloc_context(
        TSBuffer, TSBufferSize,
        1, // write flag
        nullptr, // opaque
        nullptr, // read_packet
        write_packet, // write_packet  
        nullptr  // seek
    );
    
    if (!avio_ctx)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate AVIO context"));
        return false;
    }
    
    FormatContext->pb = avio_ctx;
    FormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
    
    return true;
}

bool FSRTVideoEncoder::EncodeFrame(const TArray<FColor>& PixelData, TArray<uint8>& OutEncodedData)
{
    if (!bIsInitialized)
        return false;
    
    // Convert pixel data
    if (!ConvertPixelData(PixelData))
        return false;
    
    // Set frame PTS
    Frame->pts = FrameCount++;
    
    // Send frame to encoder
    int ret = avcodec_send_frame(CodecContext, Frame);
    if (ret < 0)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send frame to encoder"));
        return false;
    }
    
    // Setup output context for this frame
    TSOutputContext output_ctx;
    output_ctx.OutputData = &OutEncodedData;
    FormatContext->pb->opaque = &output_ctx;
    
    // Write header on first frame
    if (FrameCount == 1)
    {
        ret = avformat_write_header(FormatContext, nullptr);
        if (ret < 0)
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to write MPEG-TS header"));
            return false;
        }
    }
    
    // Receive encoded packets
    AVPacket* pkt = av_packet_alloc();
    bool success = false;
    
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(CodecContext, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0)
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Error receiving packet from encoder"));
            break;
        }
        
        // Scale timestamps
        av_packet_rescale_ts(pkt, CodecContext->time_base, VideoStream->time_base);
        pkt->stream_index = VideoStream->index;
        
        // Write packet to MPEG-TS
        ret = av_interleaved_write_frame(FormatContext, pkt);
        if (ret < 0)
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to write frame to MPEG-TS"));
        }
        else
        {
            success = true;
        }
        
        av_packet_unref(pkt);
    }
    
    av_packet_free(&pkt);
    
    // Flush muxer
    av_write_frame(FormatContext, nullptr);
    
    return success && OutEncodedData.Num() > 0;
}

bool FSRTVideoEncoder::ConvertPixelData(const TArray<FColor>& PixelData)
{
    if (PixelData.Num() != FrameWidth * FrameHeight)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Invalid pixel data size"));
        return false;
    }
    
    // Setup source data pointers
    const uint8* src_data[4] = {(const uint8*)PixelData.GetData(), nullptr, nullptr, nullptr};
    int src_linesize[4] = {FrameWidth * 4, 0, 0, 0};
    
    // Convert BGRA to YUV420P
    int result = sws_scale(
        SwsContext,
        src_data, src_linesize, 0, FrameHeight,
        Frame->data, Frame->linesize
    );
    
    return result == FrameHeight;
}

void FSRTVideoEncoder::CleanupFFmpeg()
{
    if (FormatContext)
    {
        if (FormatContext->pb)
        {
            av_free(FormatContext->pb->buffer);
            avio_context_free(&FormatContext->pb);
        }
        avformat_free_context(FormatContext);
        FormatContext = nullptr;
    }
    
    if (SwsContext)
    {
        sws_freeContext(SwsContext);
        SwsContext = nullptr;
    }
    
    if (Frame)
    {
        av_frame_free(&Frame);
    }
    
    if (CodecContext)
    {
        avcodec_free_context(&CodecContext);
    }
}

void FSRTVideoEncoder::Shutdown()
{
    if (bIsInitialized)
    {
        CleanupFFmpeg();
        bIsInitialized = false;
    }
}
```

#### Day 28-30: 컴포넌트 업데이트 및 최종 테스트

**SRTStreamComponent.cpp 업데이트 (SendFrameData 메서드)**
```cpp
// 기존 raw 데이터 대신 인코딩된 비디오 전송
bool USRTStreamComponent::SendFrameData(const TArray<FColor>& PixelData)
{
    // Phase 3: H.264 인코딩 추가
    if (!VideoEncoder)
    {
        VideoEncoder = MakeUnique<FSRTVideoEncoder>();
        
        int32 Width = RenderTarget->SizeX;
        int32 Height = RenderTarget->SizeY;
        int32 BitrateKbps = 5000; // 5 Mbps
        
        if (!VideoEncoder->Initialize(Width, Height, (int32)StreamFPS, BitrateKbps))
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to initialize video encoder"));
            VideoEncoder.Reset();
            return false;
        }
    }
    
    // Encode frame
    TArray<uint8> EncodedData;
    if (!VideoEncoder->EncodeFrame(PixelData, EncodedData))
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to encode frame"));
        return false;
    }
    
    // Send encoded MPEG-TS data via SRT
    FScopeLock Lock(&SocketMutex);
    
    if (!SRTSocket)
        return false;
    
    SRTSOCKET sock = (SRTSOCKET)SRTSocket;
    
    // Send in chunks (MPEG-TS packets are 188 bytes)
    const uint8* DataPtr = EncodedData.GetData();
    int32 TotalSize = EncodedData.Num();
    int32 Sent = 0;
    
    while (Sent < TotalSize)
    {
        int32 ChunkSize = FMath::Min(1316, TotalSize - Sent); // 7 TS packets
        int result = srt_send(sock, (char*)(DataPtr + Sent), ChunkSize);
        
        if (result == SRT_ERROR)
        {
            FString Error = ANSI_TO_TCHAR(srt_getlasterror_str());
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send video data: %s"), *Error);
            return false;
        }
        
        Sent += result;
    }
    
    return true;
}
```

### 🎯 Phase 3 최종 테스트

**OBS 설정:**
1. Sources → Add → Media Source
2. 설정:
   - Local File: 체크 해제
   - Input: `srt://127.0.0.1:9001?mode=listener&passphrase=YourSecurePassphrase`
   - Input Format: mpegts (자동 감지됨)

**테스트 순서:**
1. OBS 실행 및 SRT 소스 추가
2. 언리얼 에디터에서 Start Streaming
3. OBS에서 실시간 영상 확인

**디버깅 도구:**
```bash
# VLC로 직접 수신 테스트
vlc srt://127.0.0.1:9001?mode=listener&passphrase=YourSecurePassphrase

# FFmpeg로 스트림 정보 확인
ffmpeg -i "srt://127.0.0.1:9001?mode=listener&passphrase=YourSecurePassphrase" -f null -
```

**✅ Phase 3 성공 = MVP 완성!**
- 언리얼 카메라 영상이 OBS에 실시간 표시
- 30fps 안정적 스트리밍
- 암호화된 SRT 전송

---

## 🚀 Phase 4: 멀티스트림 확장 (선택사항, 2-3주)

Phase 3가 완벽히 성공한 후에만 진행

- 4개의 카메라에 각각 컴포넌트 추가
- 포트 9001-9004 사용
- OBS에서 4개 소스로 수신

---

## 📋 최종 체크리스트

### Phase 1 (OpenSSL + SRT)
- [ ] OpenSSL 빌드 성공
- [ ] SRT 암호화 빌드 성공
- [ ] 테스트 프로그램 동작

### Phase 2 (언리얼 통합)
- [ ] 플러그인 컴파일 성공
- [ ] 컴포넌트 추가 가능
- [ ] 데이터 전송 확인

### Phase 3 (비디오 인코딩)
- [ ] H.264 인코딩 성공
- [ ] MPEG-TS 패키징
- [ ] **OBS에서 영상 표시!**

### Phase 4 (멀티스트림)
- [ ] 4개 카메라 동시 운영
- [ ] 성능 최적화

---

## 🎉 축하합니다!

이 가이드를 끝까지 따라오셨다면, Windows에서 가장 어려운 스트리밍 프로젝트 중 하나를 완성하신 것입니다!

**얻은 것들:**
- Windows SRT + OpenSSL 전문 지식
- 언리얼 엔진 플러그인 개발 경험
- 실시간 비디오 인코딩 기술
- 차별화된 기술 포트폴리오

**다음 단계:**
- UI/UX 개선
- 성능 최적화
- 상용화 준비

성공하셨다면 공유해주세요! 🚀