// SRTStreamComponent.cpp - SRT 직접 사용 완전 제거
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

// SRT 네트워크 헤더만 포함 (srt.h 없음!)
#include "SRTNetworkWorker.h"

// Phase 3: 새로운 인코더 및 멀티플렉서
#include "SRTVideoEncoder.h"
#include "SRTTransportStream.h"

#ifdef _WIN32
    #include <string>
    #include <memory>
#endif

// ================================================================================
// FrameBuffer Implementation
// ================================================================================

void FrameBuffer::SetFrame(Frame&& frame)
{
    FScopeLock Lock(&Mutex);
    CurrentFrame = MoveTemp(frame);
    bNewFrameReady = true;
}

bool FrameBuffer::GetFrame(Frame& OutFrame)
{
    FScopeLock Lock(&Mutex);
    if (bNewFrameReady)
    {
        OutFrame = MoveTemp(CurrentFrame);
        bNewFrameReady = false;
        return true;
    }
    return false;
}

void FrameBuffer::Clear()
{
    FScopeLock Lock(&Mutex);
    CurrentFrame.Data.Empty();
    bNewFrameReady = false;
}

bool FrameBuffer::HasNewFrame() const
{
    return bNewFrameReady.Load();
}

// ================================================================================
// FGPUReadbackManager Implementation
// ================================================================================

FGPUReadbackManager::FGPUReadbackManager(TSharedPtr<FrameBuffer> InFrameBuffer)
    : MyFrameBuffer(InFrameBuffer)
{
}

void FGPUReadbackManager::RequestReadback(UTextureRenderTarget2D* RenderTarget, uint32 FrameNumber)
{
    if (!RenderTarget || bShuttingDown.Load()) return;
    
    FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource) return;
    
    // 단순화된 동기식 읽기
    TArray<FColor> Pixels;
    if (Resource->ReadPixels(Pixels))
    {
        // 프레임 버퍼에 추가
        FrameBuffer::Frame Frame;
        Frame.FrameNumber = FrameNumber;
        Frame.Timestamp = FPlatformTime::Seconds();
        Frame.Width = RenderTarget->SizeX;
        Frame.Height = RenderTarget->SizeY;
        Frame.Data.SetNum(Pixels.Num() * sizeof(FColor));
        FMemory::Memcpy(Frame.Data.GetData(), Pixels.GetData(), Frame.Data.Num());
        
        if (MyFrameBuffer) {
            MyFrameBuffer->SetFrame(MoveTemp(Frame));
        }
    }
}

void FGPUReadbackManager::Shutdown()
{
    bShuttingDown.Store(true);
}

// ================================================================================
// USRTStreamComponent Implementation
// ================================================================================

USRTStreamComponent::USRTStreamComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    bAutoActivate = true;
    
    // 프레임 버퍼 시스템 초기화
    FrameBuffer = MakeShared<::FrameBuffer>();
    GPUReadbackManager = MakeShared<FGPUReadbackManager>(FrameBuffer);
    
    // Phase 3: 새로운 인코더 및 멀티플렉서 초기화
    VideoEncoder = MakeUnique<FSRTVideoEncoder>();
    TransportStream = MakeUnique<FSRTTransportStream>();
}

// USRTStreamComponent 소멸자 구현
USRTStreamComponent::~USRTStreamComponent()
{
    // 소멸 시 동기적으로 정리 (크래시 방지)
    if (bIsStreaming || bCleanupInProgress || WorkerThread)
    {
        bStopRequested = true;
        bIsStreaming = false;
        
        if (StreamWorker.IsValid())
        {
            StreamWorker->Stop();
        }
        
        if (WorkerThread)
        {
            WorkerThread->Kill(true);  // 강제 종료
            delete WorkerThread;
            WorkerThread = nullptr;
        }
        
        StreamWorker.Reset();
    }
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

void USRTStreamComponent::BeginDestroy()
{
    // 컴포넌트 파괴 전 확실한 정리
    StopStreaming();
    
    // 정리 완료 대기
    while (bCleanupInProgress)
    {
        FPlatformProcess::Sleep(0.1f);
    }
    
    Super::BeginDestroy();
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
    FScopeLock Lock(&CleanupMutex);
    
    // 이미 스트리밍 중
    if (bIsStreaming)
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("Already streaming"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, TEXT("Already streaming"));
        }
        return;
    }
    
    // 정리 중인 경우
    if (bCleanupInProgress)
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("Cleanup in progress, please wait..."));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, 
                TEXT("Cleanup in progress, please wait..."));
        }
        return;
    }
    
    // 이전 리소스가 남아있는 경우 강제 정리
    if (StreamWorker.IsValid() || WorkerThread)
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("Previous resources detected, cleaning..."));
        
        if (WorkerThread)
        {
            WorkerThread->Kill(true);
            delete WorkerThread;
            WorkerThread = nullptr;
        }
        StreamWorker.Reset();
        
        // 메모리 정리 대기
        FPlatformProcess::Sleep(0.1f);
    }
    
    // 프레임 버퍼 재초기화
    if (!FrameBuffer)
    {
        FrameBuffer = MakeShared<::FrameBuffer>();
    }
    else
    {
        FrameBuffer->Clear();
    }
    
    if (!GPUReadbackManager)
    {
        GPUReadbackManager = MakeShared<FGPUReadbackManager>(FrameBuffer);
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("=== Starting SRT Stream ==="));
    // 시스템 정보 출력 및 호환성 체크
    SRTNetwork::SystemInfo sysInfo;
    if (SRTNetwork::GetSystemInfo(sysInfo))
    {
        UE_LOG(LogCineSRTStream, Log, TEXT("System Information:"));
        UE_LOG(LogCineSRTStream, Log, TEXT("  SRT Version: %s (Build: %s)"), *sysInfo.SRTVersion.FullVersion, *sysInfo.SRTVersion.BuildInfo);
        UE_LOG(LogCineSRTStream, Log, TEXT("  Platform: %s"), *sysInfo.Platform);
        UE_LOG(LogCineSRTStream, Log, TEXT("  Encryption: %s"), sysInfo.bEncryptionSupported ? TEXT("Supported") : TEXT("Not Supported"));
        UE_LOG(LogCineSRTStream, Log, TEXT("  Build Date: %s"), *sysInfo.BuildDate);
        if (!sysInfo.SRTVersion.bIsCompatible)
        {
            SetConnectionState(ESRTConnectionState::Error, FString::Printf(TEXT("SRT version %s is not compatible. Minimum required: 1.4.0"), *sysInfo.SRTVersion.FullVersion));
            return;
        }
    }
    UE_LOG(LogCineSRTStream, Log, TEXT("Target: %s:%d"), *StreamIP, StreamPort);
    UE_LOG(LogCineSRTStream, Log, TEXT("Mode: %s"), bCallerMode ? TEXT("Caller (Client)") : TEXT("Listener (Server)"));
    
    SetConnectionState(ESRTConnectionState::Connecting, TEXT("Initializing capture..."));
    
    // Phase 3: 새로운 인코더 및 멀티플렉서 초기화
    if (VideoEncoder && TransportStream)
    {
        // 비디오 인코더 설정
        FSRTVideoEncoder::FConfig EncoderConfig;
        GetResolution(EncoderConfig.Width, EncoderConfig.Height);
        EncoderConfig.FrameRate = StreamFPS;
        EncoderConfig.BitrateKbps = BitrateKbps;
        EncoderConfig.GOPSize = 30;
        EncoderConfig.Preset = TEXT("ultrafast");
        EncoderConfig.Tune = TEXT("zerolatency");
        EncoderConfig.bUseHardwareAcceleration = bUseHardwareAcceleration;
        
        if (!VideoEncoder->Initialize(EncoderConfig))
        {
            SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to initialize video encoder"));
            return;
        }
        
        // Transport Stream 설정
        FSRTTransportStream::FConfig TSConfig;
        TSConfig.ServiceID = 1;
        TSConfig.VideoPID = 0x0100;
        TSConfig.PCRPID = 0x0100;
        TSConfig.ServiceName = TEXT("UnrealStream");
        TSConfig.ProviderName = TEXT("CineSRT");
        
        if (!TransportStream->Initialize(TSConfig))
        {
            SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to initialize transport stream"));
            return;
        }
        
        UE_LOG(LogCineSRTStream, Log, TEXT("Phase 3 components initialized: %dx%d, %d fps, %d kbps"),
            EncoderConfig.Width, EncoderConfig.Height, EncoderConfig.FrameRate, EncoderConfig.BitrateKbps);
    }
    
    // Scene capture 설정
    if (!SetupSceneCapture())
    {
        SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to setup scene capture"));
        return;
    }
    
    // 플래그 초기화
    bStopRequested = false;
    bIsStreaming = true;
    
    // 워커 스레드 생성
    StreamWorker = MakeUnique<FSRTStreamWorker>(this);
    WorkerThread = FRunnableThread::Create(StreamWorker.Get(), TEXT("SRTStreamWorker"));
    
    if (!WorkerThread)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create worker thread"));
        bIsStreaming = false;
        CleanupSceneCapture();
        SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to create worker thread"));
        StreamWorker.Reset();
        return;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT streaming started"));
}

void USRTStreamComponent::StopStreaming()
{
    if (!bIsStreaming && !bCleanupInProgress)
        return;
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Stopping SRT stream..."));
    
    // 즉시 상태 변경
    bStopRequested = true;
    bIsStreaming = false;
    bCleanupInProgress = true;
    
    SetConnectionState(ESRTConnectionState::Disconnected, TEXT("Stopping..."));
    
    // 워커에게 종료 신호
    if (StreamWorker.IsValid())
    {
        StreamWorker->Stop();
    }
    
    // 워커 스레드 종료 대기
    if (WorkerThread)
    {
        // 간단한 방법 사용
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    
    // 리소스 정리
    StreamWorker.Reset();
    
    if (GPUReadbackManager)
    {
        GPUReadbackManager->Shutdown();
    }
    
    if (FrameBuffer)
    {
        FrameBuffer->Clear();
    }
    
    if (VideoEncoder)
    {
        VideoEncoder->Shutdown();
    }
    
    if (TransportStream)
    {
        TransportStream->Shutdown();
    }
    
    CleanupSceneCapture();
    
    // 통계 초기화
    CurrentBitrateKbps = 0.0f;
    TotalFramesSent = 0;
    DroppedFrames = 0;
    RoundTripTimeMs = 0.0f;
    
    bCleanupInProgress = false;
    SetConnectionState(ESRTConnectionState::Disconnected, TEXT("Stopped"));
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT stream stopped"));
}

void USRTStreamComponent::TestConnection()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("=== Testing SRT Connection ==="));
    
    void* testSock = SRTNetwork::CreateSocket();
    if (!testSock)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create test socket"));
        return;
    }
    
    // Test encryption
    if (bUseEncryption)
    {
        int pbkeylen = EncryptionKeyLength;
        if (SRTNetwork::SetSocketOption(testSock, SRTNetwork::OPT_PBKEYLEN, &pbkeylen, sizeof(pbkeylen)))
        {
            UE_LOG(LogCineSRTStream, Log, TEXT("✅ Encryption test passed (%d-bit AES)"), pbkeylen * 8);
        }
        else
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("❌ Encryption test failed"));
        }
    }
    
    SRTNetwork::CloseSocket(testSock);
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
}

void USRTStreamComponent::CaptureFrame()
{
    // 디버그 카운터 추가
    static int CaptureCount = 0;
    CaptureCount++;
    
    if (CaptureCount % 30 == 0) // 1초마다 로그
    {
        UE_LOG(LogCineSRTStream, Log, TEXT("CaptureFrame called %d times"), CaptureCount);
    }
    
    // 컴포넌트 상태 확인
    if (!SceneCapture)
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("CaptureFrame: SceneCapture is null"));
        return;
    }
    
    if (!RenderTarget)
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("CaptureFrame: RenderTarget is null"));
        return;
    }
    
    // Capture the scene
    SceneCapture->CaptureScene();
    
    // GPU readback 요청
    if (GPUReadbackManager)
    {
        GPUReadbackManager->RequestReadback(RenderTarget, TotalFramesSent);
        
        if (CaptureCount % 30 == 0)
        {
            UE_LOG(LogCineSRTStream, Log, TEXT("GPU readback requested for frame #%d"), TotalFramesSent);
        }
    }
    else
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("CaptureFrame: GPUReadbackManager is null"));
    }
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
// FSRTStreamWorker Implementation - 모든 SRT 직접 호출 제거!
// ================================================================================

FSRTStreamWorker::FSRTStreamWorker(USRTStreamComponent* InOwner)
    : Owner(InOwner)
    , bShouldExit(false)
{
}

FSRTStreamWorker::~FSRTStreamWorker()
{
    CleanupSRT();
}

void FSRTStreamWorker::ForceCloseSocket()
{
    FScopeLock Lock(&SocketLock);
    if (SRTSocket)
    {
        SRTNetwork::CloseSocket(SRTSocket);
        SRTSocket = nullptr;
    }
}

bool FSRTStreamWorker::Init()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread initializing..."));
    return InitializeSRT();
}

uint32 FSRTStreamWorker::Run()
{
    // ... 초기화 코드 ...
    
    const double FrameInterval = 1.0 / Owner->StreamFPS;
    double LastFrameTime = FPlatformTime::Seconds();
    double LastStatsTime = LastFrameTime;
    
    // 메인 루프 - bShouldExit 체크 추가
    while (!bShouldExit && Owner && !Owner->bStopRequested)
    {
        // 종료 체크를 더 자주
        if (bShouldExit)
        {
            UE_LOG(LogCineSRTStream, Log, TEXT("Worker detected exit signal"));
            break;
        }
        
        // Owner 체크
        if (!Owner || !IsValid(Owner))
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Owner became invalid during execution"));
            break;
        }
        
        double CurrentTime = FPlatformTime::Seconds();
        
        // 프레임 전송
        if (CurrentTime - LastFrameTime >= FrameInterval)
        {
            if (Owner->FrameBuffer && Owner->FrameBuffer->HasNewFrame())
            {
                FScopeLock Lock(&SocketLock);
                if (SRTSocket && !bShouldExit)  // 다시 체크
                {
                    if (!SendFrameData())
                    {
                        Owner->DroppedFrames++;
                        
                        // 연결이 끊어졌는지 확인
                        if (!SRTSocket)
                        {
                            UE_LOG(LogCineSRTStream, Warning, TEXT("Connection lost"));
                            break;
                        }
                    }
                }
            }
            LastFrameTime = CurrentTime;
        }
        
        // 통계 업데이트
        if (CurrentTime - LastStatsTime >= 1.0)
        {
            FScopeLock Lock(&SocketLock);
            if (SRTSocket && !bShouldExit)
            {
                UpdateSRTStats();
            }
            LastStatsTime = CurrentTime;
        }
        
        // CPU 사용률 감소를 위한 짧은 대기
        FPlatformProcess::Sleep(0.001f);
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread ending (exit: %s, stop: %s)"),
        bShouldExit ? TEXT("true") : TEXT("false"),
        (Owner && Owner->bStopRequested) ? TEXT("true") : TEXT("false"));
    
    return 0;
}

void FSRTStreamWorker::Stop()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("Worker Stop() called"));
    
    // 종료 플래그 설정
    bShouldExit = true;
    
    // 소켓을 즉시 닫아서 블로킹된 Send/Recv 해제
    FScopeLock Lock(&SocketLock);
    if (SRTSocket)
    {
        UE_LOG(LogCineSRTStream, Log, TEXT("Closing SRT socket immediately"));
        
        // 소켓 핸들 백업 후 null로 설정
        void* socketToClose = SRTSocket;
        SRTSocket = nullptr;
        
        // 소켓 닫기 - 이렇게 하면 진행 중인 Send/Recv가 즉시 실패
        SRTNetwork::CloseSocket(socketToClose);
        
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT socket closed"));
    }
}

void FSRTStreamWorker::Exit()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("Worker Exit() called"));
    
    // 혹시 남아있는 소켓 정리
    FScopeLock Lock(&SocketLock);
    if (SRTSocket)
    {
        SRTNetwork::CloseSocket(SRTSocket);
        SRTSocket = nullptr;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("Worker thread exiting"));
}

bool FSRTStreamWorker::InitializeSRT()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("Creating SRT socket..."));
    void* sock = SRTNetwork::CreateSocket();
    if (!sock)
    {
        Owner->SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to create SRT socket"));
        return false;
    }
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT socket created successfully"));
    
    // 최소한의 SRT 설정만 사용
    UE_LOG(LogCineSRTStream, Log, TEXT("Using minimal SRT options..."));
    
    // ⭐ 스트림 모드 설정 (메시지 모드 OFF)
    int messageapi = 0;  // 0 = stream mode
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_MESSAGEAPI, &messageapi, sizeof(messageapi));
    
    // 기본 전송 모드만 설정
    int live_mode = SRTNetwork::TRANSTYPE_LIVE;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_TRANSTYPE, &live_mode, sizeof(live_mode));
    
    // 성능 최적화 옵션 (권장)
    int latency = Owner->LatencyMs;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_LATENCY, &latency, sizeof(latency));
    
    int sndbuf = 64 * 1024 * 1024; // 64MB 송신 버퍼 (대폭 증가)
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    // ⭐ Flow Control 윈도우 크기 증가
    int fc = 50000;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_FC, &fc, sizeof(fc));
    
    // ⭐ 암호화 관련 코드 모두 삭제!
    
    // Connect or bind
    if (!Owner->bCallerMode)
    {
        // Listener 모드
        if (!SRTNetwork::Bind(sock, Owner->StreamPort))
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            Owner->SetConnectionState(ESRTConnectionState::Error, FString::Printf(TEXT("Bind failed: %s"), *Error));
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        if (!SRTNetwork::Listen(sock, 1))
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            Owner->SetConnectionState(ESRTConnectionState::Error, FString::Printf(TEXT("Listen failed: %s"), *Error));
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        Owner->SetConnectionState(ESRTConnectionState::Connecting, FString::Printf(TEXT("Listening on port %d..."), Owner->StreamPort));
        void* client = nullptr;
        while (!bShouldExit && !Owner->bStopRequested)
        {
            client = SRTNetwork::AcceptWithTimeout(sock, 100);
            if (client)
            {
                break;
            }
            if (bShouldExit || Owner->bStopRequested)
            {
                SRTNetwork::CloseSocket(sock);
                return false;
            }
        }
        if (!client)
        {
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        SRTNetwork::CloseSocket(sock);
        SRTSocket = client;
        Owner->SetConnectionState(ESRTConnectionState::Connected, TEXT("Client connected"));
    }
    else
    {
        Owner->SetConnectionState(ESRTConnectionState::Connecting, FString::Printf(TEXT("Connecting to %s:%d..."), *Owner->StreamIP, Owner->StreamPort));
        if (!SRTNetwork::Connect(sock, TCHAR_TO_UTF8(*Owner->StreamIP), Owner->StreamPort))
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            Owner->SetConnectionState(ESRTConnectionState::Error, FString::Printf(TEXT("Connection failed: %s"), *Error));
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        SRTSocket = sock;
        Owner->SetConnectionState(ESRTConnectionState::Connected, TEXT("Connected successfully"));
    }
    Owner->SetConnectionState(ESRTConnectionState::Streaming, TEXT("Streaming active"));
    return true;
}

void FSRTStreamWorker::CleanupSRT()
{
    if (SRTSocket)
    {
        SRTNetwork::CloseSocket(SRTSocket);
        SRTSocket = nullptr;
    }
}

bool FSRTStreamWorker::SendFrameData()
{
    if (!SRTSocket || !Owner || !Owner->FrameBuffer)
        return false;
    
    FrameBuffer::Frame Frame;
    if (!Owner->FrameBuffer->GetFrame(Frame))
    {
        // 로그 추가
        static int NoFrameCount = 0;
        if (++NoFrameCount % 30 == 0) // 1초마다 한 번
        {
            UE_LOG(LogCineSRTStream, Warning, TEXT("No new frame available (count: %d)"), NoFrameCount);
        }
        return false;
    }
    
    // Phase 3: 새로운 인코더 및 멀티플렉서 사용
    if (Owner->VideoEncoder && Owner->TransportStream)
    {
        UE_LOG(LogCineSRTStream, VeryVerbose, TEXT("Processing frame #%d: %dx%d"), 
            Frame.FrameNumber, Frame.Width, Frame.Height);
        
        // BGRA 데이터를 FColor 배열로 변환
        TArray<FColor> BGRAData;
        BGRAData.SetNum(Frame.Width * Frame.Height);
        FMemory::Memcpy(BGRAData.GetData(), Frame.Data.GetData(), Frame.Data.Num());
        
        // H.264 인코딩
        FEncodedFrame EncodedFrame;
        if (!Owner->VideoEncoder->EncodeFrame(BGRAData, EncodedFrame))
        {
            UE_LOG(LogCineSRTStream, Warning, TEXT("Failed to encode frame #%d"), Frame.FrameNumber);
            return false;
        }
        
        UE_LOG(LogCineSRTStream, VeryVerbose, TEXT("Encoded frame #%d: %d bytes, %s"), 
            EncodedFrame.FrameNumber, 
            EncodedFrame.Data.Num(),
            EncodedFrame.bKeyFrame ? TEXT("KEY") : TEXT("DELTA"));
        
        // MPEG-TS 멀티플렉싱
        TArray<uint8> TSPackets;
        if (!Owner->TransportStream->MuxH264Frame(
            EncodedFrame.Data,
            EncodedFrame.PTS,
            EncodedFrame.DTS,
            EncodedFrame.bKeyFrame,
            TSPackets))
        {
            UE_LOG(LogCineSRTStream, Warning, TEXT("Failed to mux H.264 frame"));
            return false;
        }
        
        UE_LOG(LogCineSRTStream, VeryVerbose, TEXT("Generated %d TS packets (%d bytes)"), 
            TSPackets.Num() / 188, TSPackets.Num());
        
        // TS 패킷 전송
        const int ChunkSize = 1316; // SRT 최적 청크 크기 (7 TS packets)
        const uint8* DataPtr = TSPackets.GetData();
        int32 TotalSize = TSPackets.Num();
        int32 BytesSent = 0;
        
        while (BytesSent < TotalSize && !Owner->bStopRequested && !bShouldExit)
        {
            int32 ToSend = FMath::Min(ChunkSize, TotalSize - BytesSent);
            int sent = SRTNetwork::Send(SRTSocket, (char*)(DataPtr + BytesSent), ToSend);
            
            if (sent < 0)
            {
                const char* error = SRTNetwork::GetLastError();
                UE_LOG(LogCineSRTStream, Error, TEXT("Send failed: %s"), UTF8_TO_TCHAR(error));
                return false;
            }
            BytesSent += sent;
        }
        
        if (BytesSent == TotalSize)
        {
            Owner->TotalFramesSent++;
            
            // 매 30프레임마다 상태 출력
            if (Owner->TotalFramesSent % 30 == 0)
            {
                UE_LOG(LogCineSRTStream, Log, TEXT("Streaming status: %d frames sent, %d bytes total"), 
                    Owner->TotalFramesSent, BytesSent);
            }
            
            return true;
        }
    }
    else
    {
        UE_LOG(LogCineSRTStream, Warning, TEXT("VideoEncoder or TransportStream is null"));
    }
    
    return false;
}

void FSRTStreamWorker::UpdateSRTStats()
{
    if (!SRTSocket)
        return;
    
    SRTNetwork::Stats stats;
    if (SRTNetwork::GetStats(SRTSocket, stats))
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

void FSRTStreamWorker::HandleDisconnection()
{
    Owner->SetConnectionState(ESRTConnectionState::Error, TEXT("Connection lost"));
    
    // 재연결 시도 (선택적)
    if (Owner->bAutoReconnect && !bShouldExit) {
        UE_LOG(LogCineSRTStream, Log, TEXT("Waiting 2 seconds before reconnect..."));
        FPlatformProcess::Sleep(2.0f); // 2초 대기
        
        if (!bShouldExit) {
            UE_LOG(LogCineSRTStream, Log, TEXT("Attempting to reconnect..."));
            InitializeSRT();
        }
    }
}

void FSRTStreamWorker::CheckHealth()
{
    static double LastHealthCheck = 0.0;
    double CurrentTime = FPlatformTime::Seconds();
    
    if (CurrentTime - LastHealthCheck >= 5.0) // 5초마다 체크
    {
        LastHealthCheck = CurrentTime;
        
        // 버퍼 상태 로깅
        bool HasFrame = Owner->FrameBuffer ? Owner->FrameBuffer->HasNewFrame() : false;
        UE_LOG(LogCineSRTStream, VeryVerbose, TEXT("Health Check: Has new frame = %s"), 
            HasFrame ? TEXT("true") : TEXT("false"));
    }
}

void FSRTStreamWorker::CleanupConnection()
{
    if (SRTSocket) {
        SRTNetwork::CloseSocket(SRTSocket);
        SRTSocket = nullptr;
    }
} 