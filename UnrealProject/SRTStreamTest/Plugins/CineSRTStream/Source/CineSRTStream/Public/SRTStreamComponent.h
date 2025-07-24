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
    TSharedPtr<FrameBuffer> FrameBuffer;
    TAtomic<bool> bShuttingDown{false};
};

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
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SRT Settings|Performance",
        meta = (ToolTip = "Automatically reconnect on connection loss"))
    bool bAutoReconnect = true;
    
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
    
    // 프레임 버퍼 시스템
    TSharedPtr<FrameBuffer> FrameBuffer;
    TSharedPtr<FGPUReadbackManager> GPUReadbackManager;
    
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
    void HandleDisconnection();
    void CheckHealth();
    void CleanupConnection();
}; 