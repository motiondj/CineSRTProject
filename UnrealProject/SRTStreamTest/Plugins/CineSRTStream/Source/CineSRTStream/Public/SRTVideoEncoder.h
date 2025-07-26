#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/CircularQueue.h"
#include "HAL/CriticalSection.h"

// FFmpeg 전방 선언
extern "C" {
    struct AVCodec;
    struct AVCodecContext;
    struct AVFrame;
    struct AVPacket;
    struct SwsContext;
    struct AVBufferRef;
}

// 인코딩된 프레임 데이터
struct FEncodedFrame
{
    TArray<uint8> Data;
    int64 PTS;
    int64 DTS;
    bool bKeyFrame;
    uint32 FrameNumber;
};

class CINESRTSTREAM_API FSRTVideoEncoder
{
public:
    struct FConfig
    {
        // 비디오 설정
        int32 Width = 1920;
        int32 Height = 1080;
        int32 FrameRate = 30;
        int32 GOPSize = 30;  // 키프레임 간격
        
        // 비트레이트 설정
        int32 BitrateKbps = 5000;
        int32 MaxBitrateKbps = 8000;
        int32 BufferSizeKb = 2000;
        
        // 인코더 설정
        FString Preset = TEXT("ultrafast");  // ultrafast, superfast, veryfast, faster, fast
        FString Tune = TEXT("zerolatency");  // zerolatency, film, animation
        FString Profile = TEXT("baseline");  // baseline, main, high
        int32 Level = 41;  // 4.1 = 1080p30
        
        // 하드웨어 가속
        bool bUseHardwareAcceleration = true;  // 기본값이 true!
        FString HWAccelType = TEXT("nvenc");  // nvenc, qsv, amf
        
        // 고급 설정
        int32 ThreadCount = 4;
        bool bUseCBR = false;  // CBR vs VBR
        float CRF = 23.0f;  // Constant Rate Factor (VBR용)
    };

    FSRTVideoEncoder();
    ~FSRTVideoEncoder();

    // 초기화/종료
    bool Initialize(const FConfig& InConfig);
    void Shutdown();
    
    // 인코딩
    bool EncodeFrame(const TArray<FColor>& BGRAData, FEncodedFrame& OutFrame);
    bool EncodeFrameAsync(const TArray<FColor>& BGRAData);
    bool GetEncodedFrame(FEncodedFrame& OutFrame);
    
    // 상태 확인
    bool IsInitialized() const { return bIsInitialized; }
    bool HasEncodedFrames() const { return !EncodedFrameQueue.IsEmpty(); }
    
    // 통계
    float GetLastEncodingTimeMs() const { return LastEncodingTimeMs; }
    int32 GetEncodedFrameCount() const { return EncodedFrameCount; }
    int32 GetDroppedFrameCount() const { return DroppedFrameCount; }
    float GetAverageBitrateKbps() const;
    
    // 동적 설정 변경
    bool SetBitrate(int32 NewBitrateKbps);
    bool ForceKeyFrame();

private:
    FConfig Config;
    FThreadSafeBool bIsInitialized;
    
    // 스레드 안전성
    mutable FCriticalSection EncoderLock;
    
    // FFmpeg 객체
    const AVCodec* Codec = nullptr;  // const 추가!
    AVCodecContext* CodecContext = nullptr;
    AVFrame* Frame = nullptr;
    AVPacket* Packet = nullptr;
    SwsContext* SwsContext = nullptr;
    AVBufferRef* HWDeviceContext = nullptr;
    
    // 버퍼 관리
    TQueue<FEncodedFrame> EncodedFrameQueue;  // TCircularQueue가 아님!
    FCriticalSection QueueLock;
    
    // 통계
    TAtomic<float> LastEncodingTimeMs;
    TAtomic<int32> EncodedFrameCount;
    TAtomic<int32> DroppedFrameCount;
    TAtomic<int64> TotalEncodedBytes;
    
    // 내부 메서드
    bool CheckFFmpegInstallation();
    bool InitializeSoftwareEncoder();
    bool InitializeHardwareEncoder();
    bool SetupCodecContext();
    bool ConvertAndEncode(const TArray<FColor>& BGRAData);
    void LogCodecInfo();
    
    // 하드웨어 가속 헬퍼
    bool InitializeNVENC();
    bool InitializeQuickSync();
    bool InitializeAMF();
}; 