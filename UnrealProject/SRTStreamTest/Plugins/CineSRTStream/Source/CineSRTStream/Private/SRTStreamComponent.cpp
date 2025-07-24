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

// C++ 헤더들을 먼저 포함
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

// SRT 헤더 래퍼 사용
#include "SRTWrapper.h"

// FSRTData 클래스는 이제 헤더에서 완전히 정의됨

// ================================================================================
// USRTStreamComponent Implementation
// ================================================================================

USRTStreamComponent::USRTStreamComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    bAutoActivate = true;
    
    // Initialize SRT data
    SRTData = std::make_unique<FSRTData>();
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
    
    // Test SRT socket creation using wrapper
    auto testSocket = std::make_unique<SRTWrapper::SRTSocket>();
    if (!testSocket->Create())
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create test socket: %s"), 
               *FString(testSocket->GetLastErrorString()));
        return;
    }
    
    // Test encryption
    if (bUseEncryption)
    {
        int pbkeylen = EncryptionKeyLength;
        if (testSocket->SetOption(SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen)))
        {
            UE_LOG(LogCineSRTStream, Log, TEXT("✅ Encryption test passed (%d-bit AES)"), pbkeylen * 8);
        }
        else
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("❌ Encryption test failed"));
        }
    }
    
    testSocket->Close();
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
    // Create socket using wrapper
    auto socket = std::make_unique<SRTWrapper::SRTSocket>();
    if (!socket->Create())
    {
        Owner->SetConnectionState(ESRTConnectionState::Error, 
            FString::Printf(TEXT("Failed to create SRT socket: %s"), 
            *FString(socket->GetLastErrorString())));
        return false;
    }
    
    // Configure socket options
    int yes = 1;
    int live_mode = SRTT_LIVE;
    
    socket->SetOption(SRTO_TRANSTYPE, &live_mode, sizeof(live_mode));
    
    // Set mode
    if (Owner->bCallerMode)
    {
        socket->SetOption(SRTO_SENDER, &yes, sizeof(yes));
    }
    
    // Stream ID
    if (!Owner->StreamID.IsEmpty())
    {
        std::string streamId = TCHAR_TO_UTF8(*Owner->StreamID);
        socket->SetOption(SRTO_STREAMID, streamId.c_str(), streamId.length());
    }
    
    // Encryption
    if (Owner->bUseEncryption)
    {
        int pbkeylen = Owner->EncryptionKeyLength;
        socket->SetOption(SRTO_PBKEYLEN, &pbkeylen, sizeof(pbkeylen));
        
        if (!Owner->EncryptionPassphrase.IsEmpty())
        {
            std::string passphrase = TCHAR_TO_UTF8(*Owner->EncryptionPassphrase);
            socket->SetOption(SRTO_PASSPHRASE, passphrase.c_str(), passphrase.length());
        }
    }
    
    // Performance options
    int latency = Owner->LatencyMs;
    socket->SetOption(SRTO_LATENCY, &latency, sizeof(latency));
    
    int mss = 1500;
    socket->SetOption(SRTO_MSS, &mss, sizeof(mss));
    
    int fc = 25600;
    socket->SetOption(SRTO_FC, &fc, sizeof(fc));
    
    int sndbuf = 12058624; // 12MB
    socket->SetOption(SRTO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    if (Owner->bCallerMode)
    {
        // Caller mode - connect to server
        Owner->SetConnectionState(ESRTConnectionState::Connecting, 
            FString::Printf(TEXT("Connecting to %s:%d..."), *Owner->StreamIP, Owner->StreamPort));
        
        std::string ipStr = TCHAR_TO_UTF8(*Owner->StreamIP);
        if (!socket->Connect(ipStr.c_str(), Owner->StreamPort))
        {
                    Owner->SetConnectionState(ESRTConnectionState::Error, 
            FString::Printf(TEXT("Connection failed: %s"), 
            *FString(socket->GetLastErrorString())));
            return false;
        }
        
        SRTSocket = socket.release();
        Owner->SetConnectionState(ESRTConnectionState::Connected, TEXT("Connected successfully"));
    }
    else
    {
        // Listener mode - wait for connection
        if (!socket->Bind("0.0.0.0", Owner->StreamPort))
        {
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Bind failed: %s"), 
                *FString(socket->GetLastErrorString())));
            return false;
        }
        
        if (!socket->Listen(1))
        {
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Listen failed: %s"), 
                *FString(socket->GetLastErrorString())));
            return false;
        }
        
        Owner->SetConnectionState(ESRTConnectionState::Connecting, 
            FString::Printf(TEXT("Listening on port %d..."), Owner->StreamPort));
        
        // Accept connection
        auto clientSocket = socket->Accept();
        if (!clientSocket)
        {
            Owner->SetConnectionState(ESRTConnectionState::Error, 
                FString::Printf(TEXT("Accept failed: %s"), 
                *FString(socket->GetLastErrorString())));
            return false;
        }
        
        SRTSocket = clientSocket;
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
        auto socket = static_cast<SRTWrapper::SRTSocket*>(SRTSocket);
        socket->Close();
        delete socket;
        SRTSocket = nullptr;
    }
}

bool FSRTStreamWorker::SendFrameData()
{
    if (!SRTSocket || !Owner)
        return false;
    
    auto socket = static_cast<SRTWrapper::SRTSocket*>(SRTSocket);
    
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
    int sent = socket->Send((char*)&Header, sizeof(Header));
    if (sent < 0)
    {
        if (!Owner->bStopRequested)
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send header: %s"), 
                   *FString(socket->GetLastErrorString()));
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
        sent = socket->Send((char*)(DataPtr + BytesSent), ToSend);
        
        if (sent < 0)
        {
            if (!Owner->bStopRequested)
            {
                UE_LOG(LogCineSRTStream, Error, TEXT("Failed to send data: %s"), 
                       *FString(socket->GetLastErrorString()));
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
    
    auto socket = static_cast<SRTWrapper::SRTSocket*>(SRTSocket);
    
    // SRT 통계 정보 가져오기
    SRTStats stats;
    if (SRT_C_GetStats(socket->GetSocketHandle(), &stats, 1) == 0) // 1 = clear after reading
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