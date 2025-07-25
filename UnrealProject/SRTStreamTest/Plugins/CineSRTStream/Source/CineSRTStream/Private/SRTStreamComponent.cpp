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
    
    SetConnectionState(ESRTConnectionState::Disconnected, TEXT("Cleaning up..."));
    
    // 비동기 정리 (메인 스레드 블로킹 방지)
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
    {
        // 워커 종료 신호
        if (StreamWorker.IsValid())
        {
            StreamWorker->Stop();
        }
        
        // 워커 스레드 정리 (최대 3초 대기)
        if (WorkerThread)
        {
            double StartTime = FPlatformTime::Seconds();
            const double MaxWaitTime = 3.0;
            
            while (WorkerThread->GetThreadID() != 0)
            {
                if (FPlatformTime::Seconds() - StartTime > MaxWaitTime)
                {
                    UE_LOG(LogCineSRTStream, Warning, TEXT("Worker thread timeout, forcing termination"));
                    WorkerThread->Kill(true);
                    break;
                }
                FPlatformProcess::Sleep(0.05f);
            }
            
            delete WorkerThread;
            WorkerThread = nullptr;
        }
        
        // 게임 스레드에서 최종 정리
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            FScopeLock Lock(&CleanupMutex);
            
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
            
            CleanupSceneCapture();
            
            // 통계 초기화
            CurrentBitrateKbps = 0.0f;
            TotalFramesSent = 0;
            DroppedFrames = 0;
            RoundTripTimeMs = 0.0f;
            
            // 정리 완료
            bCleanupInProgress = false;
            SetConnectionState(ESRTConnectionState::Disconnected, TEXT("Ready"));
            
            UE_LOG(LogCineSRTStream, Log, TEXT("SRT cleanup completed - Ready for restart"));
            
            // 화면에 메시지 표시
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, 
                    TEXT("SRT Stream cleanup completed - Ready"));
            }
        });
    });
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
    if (!SceneCapture || !RenderTarget)
        return;
    
    // Capture the scene
    SceneCapture->CaptureScene();
    
    // Request GPU readback
    if (GPUReadbackManager)
    {
        GPUReadbackManager->RequestReadback(RenderTarget, TotalFramesSent);
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
    // Owner 검증
    if (!Owner)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Worker started with null Owner!"));
        return 1;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread started"));
    
    // Owner가 유효한지 다시 확인
    if (!IsValid(Owner))
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Owner is not valid!"));
        return 1;
    }
    
    const double FrameInterval = 1.0 / Owner->StreamFPS;
    double LastFrameTime = FPlatformTime::Seconds();
    double LastStatsTime = LastFrameTime;
    
    while (!bShouldExit && Owner && !Owner->bStopRequested)
    {
        // 매 루프마다 Owner 체크
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
                if (SRTSocket && !bShouldExit)
                {
                    if (SendFrameData())
                    {
                        Owner->TotalFramesSent++;
                    }
                    else
                    {
                        Owner->DroppedFrames++;
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
        
        FPlatformProcess::Sleep(0.001f);
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread ending"));
    return 0;
}

void FSRTStreamWorker::Stop()
{
    bShouldExit = true;
    
    FScopeLock Lock(&SocketLock);
    if (SRTSocket)
    {
        SRTNetwork::CloseSocket(SRTSocket);
        SRTSocket = nullptr;
    }
}

void FSRTStreamWorker::Exit()
{
    CleanupSRT();
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
    // Configure socket options
    int yes = 1;
    int live_mode = SRTNetwork::TRANSTYPE_LIVE;
    UE_LOG(LogCineSRTStream, Log, TEXT("Setting socket options..."));
    
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_TRANSTYPE, &live_mode, sizeof(live_mode));
    
    if (Owner->bCallerMode)
    {
        SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_SENDER, &yes, sizeof(yes));
    }
    
    // Stream ID
    if (!Owner->StreamID.IsEmpty())
    {
        std::string streamId = TCHAR_TO_UTF8(*Owner->StreamID);
        SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_STREAMID, streamId.c_str(), streamId.length());
    }
    
    // Encryption
    if (Owner->bUseEncryption)
    {
        int pbkeylen = Owner->EncryptionKeyLength;
        SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_PBKEYLEN, &pbkeylen, sizeof(pbkeylen));
        
        if (!Owner->EncryptionPassphrase.IsEmpty())
        {
            std::string passphrase = TCHAR_TO_UTF8(*Owner->EncryptionPassphrase);
            SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_PASSPHRASE, passphrase.c_str(), passphrase.length());
        }
    }
    
    // Performance options
    int latency = Owner->LatencyMs;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_LATENCY, &latency, sizeof(latency));
    
    int mss = 1500;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_MSS, &mss, sizeof(mss));
    
    int fc = 25600;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_FC, &fc, sizeof(fc));
    
    // 송신 버퍼 크기 증가
    int sndbuf = 32 * 1024 * 1024; // 32MB
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_SNDBUF, &sndbuf, sizeof(sndbuf));
    // 송신 드롭 설정 (버퍼 가득 찰 때 오래된 패킷 드롭)
    int snddrop = 1;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_SNDDROPDELAY, &snddrop, sizeof(snddrop));
    
    // Send 타임아웃 추가 (1초)
    int sndtimeo = 1000;  // 1000ms = 1초
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
    
    // SRT 버전 호환성 설정 (1.3.0 이상 강제)
    int peerlatency = 120;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_PEERLATENCY, &peerlatency, sizeof(peerlatency));
    
    int peeridletimeo = 5000;
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_PEERIDLETIMEO, &peeridletimeo, sizeof(peeridletimeo));
    
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
        return false;
    struct FrameHeader
    {
        uint32 Magic = 0x53525446;
        uint32 Width;
        uint32 Height;
        uint32 PixelFormat = 1;
        uint32 DataSize;
        uint64 Timestamp;
        uint32 FrameNumber;
    };
    FrameHeader Header;
    Header.Width = Frame.Width;
    Header.Height = Frame.Height;
    Header.DataSize = Frame.Data.Num();
    Header.Timestamp = FPlatformTime::Cycles64();
    Header.FrameNumber = Frame.FrameNumber;
    int sent = SRTNetwork::Send(SRTSocket, (char*)&Header, sizeof(Header));
    if (sent < 0)
    {
        const char* error = SRTNetwork::GetLastError();
        // 6002(EAGAIN) 또는 'no buffer available' 메시지는 무시
        if (strstr(error, "6002") || strstr(error, "no buffer available"))
        {
            Owner->DroppedFrames++;
            return false;
        }
        if (!Owner->bStopRequested && !bShouldExit)
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Send failed: %s"), UTF8_TO_TCHAR(error));
        }
        return false;
    }
    const int ChunkSize = 1316;
    const uint8* DataPtr = Frame.Data.GetData();
    int32 TotalSize = Frame.Data.Num();
    int32 BytesSent = 0;
    while (BytesSent < TotalSize && !Owner->bStopRequested && !bShouldExit)
    {
        int32 ToSend = FMath::Min(ChunkSize, TotalSize - BytesSent);
        sent = SRTNetwork::Send(SRTSocket, (char*)(DataPtr + BytesSent), ToSend);
        if (sent < 0)
        {
            const char* error = SRTNetwork::GetLastError();
            if (strstr(error, "6002") || strstr(error, "no buffer available"))
            {
                Owner->DroppedFrames++;
                continue;
            }
            if (!Owner->bStopRequested && !bShouldExit)
            {
                UE_LOG(LogCineSRTStream, Error, TEXT("Send failed: %s"), UTF8_TO_TCHAR(error));
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
    if (Owner->bAutoReconnect) {
        FPlatformProcess::Sleep(1.0f);
        InitializeSRT();
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