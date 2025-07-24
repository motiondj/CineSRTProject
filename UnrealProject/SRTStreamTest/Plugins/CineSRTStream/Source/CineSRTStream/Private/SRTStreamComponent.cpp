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
    
    // 1. 먼저 플래그 설정
    bStopRequested = true;
    bIsStreaming = false;
    
    // 2. 워커의 소켓을 즉시 닫기 (블로킹 해제)
    if (StreamWorker.IsValid())
    {
        StreamWorker->ForceCloseSocket();
    }
    
    // 3. 스레드 종료 (기다리지 않음)
    if (WorkerThread)
    {
        WorkerThread->Kill(false);  // false = 기다리지 않고 즉시 종료
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    
    StreamWorker.Reset();
    
    // 4. 나머지 정리
    CleanupSceneCapture();
    
    // Reset stats
    CurrentBitrateKbps = 0.0f;
    TotalFramesSent = 0;
    DroppedFrames = 0;
    RoundTripTimeMs = 0.0f;
    
    SetConnectionState(ESRTConnectionState::Disconnected, TEXT("Stream stopped"));
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT stream stopped successfully"));
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
{
}

FSRTStreamWorker::~FSRTStreamWorker()
{
    CleanupSRT();
}

void FSRTStreamWorker::ForceCloseSocket()
{
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
    UE_LOG(LogCineSRTStream, Log, TEXT("SRT Worker thread started"));
    
    const double FrameInterval = 1.0 / Owner->StreamFPS;
    double LastFrameTime = FPlatformTime::Seconds();
    double LastStatsTime = LastFrameTime;
    
    while (!Owner->bStopRequested)
    {
        // 중단 요청 체크를 더 자주
        if (Owner->bStopRequested)
            break;
        
        double CurrentTime = FPlatformTime::Seconds();
        
        // Send frame if ready and timing is right
        if (CurrentTime - LastFrameTime >= FrameInterval)
        {
            if (Owner->FrameBuffer && Owner->FrameBuffer->HasNewFrame())
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
            LastFrameTime = CurrentTime;
        }
        
        // Update stats periodically
        if (CurrentTime - LastStatsTime >= 1.0)
        {
            UpdateSRTStats();
            LastStatsTime = CurrentTime;
        }
        
        // Health check
        CheckHealth();
        
        // Sleep을 조금 더 길게 (1ms -> 10ms)
        FPlatformProcess::Sleep(0.01f);
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
    // Create socket using SRTNetwork
    void* sock = SRTNetwork::CreateSocket();
    if (!sock)
    {
        Owner->SetConnectionState(ESRTConnectionState::Error, TEXT("Failed to create SRT socket"));
        return false;
    }
    
    // Configure socket options
    int yes = 1;
    int live_mode = SRTNetwork::TRANSTYPE_LIVE;
    
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
    
    int sndbuf = 12058624; // 12MB
    SRTNetwork::SetSocketOption(sock, SRTNetwork::OPT_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    // Connect or bind
    if (Owner->bCallerMode)
    {
        // Caller mode - connect to server
        Owner->SetConnectionState(ESRTConnectionState::Connecting, 
            FString::Printf(TEXT("Connecting to %s:%d..."), *Owner->StreamIP, Owner->StreamPort));
        
        if (!SRTNetwork::Connect(sock, TCHAR_TO_UTF8(*Owner->StreamIP), Owner->StreamPort))
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Connection failed: %s"), *Error));
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        
        SRTSocket = sock;
        Owner->SetConnectionState(ESRTConnectionState::Connected, TEXT("Connected successfully"));
    }
    else
    {
        // Listener mode - wait for connection
        if (!SRTNetwork::Bind(sock, Owner->StreamPort))
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Bind failed: %s"), *Error));
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        
        if (!SRTNetwork::Listen(sock, 1))
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Listen failed: %s"), *Error));
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        
        Owner->SetConnectionState(ESRTConnectionState::Connecting, 
            FString::Printf(TEXT("Listening on port %d..."), Owner->StreamPort));
        
        // Accept connection
        void* client = SRTNetwork::Accept(sock);
        
        if (!client)
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Accept failed: %s"), *Error));
            SRTNetwork::CloseSocket(sock);
            return false;
        }
        
        SRTNetwork::CloseSocket(sock); // Close listening socket
        SRTSocket = client;
        
        Owner->SetConnectionState(ESRTConnectionState::Connected, TEXT("Client connected"));
    }
    
    // Start streaming
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
    
    // Get frame from buffer
    FrameBuffer::Frame Frame;
    if (!Owner->FrameBuffer->GetFrame(Frame))
        return false;
    
    // Send frame data with simple header
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
    
    FrameHeader Header;
    Header.Width = Frame.Width;
    Header.Height = Frame.Height;
    Header.DataSize = Frame.Data.Num();
    Header.Timestamp = FPlatformTime::Cycles64();
    Header.FrameNumber = Frame.FrameNumber;
    
    // Send header
    int sent = SRTNetwork::Send(SRTSocket, (char*)&Header, sizeof(Header));
    if (sent < 0)
    {
        if (!Owner->bStopRequested)
        {
            FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send header: %s"), *Error);
        }
        return false;
    }
    
    // Send pixel data in chunks
    const int ChunkSize = 1316; // SRT recommended payload size
    const uint8* DataPtr = Frame.Data.GetData();
    int32 TotalSize = Frame.Data.Num();
    int32 BytesSent = 0;
    
    while (BytesSent < TotalSize && !Owner->bStopRequested)
    {
        int32 ToSend = FMath::Min(ChunkSize, TotalSize - BytesSent);
        sent = SRTNetwork::Send(SRTSocket, (char*)(DataPtr + BytesSent), ToSend);
        
        if (sent < 0)
        {
            if (!Owner->bStopRequested)
            {
                FString Error = UTF8_TO_TCHAR(SRTNetwork::GetLastError());
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