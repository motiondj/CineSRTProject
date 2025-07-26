#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraComponent.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

// 전방 선언 대신 헤더 포함!
#include "SRTVideoEncoder.h"
#include "SRTTransportStream.h"

#include "SRTStreamComponent.generated.h"

// ===== ENUM 정의들을 여기에 먼저! =====

UENUM(BlueprintType)
enum class ESRTQualityPreset : uint8
{
    Low UMETA(DisplayName = "Low - Fast Performance"),
    Medium UMETA(DisplayName = "Medium - Balanced"),
    High UMETA(DisplayName = "High - Better Quality"),
    Ultra UMETA(DisplayName = "Ultra - Best Quality")
};

UENUM(BlueprintType)
enum class EH264Profile : uint8
{
    Baseline UMETA(DisplayName = "Baseline"),
    Main UMETA(DisplayName = "Main"),
    High UMETA(DisplayName = "High"),
    High10 UMETA(DisplayName = "High 10-bit")
};

UENUM(BlueprintType)
enum class EColorSpace : uint8
{
    YUV420 UMETA(DisplayName = "YUV 4:2:0 (Standard)"),
    YUV422 UMETA(DisplayName = "YUV 4:2:2 (Better)"),
    YUV444 UMETA(DisplayName = "YUV 4:4:4 (Best)")
};

UENUM(BlueprintType)
enum class ECaptureSource : uint8
{
    SceneColor UMETA(DisplayName = "Scene Color (LDR)"),
    SceneColorHDR UMETA(DisplayName = "Scene Color (HDR)"),
    FinalColor UMETA(DisplayName = "Final Color (LDR)"),
    FinalColorHDR UMETA(DisplayName = "Final Color (HDR)")
};

UENUM(BlueprintType)
enum class EEncoderPreset : uint8
{
    UltraFast UMETA(DisplayName = "Ultra Fast (Lowest Quality)"),
    SuperFast UMETA(DisplayName = "Super Fast"),
    VeryFast UMETA(DisplayName = "Very Fast"),
    Faster UMETA(DisplayName = "Faster"),
    Fast UMETA(DisplayName = "Fast"),
    Medium UMETA(DisplayName = "Medium (Balanced)"),
    Slow UMETA(DisplayName = "Slow (Better Quality)"),
    Slower UMETA(DisplayName = "Slower"),
    VerySlow UMETA(DisplayName = "Very Slow (Best Quality)")
};

UENUM(BlueprintType)
enum class EEncoderTune : uint8
{
    None UMETA(DisplayName = "None (Default)"),
    Film UMETA(DisplayName = "Film (Movies)"),
    Animation UMETA(DisplayName = "Animation"),
    Grain UMETA(DisplayName = "Grain (Preserve Grain)"),
    StillImage UMETA(DisplayName = "Still Image"),
    PSNR UMETA(DisplayName = "PSNR (Benchmarking)"),
    SSIM UMETA(DisplayName = "SSIM (Benchmarking)"),
    FastDecode UMETA(DisplayName = "Fast Decode"),
    ZeroLatency UMETA(DisplayName = "Zero Latency (Live)")
};

// 전방 선언 제거
// class FSRTVideoEncoder;  <- 삭제
// class FSRTTransportStream;  <- 삭제

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

// 단순한 프레임 버퍼 시스템
class FrameBuffer {
public:
    struct Frame {
        TArray<uint8> Data;
        uint32 FrameNumber;
        double Timestamp;
        int32 Width;
        int32 Height;
    };
    
    void SetFrame(Frame&& frame);
    bool GetFrame(Frame& OutFrame);
    void Clear();
    bool HasNewFrame() const;
    
private:
    Frame CurrentFrame;
    mutable FCriticalSection Mutex;
    TAtomic<bool> bNewFrameReady{false};
};

// 전용 GPU 읽기 매니저 (단순화된 버전)
class FGPUReadbackManager : public TSharedFromThis<FGPUReadbackManager> {
public:
    FGPUReadbackManager(TSharedPtr<FrameBuffer> InFrameBuffer);
    
    void RequestReadback(UTextureRenderTarget2D* RenderTarget, uint32 FrameNumber);
    void Shutdown();
    
private:
    TSharedPtr<FrameBuffer> MyFrameBuffer;  // 명확한 이름
    TAtomic<bool> bShuttingDown{false};
};

UCLASS(ClassGroup=(Streaming), meta=(BlueprintSpawnableComponent), DisplayName="SRT Stream Component")
class CINESRTSTREAM_API USRTStreamComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USRTStreamComponent();
    virtual ~USRTStreamComponent();

    // ========== 기본 설정 (언제든 변경 가능) ==========
    /** 품질 프리셋 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Quality")
    ESRTQualityPreset QualityPreset = ESRTQualityPreset::Medium;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Stream",
        meta = (DisplayPriority = "2", ClampMin = "1000", ClampMax = "50000",
        EditCondition = "!bIsStreaming"))
    int32 BitrateKbps = 5000;
    
    // ========== 고급 설정 (스트리밍 전에만 변경) ==========
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Stream|Advanced",
        meta = (EditCondition = "!bIsStreaming"))
    ESRTStreamMode StreamMode = ESRTStreamMode::HD_1920x1080;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Stream|Advanced",
        meta = (EditCondition = "!bIsStreaming", ClampMin = "1", ClampMax = "60"))
    float StreamFPS = 30.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Stream|Advanced",
        meta = (EditCondition = "!bIsStreaming"))
    bool bUseHardwareAcceleration = true;
    
    // ========== 네트워크 설정 ==========
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Stream|Network",
        meta = (EditCondition = "!bIsStreaming"))
    FString StreamIP = TEXT("127.0.0.1");
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Stream|Network",
        meta = (EditCondition = "!bIsStreaming", ClampMin = "1024", ClampMax = "65535"))
    int32 StreamPort = 9001;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Stream|Network",
        meta = (EditCondition = "!bIsStreaming", ClampMin = "20", ClampMax = "1000", 
               ToolTip = "SRT Latency in milliseconds. Lower = less delay but more packet loss"))
    int32 LatencyMs = 120;
    
    // ========== 읽기 전용 상태 ==========
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SRT Stream|Status")
    FString CurrentStatus = TEXT("Ready");
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SRT Stream|Status")
    float CurrentBitrateKbps = 0.0f;
    
    // === 상태 속성 (읽기 전용) ===
    
    UPROPERTY(BlueprintReadOnly, Category = "SRT Status")
    ESRTConnectionState ConnectionState = ESRTConnectionState::Disconnected;
    
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
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    bool IsReadyToStream() const 
    { 
        return !bIsStreaming && !bCleanupInProgress; 
    }
    
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    FString GetCurrentStatus() const
    {
        if (bIsStreaming) return TEXT("Streaming");
        if (bCleanupInProgress) return TEXT("Cleaning up...");
        return TEXT("Ready");
    }

    /** 프리셋 템플릿 함수 */
    UFUNCTION(BlueprintCallable, Category = "SRT Stream")
    void ApplyUltraQualityPreset()
    {
        QualityPreset = ESRTQualityPreset::Ultra;
        BitrateKbps = 50000;
        
        ApplyQualityPreset();
    }

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // 프리셋 적용 함수
    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Quality")
    void ApplyQualityPreset();

    // 실시간 설정 변경 함수들
    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Runtime", 
        meta = (CallInEditor = "true"))
    void SetBitrateRuntime(int32 NewBitrate);

    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Runtime", 
        meta = (CallInEditor = "true"))
    void SetQualityRuntime(int32 NewCRF);

    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Runtime", 
        meta = (CallInEditor = "true"))
    void ForceKeyFrame();

    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Runtime")
    void ApplyRuntimeSettings();

    // Details 패널에 버튼 추가
    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Presets", 
        meta = (CallInEditor = "true", DisplayName = "Apply Gaming Preset"))
    void ApplyGamingPreset();

    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Presets", 
        meta = (CallInEditor = "true", DisplayName = "Apply Movie Preset"))
    void ApplyMoviePreset();

    UFUNCTION(BlueprintCallable, Category = "SRT Stream|Presets", 
        meta = (CallInEditor = "true", DisplayName = "Apply Fast Preset"))
    void ApplyFastPreset();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, 
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void BeginDestroy() override;

private:
    // 스트리밍 상태
    UPROPERTY()
    bool bIsStreaming = false;
    TAtomic<bool> bStopRequested{false};
    
    // 정리 상태 추적
    TAtomic<bool> bCleanupInProgress{false};
    FCriticalSection CleanupMutex;
    
    // Scene Capture
    UPROPERTY()
    USceneCaptureComponent2D* SceneCapture = nullptr;
    
    UPROPERTY()
    UTextureRenderTarget2D* RenderTarget = nullptr;
    
    // 워커 스레드
    TUniquePtr<FSRTStreamWorker> StreamWorker;
    FRunnableThread* WorkerThread = nullptr;
    
    // 프레임 버퍼 시스템
    TSharedPtr<FrameBuffer> FrameBuffer;
    TSharedPtr<FGPUReadbackManager> GPUReadbackManager;
    
    // Phase 3: 새로운 인코더 및 멀티플렉서
    TUniquePtr<FSRTVideoEncoder> VideoEncoder;
    TUniquePtr<FSRTTransportStream> TransportStream;
    
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

    // 품질 프리셋 내부 값들
    int32 InternalCRF = 23;
    int32 InternalMaxBitrate = 10000;
    FString InternalPreset = TEXT("medium");
    FString InternalProfile = TEXT("main");
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
    
    void ForceCloseSocket();
    
private:
    USRTStreamComponent* Owner;
    void* SRTSocket = nullptr;
    
    // 추가된 멤버들
    TAtomic<bool> bShouldExit{false};      // 종료 플래그
    FCriticalSection SocketLock;           // 소켓 보호용
    
    bool InitializeSRT();
    void CleanupSRT();
    bool SendFrameData();
    void UpdateSRTStats();
    void HandleDisconnection();
    void CheckHealth();
    void CleanupConnection();
}; 