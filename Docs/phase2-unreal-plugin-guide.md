# Phase 2: 언리얼 엔진 5.5 플러그인 통합 완전 가이드

## 🎯 Phase 2 목표
언리얼 엔진 5.5에서 SRT 스트리밍 플러그인을 생성하고, 카메라 영상을 캡처하여 암호화된 SRT로 전송하는 기능 구현

---

## 📋 사전 준비 체크리스트

### 필수 확인 사항:
- [ ] Phase 1 완료 (모든 라이브러리 빌드 성공)
- [ ] 언리얼 엔진 5.5 설치
- [ ] Visual Studio 2022 17.8 이상
- [ ] 다음 파일들이 준비되었는지 확인:
  ```
  BuildTools/srt/srt/_build/Release/srt_static.lib
  BuildTools/OpenSSL/install/lib/libssl.lib
  BuildTools/OpenSSL/install/lib/libcrypto.lib
  BuildTools/vcpkg/installed/x64-windows-static/lib/pthreadVC3.lib
  ```

---

## 🚀 Step 1: 언리얼 프로젝트 생성

### 1.1 프로젝트 생성
1. Epic Games Launcher → Unreal Engine 5.5 실행
2. 새 프로젝트 생성:
   - 템플릿: **Games** → **Third Person**
   - 프로젝트 기본값:
     - Blueprint/C++: **C++**
     - 타겟 플랫폼: **Desktop**
     - 품질 프리셋: **Maximum**
     - 스타터 콘텐츠: **포함 안 함**
     - 레이트레이싱: **비활성화**
     - 플랫폼 및 서비스: **모두 해제**
   - 프로젝트 이름: `SRTStreamTest`
   - 폴더: `C:\CineSRTProject\UnrealProject`
   - **프로젝트 생성**

### 1.2 초기 빌드 확인
- Visual Studio가 자동으로 열림
- **Development Editor** | **Win64** 설정 확인
- Ctrl+B로 빌드 (약 5-10분)
- 빌드 성공 후 언리얼 에디터 종료

---

## 🔧 Step 2: 플러그인 구조 생성

### 2.1 폴더 구조 생성 (PowerShell)
```powershell
# 프로젝트 폴더로 이동
cd C:\CineSRTProject\UnrealProject\SRTStreamTest

# 플러그인 폴더 생성
New-Item -ItemType Directory -Force -Path @(
    "Plugins\CineSRTStream",
    "Plugins\CineSRTStream\Source",
    "Plugins\CineSRTStream\Source\CineSRTStream",
    "Plugins\CineSRTStream\Source\CineSRTStream\Public",
    "Plugins\CineSRTStream\Source\CineSRTStream\Private",
    "Plugins\CineSRTStream\Resources",
    "Plugins\CineSRTStream\ThirdParty",
    "Plugins\CineSRTStream\ThirdParty\SRT",
    "Plugins\CineSRTStream\ThirdParty\SRT\include",
    "Plugins\CineSRTStream\ThirdParty\SRT\include\srtcore",
    "Plugins\CineSRTStream\ThirdParty\SRT\include\common",
    "Plugins\CineSRTStream\ThirdParty\SRT\include\win",
    "Plugins\CineSRTStream\ThirdParty\SRT\lib",
    "Plugins\CineSRTStream\ThirdParty\SRT\lib\Win64"
)
```

### 2.2 라이브러리 파일 복사
```powershell
# 라이브러리 파일 복사
$targetLib = "C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\lib\Win64"

Copy-Item "C:\CineSRTProject\BuildTools\srt\srt\_build\Release\srt_static.lib" -Destination $targetLib
Copy-Item "C:\CineSRTProject\BuildTools\OpenSSL\install\lib\libssl.lib" -Destination $targetLib
Copy-Item "C:\CineSRTProject\BuildTools\OpenSSL\install\lib\libcrypto.lib" -Destination $targetLib
Copy-Item "C:\CineSRTProject\BuildTools\vcpkg\installed\x64-windows-static\lib\pthreadVC3.lib" -Destination $targetLib

# 확인
Get-ChildItem $targetLib
```

### 2.3 헤더 파일 복사
```powershell
# SRT 헤더 복사
$srcSRT = "C:\CineSRTProject\BuildTools\srt\srt"
$dstSRT = "C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\include"

# 메인 헤더
Copy-Item "$srcSRT\srt.h" -Destination $dstSRT
Copy-Item "$srcSRT\logging_api.h" -Destination $dstSRT
Copy-Item "$srcSRT\platform_sys.h" -Destination $dstSRT
Copy-Item "$srcSRT\srt4udt.h" -Destination $dstSRT

# srtcore 헤더
Copy-Item "$srcSRT\srtcore\*.h" -Destination "$dstSRT\srtcore" -Force

# common 헤더
Copy-Item "$srcSRT\common\*.h" -Destination "$dstSRT\common" -Force

# 빌드된 헤더 (중요!)
Copy-Item "$srcSRT\_build\version.h" -Destination $dstSRT -Force
Copy-Item "$srcSRT\_build\srt_config.h" -Destination "$dstSRT\srtcore" -Force
Copy-Item "$srcSRT\_build\platform_sys.h" -Destination $dstSRT -Force

# win 특수 헤더
Copy-Item "$srcSRT\common\win\syslog_defs.h" -Destination "$dstSRT\win" -Force
```

---

## 📄 Step 3: 플러그인 파일 생성

### 3.1 CineSRTStream.uplugin
**파일 위치:** `Plugins\CineSRTStream\CineSRTStream.uplugin`

```json
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0.0",
    "FriendlyName": "CineSRT Stream",
    "Description": "Real-time encrypted SRT streaming from Unreal Engine cameras",
    "Category": "Media",
    "CreatedBy": "Your Company",
    "CreatedByURL": "https://yourcompany.com",
    "DocsURL": "",
    "MarketplaceURL": "",
    "EngineVersion": "5.5.0",
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

### 3.2 CineSRTStream.Build.cs
**파일 위치:** `Plugins\CineSRTStream\Source\CineSRTStream\CineSRTStream.Build.cs`

```csharp
using UnrealBuildTool;
using System.IO;

public class CineSRTStream : ModuleRules
{
    public CineSRTStream(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        // Unreal Engine 5.5 C++20 지원
        CppStandard = CppStandardVersion.Cpp20;
        
        // 예외 처리 활성화 (SRT 필수)
        bEnableExceptions = true;
        bUseRTTI = true;
        
        // 경고 레벨 조정 (SRT 헤더 경고 방지)
        bEnableUndefinedIdentifierWarnings = false;
        
        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Public")
            }
        );
        
        PrivateIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Private")
            }
        );
        
        // 언리얼 엔진 모듈 의존성
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "RenderCore",
                "RHI",
                "Renderer",
                "Projects"
            }
        );
        
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Sockets",
                "Networking",
                "Media",
                "MediaAssets",
                "CineCameraEditor" // UE5.5 시네카메라 지원
            }
        );
        
        // 에디터 전용 모듈
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "UnrealEd",
                    "ToolMenus",
                    "EditorSubsystem"
                }
            );
        }
        
        // Windows 플랫폼 전용 설정
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // ThirdParty 경로 설정
            string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty"));
            string SRTPath = Path.Combine(ThirdPartyPath, "SRT");
            
            // Include 경로 추가
            PublicIncludePaths.AddRange(new string[] {
                Path.Combine(SRTPath, "include"),
                Path.Combine(SRTPath, "include", "srtcore"),
                Path.Combine(SRTPath, "include", "common"),
                Path.Combine(SRTPath, "include", "win")
            });
            
            // 라이브러리 경로
            string LibPath = Path.Combine(SRTPath, "lib", "Win64");
            
            // 라이브러리 파일 확인 및 추가
            string[] RequiredLibs = new string[] {
                "srt_static.lib",
                "libssl.lib",
                "libcrypto.lib",
                "pthreadVC3.lib"
            };
            
            foreach (string LibName in RequiredLibs)
            {
                string LibFullPath = Path.Combine(LibPath, LibName);
                if (File.Exists(LibFullPath))
                {
                    PublicAdditionalLibraries.Add(LibFullPath);
                    System.Console.WriteLine("CineSRTStream: Added library " + LibName);
                }
                else
                {
                    System.Console.WriteLine("CineSRTStream: WARNING - Missing library " + LibFullPath);
                }
            }
            
            // Windows 시스템 라이브러리
            PublicSystemLibraries.AddRange(new string[] {
                "ws2_32.lib",
                "Iphlpapi.lib",
                "Crypt32.lib",
                "Advapi32.lib",
                "User32.lib",
                "Gdi32.lib",
                "Ole32.lib",
                "Shell32.lib"
            });
            
            // 전처리기 정의
            PublicDefinitions.AddRange(new string[] {
                "_WIN32_WINNT=0x0A00", // Windows 10
                "WIN32_LEAN_AND_MEAN",
                "NOMINMAX",
                "SRT_STATIC=1",
                "SRT_ENABLE_ENCRYPTION=1",
                "SRT_VERSION_MAJOR=1",
                "SRT_VERSION_MINOR=5",
                "SRT_VERSION_PATCH=3",
                "_CRT_SECURE_NO_WARNINGS",
                "_WINSOCK_DEPRECATED_NO_WARNINGS",
                "WINDOWS_IGNORE_PACKING_MISMATCH"
            });
        }
    }
}
```

### 3.3 CineSRTStream.h
**파일 위치:** `Plugins\CineSRTStream\Source\CineSRTStream\Public\CineSRTStream.h`

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
    
    /** Gets the module singleton */
    static inline FCineSRTStreamModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FCineSRTStreamModule>("CineSRTStream");
    }
    
    /** Checks if the module is loaded and available */
    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("CineSRTStream");
    }
    
    /** SRT 라이브러리 상태 확인 */
    bool IsSRTInitialized() const { return bSRTInitialized; }
    
private:
    bool bSRTInitialized = false;
    
    /** SRT 라이브러리 초기화 */
    bool InitializeSRT();
    
    /** SRT 라이브러리 정리 */
    void CleanupSRT();
};
```

### 3.4 CineSRTStream.cpp
**파일 위치:** `Plugins\CineSRTStream\Source\CineSRTStream\Private\CineSRTStream.cpp`

```cpp
#include "CineSRTStream.h"

// Windows 헤더 충돌 방지
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    // SRT 헤더 전 경고 비활성화
    #pragma warning(push)
    #pragma warning(disable: 4005) // macro redefinition
    #pragma warning(disable: 4996) // deprecated functions
    #pragma warning(disable: 4244) // conversion warnings
    #pragma warning(disable: 4267) // size_t to int conversion
#endif

// SRT 헤더 (외부에서 포함)
extern "C" {
    #include "srt.h"
}

#ifdef _WIN32
    #pragma warning(pop)
#endif

#define LOCTEXT_NAMESPACE "FCineSRTStreamModule"

DEFINE_LOG_CATEGORY(LogCineSRTStream);

void FCineSRTStreamModule::StartupModule()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("=== CineSRTStream Module Starting ==="));
    
    // Windows 소켓 초기화 (SRT 사용 전 필수)
#ifdef _WIN32
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("WSAStartup failed: %d"), wsaResult);
        return;
    }
    UE_LOG(LogCineSRTStream, Log, TEXT("Windows Sockets initialized"));
#endif
    
    // SRT 초기화
    if (InitializeSRT())
    {
        UE_LOG(LogCineSRTStream, Log, TEXT("✅ CineSRTStream module initialized successfully"));
    }
    else
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("❌ Failed to initialize CineSRTStream module"));
    }
}

void FCineSRTStreamModule::ShutdownModule()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("=== CineSRTStream Module Shutting Down ==="));
    
    CleanupSRT();
    
#ifdef _WIN32
    WSACleanup();
    UE_LOG(LogCineSRTStream, Log, TEXT("Windows Sockets cleaned up"));
#endif
}

bool FCineSRTStreamModule::InitializeSRT()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("Initializing SRT library..."));
    
    // SRT 초기화
    if (srt_startup() != 0)
    {
        const char* errStr = srt_getlasterror_str();
        UE_LOG(LogCineSRTStream, Error, TEXT("srt_startup failed: %hs"), errStr);
        return false;
    }
    
    // 버전 정보 출력
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Library Version: %hs"), SRT_VERSION_STRING);
    
    // 로깅 설정
    srt_setloglevel(SRT_LOG_WARNING);
    
    // 암호화 지원 확인
    SRTSOCKET testSock = srt_create_socket();
    if (testSock != SRT_INVALID_SOCK)
    {
        int pbkeylen = 16; // AES-128
        if (srt_setsockopt(testSock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen)) == 0)
        {
            UE_LOG(LogCineSRTStream, Log, TEXT("✅ SRT Encryption support verified"));
        }
        else
        {
            UE_LOG(LogCineSRTStream, Warning, TEXT("⚠️  SRT Encryption may not be available"));
        }
        srt_close(testSock);
    }
    
    bSRTInitialized = true;
    return true;
}

void FCineSRTStreamModule::CleanupSRT()
{
    if (bSRTInitialized)
    {
        UE_LOG(LogCineSRTStream, Log, TEXT("Cleaning up SRT library..."));
        srt_cleanup();
        bSRTInitialized = false;
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCineSRTStreamModule, CineSRTStream)
```

### 3.5 SRTStreamComponent.h
**파일 위치:** `Plugins\CineSRTStream\Source\CineSRTStream\Public\SRTStreamComponent.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraComponent.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "SRTStreamComponent.generated.h"

// Forward declarations
class FSRTStreamWorker;

UENUM(BlueprintType)
enum class ESRTStreamMode : uint8
{
    HD_1280x720 UMETA(DisplayName = "HD Ready 720p"),
    HD_1920x1080 UMETA(DisplayName = "Full HD 1080p"),
    UHD_3840x2160 UMETA(DisplayName = "Ultra HD 4K"),
    Custom UMETA(DisplayName = "Custom Resolution")
};

UENUM(BlueprintType)
enum class ESRTConnectionState : uint8
{
    Disconnected UMETA(DisplayName = "Disconnected"),
    Connecting UMETA(DisplayName = "Connecting"),
    Connected UMETA(DisplayName = "Connected"),
    Streaming UMETA(DisplayName = "Streaming"),
    Error UMETA(DisplayName = "Error")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnSRTStateChanged,
    ESRTConnectionState, NewState,
    const FString&, Message
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnSRTStatsUpdated,
    float, BitrateKbps,
    int32, FramesSent,
    float, RTTms
);

UCLASS(ClassGroup=(Streaming), meta=(BlueprintSpawnableComponent), DisplayName="SRT Stream Component")
class CINESRTSTREAM_API USRTStreamComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USRTStreamComponent();
    virtual ~USRTStreamComponent();

    // === 설정 속성 ===
    
    /** 연결 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Connection")
    FString StreamIP = TEXT("127.0.0.1");
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Connection", 
        meta = (ClampMin = "1024", ClampMax = "65535"))
    int32 StreamPort = 9001;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Connection")
    FString StreamID = TEXT("UnrealCamera1");
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Connection",
        meta = (ToolTip = "Connection mode: true = caller (client), false = listener (server)"))
    bool bCallerMode = true;
    
    /** 비디오 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Video")
    ESRTStreamMode StreamMode = ESRTStreamMode::HD_1920x1080;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Video",
        meta = (EditCondition = "StreamMode == ESRTStreamMode::Custom"))
    int32 CustomWidth = 1920;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Video",
        meta = (EditCondition = "StreamMode == ESRTStreamMode::Custom"))
    int32 CustomHeight = 1080;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Video", 
        meta = (ClampMin = "1", ClampMax = "120"))
    float StreamFPS = 30.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Video",
        meta = (ClampMin = "100", ClampMax = "50000", ToolTip = "Target bitrate in Kbps"))
    int32 BitrateKbps = 5000;
    
    /** 암호화 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Encryption")
    bool bUseEncryption = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Encryption", 
        meta = (EditCondition = "bUseEncryption"))
    FString EncryptionPassphrase = TEXT("YourSecurePassphrase");
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Encryption",
        meta = (EditCondition = "bUseEncryption", ClampMin = "0", ClampMax = "32"))
    int32 EncryptionKeyLength = 16; // 0=auto, 16=AES-128, 24=AES-192, 32=AES-256
    
    /** 성능 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Performance",
        meta = (ClampMin = "20", ClampMax = "8000", ToolTip = "Latency in milliseconds"))
    int32 LatencyMs = 120;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Performance",
        meta = (ToolTip = "Use hardware acceleration if available"))
    bool bUseHardwareAcceleration = true;
    
    // === 상태 속성 (읽기 전용) ===
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    ESRTConnectionState ConnectionState = ESRTConnectionState::Disconnected;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    float CurrentBitrateKbps = 0.0f;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    int32 TotalFramesSent = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    int32 DroppedFrames = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    float RoundTripTimeMs = 0.0f;
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    FString LastErrorMessage;
    
    // === 이벤트 ===
    
    UPROPERTY(BlueprintAssignable, Category = "SRT Events")
    FOnSRTStateChanged OnStateChanged;
    
    UPROPERTY(BlueprintAssignable, Category = "SRT Events")
    FOnSRTStatsUpdated OnStatsUpdated;
    
    // === 블루프린트 메서드 ===
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream", meta = (CallInEditor = "true"))
    void StartStreaming();
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream", meta = (CallInEditor = "true"))
    void StopStreaming();
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    bool IsStreaming() const { return bIsStreaming; }
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    FString GetConnectionInfo() const;
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    FString GetStreamURL() const;
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream", meta = (CallInEditor = "true"))
    void TestConnection();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, 
        FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    // 스트리밍 상태
    bool bIsStreaming = false;
    TAtomic<bool> bStopRequested{false};
    
    // Scene Capture
    UPROPERTY()
    USceneCaptureComponent2D* SceneCapture = nullptr;
    
    UPROPERTY()
    UTextureRenderTarget2D* RenderTarget = nullptr;
    
    // 워커 스레드
    TUniquePtr<FSRTStreamWorker> StreamWorker;
    FRunnableThread* WorkerThread = nullptr;
    
    // Frame buffer
    TArray<FColor> FrameBuffer;
    FCriticalSection FrameBufferMutex;
    TAtomic<bool> bNewFrameReady{false};
    
    // 통계
    double LastStatsUpdateTime = 0.0;
    const double StatsUpdateInterval = 1.0;
    
    // 내부 메서드
    void GetResolution(int32& OutWidth, int32& OutHeight) const;
    bool SetupSceneCapture();
    void CleanupSceneCapture();
    void CaptureFrame();
    void UpdateStats();
    void SetConnectionState(ESRTConnectionState NewState, const FString& Message = TEXT(""));
    
    friend class FSRTStreamWorker;
};

/**
 * SRT 스트리밍 워커 스레드
 */
class FSRTStreamWorker : public FRunnable
{
public:
    FSRTStreamWorker(USRTStreamComponent* InOwner);
    virtual ~FSRTStreamWorker();
    
    // FRunnable interface
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;
    
private:
    USRTStreamComponent* Owner;
    void* SRTSocket = nullptr;
    
    bool InitializeSRT();
    void CleanupSRT();
    bool SendFrameData();
    void UpdateSRTStats();
};
```

### 3.6 SRTStreamComponent.cpp
**파일 위치:** `Plugins\CineSRTStream\Source\CineSRTStream\Private\SRTStreamComponent.cpp`

```cpp
#include "SRTStreamComponent.h"
#include "CineSRTStream.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "RenderingThread.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "TextureResource.h"
#include "Async/Async.h"
#include "HAL/PlatformFilemanager.h"

// Windows 헤더
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    #pragma warning(push)
    #pragma warning(disable: 4005)
    #pragma warning(disable: 4996)
#endif

// SRT 헤더
extern "C" {
    #include "srt.h"
}

#ifdef _WIN32
    #pragma warning(pop)
#endif

// ================================================================================
// USRTStreamComponent Implementation
// ================================================================================

USRTStreamComponent::USRTStreamComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    bAutoActivate = true;
}

USRTStreamComponent::~USRTStreamComponent()
{
    StopStreaming();
}

void USRTStreamComponent::BeginPlay()
{
    Super::BeginPlay();
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRTStreamComponent initialized - Target: %s:%d @ %.1f FPS"),
        *StreamIP, StreamPort, StreamFPS);
}

void USRTStreamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopStreaming();
    Super::EndPlay(EndPlayReason);
}

void USRTStreamComponent::TickComponent(float DeltaTime, ELevelTick TickType, 
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    if (!bIsStreaming)
        return;
    
    // Update stats periodically
    double CurrentTime = FPlatformTime::Seconds();
    if (CurrentTime - LastStatsUpdateTime >= StatsUpdateInterval)
    {
        UpdateStats();
        LastStatsUpdateTime = CurrentTime;
    }
    
    // Capture frame at target FPS
    static double LastCaptureTime = 0.0;
    double FrameInterval = 1.0 / StreamFPS;
    
    if (CurrentTime - LastCaptureTime >= FrameInterval)
    {
        CaptureFrame();
        LastCaptureTime = CurrentTime;
    }
}

#if WITH_EDITOR
void USRTStreamComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    
    if (PropertyChangedEvent.Property)
    {
        const FName PropertyName = PropertyChangedEvent.Property->GetFName();
        
        // Validate settings
        if (PropertyName == GET_MEMBER_NAME_CHECKED(USRTStreamComponent, StreamFPS))
        {
            StreamFPS = FMath::Clamp(StreamFPS, 1.0f, 120.0f);
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(USRTStreamComponent, BitrateKbps))
        {
            BitrateKbps = FMath::Clamp(BitrateKbps, 100, 50000);
        }
    }
}
#endif

void USRTStreamComponent::StartStreaming()
{
    if (bIsStreaming)
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("Already streaming"));
        return;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("=== Starting SRT Stream ==="));
    UE_LOG(LogCineSRTStream, Log, TEXT("Target: %s:%d"), *StreamIP, StreamPort);
    UE_LOG(LogCineSRTStream, Log, TEXT("Mode: %s"), bCallerMode ? TEXT("Caller (Client)") : TEXT("Listener (Server)"));
    
    SetConnectionState(ESRTConnectionState::Connecting, TEXT("Initializing capture..."));
    
    // Setup scene capture
    if (!SetupSceneCapture())
    {
        SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to setup scene capture"));
        return;
    }
    
    // Reset stop flag
    bStopRequested = false;
    bIsStreaming = true;
    
    // Create and start worker thread
    StreamWorker = MakeUnique<FSRTStreamWorker>(this);
    WorkerThread = FRunnableThread::Create(StreamWorker.Get(), TEXT("SRTStreamWorker"));
    
    if (!WorkerThread)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create worker thread"));
        bIsStreaming = false;
        CleanupSceneCapture();
        SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to create worker thread"));
        return;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT streaming started"));
}

void USRTStreamComponent::StopStreaming()
{
    if (!bIsStreaming)
        return;
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Stopping SRT stream..."));
    
    // Signal stop
    bStopRequested = true;
    bIsStreaming = false;
    
    // Stop worker thread
    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    
    StreamWorker.Reset();
    
    // Cleanup
    CleanupSceneCapture();
    
    // Reset stats
    CurrentBitrateKbps = 0.0f;
    TotalFramesSent = 0;
    DroppedFrames = 0;
    RoundTripTimeMs = 0.0f;
    
    SetConnectionState(ESRTConnectionState::Disconnected, TEXT("Stream stopped"));
}

void USRTStreamComponent::TestConnection()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("=== Testing SRT Connection ==="));
    
    SRTSOCKET testSock = srt_create_socket();
    if (testSock == SRT_INVALID_SOCK)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create test socket"));
        return;
    }
    
    // Test encryption
    if (bUseEncryption)
    {
        int pbkeylen = EncryptionKeyLength;
        if (srt_setsockopt(testSock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen)) == 0)
        {
            UE_LOG(LogCineSRTStream, Log, TEXT("✅ Encryption test passed (%d-bit AES)"), pbkeylen * 8);
        }
        else
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("❌ Encryption test failed"));
        }
    }
    
    srt_close(testSock);
    UE_LOG(LogCineSRTStream, Log, TEXT("Test completed"));
}

void USRTStreamComponent::GetResolution(int32& OutWidth, int32& OutHeight) const
{
    switch (StreamMode)
    {
        case ESRTStreamMode::HD_1280x720:
            OutWidth = 1280;
            OutHeight = 720;
            break;
        case ESRTStreamMode::HD_1920x1080:
            OutWidth = 1920;
            OutHeight = 1080;
            break;
        case ESRTStreamMode::UHD_3840x2160:
            OutWidth = 3840;
            OutHeight = 2160;
            break;
        case ESRTStreamMode::Custom:
            OutWidth = CustomWidth;
            OutHeight = CustomHeight;
            break;
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
    
    // Get resolution
    int32 Width, Height;
    GetResolution(Width, Height);
    
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
    SceneCapture->ShowFlags.SetMotionBlur(false);
    SceneCapture->ShowFlags.SetAntiAliasing(true);
    
    // Copy camera settings
    SceneCapture->FOVAngle = Camera->FieldOfView;
    
    // Allocate frame buffer
    FrameBuffer.SetNum(Width * Height);
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Scene capture setup complete: %dx%d"), Width, Height);
    return true;
}

void USRTStreamComponent::CleanupSceneCapture()
{
    if (SceneCapture)
    {
        SceneCapture->DestroyComponent();
        SceneCapture = nullptr;
    }
    
    RenderTarget = nullptr;
    FrameBuffer.Empty();
}

void USRTStreamComponent::CaptureFrame()
{
    if (!SceneCapture || !RenderTarget)
        return;
    
    // Capture the scene
    SceneCapture->CaptureScene();
    
    // Read pixels asynchronously
    FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!RenderTargetResource)
        return;
    
    struct FReadSurfaceContext
    {
        USRTStreamComponent* Component;
        FRenderTarget* SrcRenderTarget;
        TArray<FColor>* OutData;
        FIntRect Rect;
    };
    
    FReadSurfaceContext Context = {
        this,
        RenderTargetResource,
        &FrameBuffer,
        FIntRect(0, 0, RenderTarget->SizeX, RenderTarget->SizeY)
    };
    
    ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)(
        [Context](FRHICommandListImmediate& RHICmdList)
        {
            RHICmdList.ReadSurfaceData(
                Context.SrcRenderTarget->GetRenderTargetTexture(),
                Context.Rect,
                *Context.OutData,
                FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX)
            );
            
            Context.Component->bNewFrameReady = true;
        });
}

void USRTStreamComponent::UpdateStats()
{
    if (OnStatsUpdated.IsBound())
    {
        OnStatsUpdated.Broadcast(CurrentBitrateKbps, TotalFramesSent, RoundTripTimeMs);
    }
}

void USRTStreamComponent::SetConnectionState(ESRTConnectionState NewState, const FString& Message)
{
    if (ConnectionState != NewState)
    {
        ConnectionState = NewState;
        LastErrorMessage = Message;
        
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
            case ESRTConnectionState::Streaming:
                StateStr = TEXT("Streaming");
                break;
            case ESRTConnectionState::Error:
                StateStr = TEXT("Error");
                break;
        }
        
        UE_LOG(LogCineSRTStream, Log, TEXT("State: %s - %s"), *StateStr, *Message);
        
        // Broadcast on game thread
        AsyncTask(ENamedThreads::GameThread, [this, NewState, Message]()
        {
            if (OnStateChanged.IsBound())
            {
                OnStateChanged.Broadcast(NewState, Message);
            }
        });
    }
}

FString USRTStreamComponent::GetConnectionInfo() const
{
    if (!bIsStreaming)
        return TEXT("Not streaming");
    
    return FString::Printf(TEXT("%s:%d | %.1f Kbps | %d frames | %.1f ms RTT"),
        *StreamIP, StreamPort, CurrentBitrateKbps, TotalFramesSent, RoundTripTimeMs);
}

FString USRTStreamComponent::GetStreamURL() const
{
    FString URL = FString::Printf(TEXT("srt://%s:%d"), *StreamIP, StreamPort);
    
    TArray<FString> Options;
    
    if (!bCallerMode)
    {
        Options.Add(TEXT("mode=listener"));
    }
    
    if (bUseEncryption && !EncryptionPassphrase.IsEmpty())
    {
        Options.Add(FString::Printf(TEXT("passphrase=%s"), *EncryptionPassphrase));
    }
    
    if (!StreamID.IsEmpty())
    {
        Options.Add(FString::Printf(TEXT("streamid=%s"), *StreamID));
    }
    
    Options.Add(FString::Printf(TEXT("latency=%d"), LatencyMs));
    
    if (Options.Num() > 0)
    {
        URL += TEXT("?") + FString::Join(Options, TEXT("&"));
    }
    
    return URL;
}

// ================================================================================
// FSRTStreamWorker Implementation
// ================================================================================

FSRTStreamWorker::FSRTStreamWorker(USRTStreamComponent* InOwner)
    : Owner(InOwner)
{
}

FSRTStreamWorker::~FSRTStreamWorker()
{
    CleanupSRT();
}

bool FSRTStreamWorker::Init()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread initializing..."));
    return InitializeSRT();
}

uint32 FSRTStreamWorker::Run()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread started"));
    
    // Frame timing
    const double FrameInterval = 1.0 / Owner->StreamFPS;
    double LastFrameTime = FPlatformTime::Seconds();
    double LastStatsTime = LastFrameTime;
    
    while (!Owner->bStopRequested)
    {
        double CurrentTime = FPlatformTime::Seconds();
        
        // Send frame if ready and timing is right
        if (CurrentTime - LastFrameTime >= FrameInterval)
        {
            if (Owner->bNewFrameReady)
            {
                if (SendFrameData())
                {
                    Owner->TotalFramesSent++;
                }
                else
                {
                    Owner->DroppedFrames++;
                }
                Owner->bNewFrameReady = false;
            }
            LastFrameTime = CurrentTime;
        }
        
        // Update stats periodically
        if (CurrentTime - LastStatsTime >= 1.0)
        {
            UpdateSRTStats();
            LastStatsTime = CurrentTime;
        }
        
        // Small sleep to prevent CPU spinning
        FPlatformProcess::Sleep(0.001f);
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread ending"));
    return 0;
}

void FSRTStreamWorker::Stop()
{
    Owner->bStopRequested = true;
}

void FSRTStreamWorker::Exit()
{
    CleanupSRT();
}

bool FSRTStreamWorker::InitializeSRT()
{
    // Create socket
    SRTSOCKET sock = srt_create_socket();
    if (sock == SRT_INVALID_SOCK)
    {
        Owner->SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to create SRT socket"));
        return false;
    }
    
    // Configure socket options
    int yes = 1;
    int live_mode = SRTT_LIVE;
    
    srt_setsockopt(sock, 0, SRTO_TRANSTYPE, &live_mode, sizeof(live_mode));
    
    // Set mode
    if (Owner->bCallerMode)
    {
        srt_setsockopt(sock, 0, SRTO_SENDER, &yes, sizeof(yes));
    }
    
    // Stream ID
    if (!Owner->StreamID.IsEmpty())
    {
        std::string streamId = TCHAR_TO_UTF8(*Owner->StreamID);
        srt_setsockopt(sock, 0, SRTO_STREAMID, streamId.c_str(), streamId.length());
    }
    
    // Encryption
    if (Owner->bUseEncryption)
    {
        int pbkeylen = Owner->EncryptionKeyLength;
        srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen));
        
        if (!Owner->EncryptionPassphrase.IsEmpty())
        {
            std::string passphrase = TCHAR_TO_UTF8(*Owner->EncryptionPassphrase);
            srt_setsockopt(sock, 0, SRTO_PASSPHRASE, passphrase.c_str(), passphrase.length());
        }
    }
    
    // Performance options
    int latency = Owner->LatencyMs;
    srt_setsockopt(sock, 0, SRTO_LATENCY, &latency, sizeof(latency));
    
    int mss = 1500;
    srt_setsockopt(sock, 0, SRTO_MSS, &mss, sizeof(mss));
    
    int fc = 25600;
    srt_setsockopt(sock, 0, SRTO_FC, &fc, sizeof(fc));
    
    int sndbuf = 12058624; // 12MB
    srt_setsockopt(sock, 0, SRTO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    // Connect or bind
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(Owner->StreamPort);
    
    if (Owner->bCallerMode)
    {
        // Caller mode - connect to server
        inet_pton(AF_INET, TCHAR_TO_UTF8(*Owner->StreamIP), &sa.sin_addr);
        
        Owner->SetConnectionState(ESRTConnectionState::Connecting, 
            FString::Printf(TEXT("Connecting to %s:%d..."), *Owner->StreamIP, Owner->StreamPort));
        
        if (srt_connect(sock, (sockaddr*)&sa, sizeof(sa)) == SRT_ERROR)
        {
            FString Error = UTF8_TO_TCHAR(srt_getlasterror_str());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Connection failed: %s"), *Error));
            srt_close(sock);
            return false;
        }
        
        SRTSocket = (void*)sock;
        Owner->SetConnectionState(ESRTConnectionState::Connected, TEXT("Connected successfully"));
    }
    else
    {
        // Listener mode - wait for connection
        sa.sin_addr.s_addr = INADDR_ANY;
        
        if (srt_bind(sock, (sockaddr*)&sa, sizeof(sa)) != 0)
        {
            FString Error = UTF8_TO_TCHAR(srt_getlasterror_str());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Bind failed: %s"), *Error));
            srt_close(sock);
            return false;
        }
        
        if (srt_listen(sock, 1) != 0)
        {
            FString Error = UTF8_TO_TCHAR(srt_getlasterror_str());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Listen failed: %s"), *Error));
            srt_close(sock);
            return false;
        }
        
        Owner->SetConnectionState(ESRTConnectionState::Connecting, 
            FString::Printf(TEXT("Listening on port %d..."), Owner->StreamPort));
        
        // Accept connection
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SRTSOCKET client = srt_accept(sock, (sockaddr*)&client_addr, &addr_len);
        
        if (client == SRT_INVALID_SOCK)
        {
            FString Error = UTF8_TO_TCHAR(srt_getlasterror_str());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Accept failed: %s"), *Error));
            srt_close(sock);
            return false;
        }
        
        srt_close(sock); // Close listening socket
        SRTSocket = (void*)client;
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        Owner->SetConnectionState(ESRTConnectionState::Connected, 
            FString::Printf(TEXT("Client connected from %hs"), client_ip));
    }
    
    // Start streaming
    Owner->SetConnectionState(ESRTConnectionState::Streaming, TEXT("Streaming active"));
    return true;
}

void FSRTStreamWorker::CleanupSRT()
{
    if (SRTSocket)
    {
        SRTSOCKET sock = (SRTSOCKET)SRTSocket;
        srt_close(sock);
        SRTSocket = nullptr;
    }
}

bool FSRTStreamWorker::SendFrameData()
{
    if (!SRTSocket || !Owner)
        return false;
    
    SRTSOCKET sock = (SRTSOCKET)SRTSocket;
    
    // Lock and copy frame data
    FScopeLock Lock(&Owner->FrameBufferMutex);
    
    if (Owner->FrameBuffer.Num() == 0)
        return false;
    
    // For now, send raw pixel data with simple header
    // In Phase 3, this will be replaced with H.264 encoded data
    
    struct FrameHeader
    {
        uint32 Magic = 0x53525446; // 'SRTF'
        uint32 Width;
        uint32 Height;
        uint32 PixelFormat = 1; // 1 = BGRA8
        uint32 DataSize;
        uint64 Timestamp;
        uint32 FrameNumber;
    };
    
    int32 Width, Height;
    Owner->GetResolution(Width, Height);
    
    FrameHeader Header;
    Header.Width = Width;
    Header.Height = Height;
    Header.DataSize = Owner->FrameBuffer.Num() * sizeof(FColor);
    Header.Timestamp = FPlatformTime::Cycles64();
    Header.FrameNumber = Owner->TotalFramesSent;
    
    // Send header
    int sent = srt_send(sock, (char*)&Header, sizeof(Header));
    if (sent == SRT_ERROR)
    {
        if (!Owner->bStopRequested)
        {
            FString Error = UTF8_TO_TCHAR(srt_getlasterror_str());
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send header: %s"), *Error);
        }
        return false;
    }
    
    // Send pixel data in chunks
    const int ChunkSize = 1316; // SRT recommended payload size
    const uint8* DataPtr = (const uint8*)Owner->FrameBuffer.GetData();
    int32 TotalSize = Header.DataSize;
    int32 BytesSent = 0;
    
    while (BytesSent < TotalSize && !Owner->bStopRequested)
    {
        int32 ToSend = FMath::Min(ChunkSize, TotalSize - BytesSent);
        sent = srt_send(sock, (char*)(DataPtr + BytesSent), ToSend);
        
        if (sent == SRT_ERROR)
        {
            if (!Owner->bStopRequested)
            {
                FString Error = UTF8_TO_TCHAR(srt_getlasterror_str());
                UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send data: %s"), *Error);
            }
            return false;
        }
        
        BytesSent += sent;
    }
    
    return true;
}

void FSRTStreamWorker::UpdateSRTStats()
{
    if (!SRTSocket)
        return;
    
    SRTSOCKET sock = (SRTSOCKET)SRTSocket;
    
    SRT_TRACEBSTATS stats;
    if (srt_bstats(sock, &stats, 1) == 0) // 1 = clear after reading
    {
        // Update owner stats
        Owner->CurrentBitrateKbps = static_cast<float>(stats.mbpsSendRate * 1000.0);
        Owner->RoundTripTimeMs = static_cast<float>(stats.msRTT);
        
        // Calculate dropped frames from packet loss
        if (stats.pktSndLossTotal > 0)
        {
            Owner->DroppedFrames += stats.pktSndLossTotal;
        }
    }
}
```

---

## 🔨 Step 4: 빌드 및 테스트

### 4.1 프로젝트 파일 재생성
1. 언리얼 에디터가 완전히 종료되었는지 확인
2. `SRTStreamTest.uproject` 파일 우클릭
3. **"Generate Visual Studio project files"** 선택
4. 성공 메시지 확인

### 4.2 Visual Studio 빌드
1. `SRTStreamTest.sln` 열기
2. Solution Configuration: **Development Editor**
3. Solution Platform: **Win64**
4. **Build → Build Solution** (Ctrl+Shift+B)
5. 예상 시간: 첫 빌드 10-15분

### 4.3 일반적인 빌드 에러 해결

#### 링크 에러 (LNK2019)
```
에러: unresolved external symbol srt_startup
해결: Build.cs의 라이브러리 경로 확인
```

#### 헤더 파일 못 찾음
```
에러: cannot open include file: 'srt.h'
해결: ThirdParty/SRT/include 경로 확인
```

#### 플러그인 로드 실패
```
에러: Missing or incompatible modules in CineSRTStream plugin
해결: Binaries 폴더 삭제 후 재빌드
```

---

## 🎮 Step 5: 언리얼 에디터에서 테스트

### 5.1 플러그인 확인
1. 언리얼 에디터 실행
2. **Edit → Plugins**
3. "Media" 카테고리에서 "CineSRT Stream" 확인
4. Enabled 체크

### 5.2 테스트 씬 설정
1. 새 레벨 생성 또는 기본 레벨 사용
2. **Place Actors → Cinematic → Cine Camera Actor** 배치
3. 카메라 위치 조정 (씬이 잘 보이도록)

### 5.3 컴포넌트 추가
1. Cine Camera Actor 선택
2. **Details → Add Component → SRT Stream**
3. 컴포넌트 설정:
   - Stream IP: `127.0.0.1`
   - Stream Port: `9001`
   - Stream ID: `TestCamera1`
   - Caller Mode: ✓ (체크)
   - Use Encryption: ✓
   - Encryption Passphrase: `YourSecurePassphrase`

### 5.4 수신 테스트
1. Phase 1에서 만든 `receiver.exe` 실행
2. 언리얼 에디터에서:
   - SRT Stream Component 선택
   - Details 패널에서 **"Start Streaming"** 버튼 클릭
3. receiver.exe에서 데이터 수신 확인

### 5.5 출력 로그 확인
- **Window → Developer Tools → Output Log**
- LogCineSRTStream 카테고리 필터링
- 연결 상태 메시지 확인

---

## 🐛 트러블슈팅 가이드

### 문제: 플러그인이 로드되지 않음
**해결:**
1. `.uproject` 파일을 텍스트 에디터로 열기
2. Plugins 섹션에 다음 추가:
```json
"Plugins": [
    {
        "Name": "CineSRTStream",
        "Enabled": true
    }
]
```

### 문제: Windows Defender가 연결을 차단
**해결:**
1. Windows Defender 방화벽 설정
2. 인바운드/아웃바운드 규칙 추가
3. 포트 9000-9010 허용

### 문제: 컴포넌트가 보이지 않음
**해결:**
1. 프로젝트 설정 → Plugins → CineSRTStream 활성화 확인
2. 에디터 재시작

### 문제: 암호화 연결 실패
**해결:**
1. OpenSSL 라이브러리가 정상적으로 링크되었는지 확인
2. Test Connection 버튼으로 암호화 지원 확인

---

## ✅ Phase 2 완료 체크리스트

- [ ] 플러그인 폴더 구조 생성
- [ ] 모든 라이브러리 파일 복사 (.lib)
- [ ] 모든 헤더 파일 복사 (.h)
- [ ] 플러그인 파일 생성 (5개)
- [ ] Visual Studio 빌드 성공
- [ ] 언리얼 에디터에서 플러그인 로드
- [ ] 컴포넌트 추가 가능
- [ ] Start Streaming 동작
- [ ] receiver.exe에서 데이터 수신
- [ ] 로그에 연결 성공 메시지

---

## 🚀 다음 단계: Phase 3

Phase 2가 완료되면 Phase 3 (H.264 인코딩)로 진행할 수 있습니다.
- FFmpeg 통합
- H.264 인코더 구현
- MPEG-TS 멀티플렉싱
- OBS에서 실시간 영상 수신

Phase 2 성공을 축하합니다! 🎉