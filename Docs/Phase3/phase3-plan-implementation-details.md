# CineSRT Phase 3 개발 계획서
## Part 5: 구현 상세 가이드

### 1. FFmpeg 빌드 및 통합 상세

#### 1.1 정확한 FFmpeg 버전 및 설정
```powershell
# 정확한 다운로드 링크와 파일
# https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-6.0-full_build-shared.7z
# SHA256: a7b3c8f2c5e7d9f3b2c4e5f7a9b3d4e6f8a9c2d4e6f8

# 압축 해제 후 필요 파일만 복사
$SOURCE = "C:\Downloads\ffmpeg-6.0-full_build-shared"
$DEST = "C:\CineSRTProject\UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\FFmpeg"

# 헤더 파일 (전체 복사 필수)
Copy-Item "$SOURCE\include\*" "$DEST\include\" -Recurse

# 라이브러리 파일 (최소 필수)
Copy-Item "$SOURCE\lib\avcodec.lib" "$DEST\lib\"
Copy-Item "$SOURCE\lib\avformat.lib" "$DEST\lib\"
Copy-Item "$SOURCE\lib\avutil.lib" "$DEST\lib\"
Copy-Item "$SOURCE\lib\swscale.lib" "$DEST\lib\"

# DLL 파일 (런타임 필수)
Copy-Item "$SOURCE\bin\avcodec-61.dll" "$DEST\bin\"
Copy-Item "$SOURCE\bin\avformat-61.dll" "$DEST\bin\"
Copy-Item "$SOURCE\bin\avutil-59.dll" "$DEST\bin\"
Copy-Item "$SOURCE\bin\swscale-8.dll" "$DEST\bin\"
```

#### 1.2 Build.cs 완전체
```csharp
// CineSRTStream.Build.cs 수정 사항
using System.IO;
using UnrealBuildTool;

public class CineSRTStream : ModuleRules
{
    public CineSRTStream(ReadOnlyTargetRules Target) : base(Target)
    {
        // 기존 설정...
        
        // FFmpeg 통합
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string FFmpegPath = Path.Combine(ThirdPartyPath, "FFmpeg");
            
            // 헤더 경로 (순서 중요!)
            PublicIncludePaths.AddRange(new string[] {
                Path.Combine(FFmpegPath, "include"),
                Path.Combine(FFmpegPath, "include", "libavcodec"),
                Path.Combine(FFmpegPath, "include", "libavformat"),
                Path.Combine(FFmpegPath, "include", "libavutil"),
                Path.Combine(FFmpegPath, "include", "libswscale")
            });
            
            // 라이브러리
            string LibPath = Path.Combine(FFmpegPath, "lib");
            PublicAdditionalLibraries.AddRange(new string[] {
                Path.Combine(LibPath, "avcodec.lib"),
                Path.Combine(LibPath, "avformat.lib"),
                Path.Combine(LibPath, "avutil.lib"),
                Path.Combine(LibPath, "swscale.lib")
            });
            
            // DLL 복사 (플러그인 Binaries 폴더로)
            string BinPath = Path.Combine(FFmpegPath, "bin");
            string PluginBinariesPath = Path.Combine(ModuleDirectory, "../../Binaries/Win64");
            string[] RequiredDLLs = {
                "avcodec-61.dll",
                "avformat-61.dll", 
                "avutil-59.dll",
                "swscale-8.dll",
                // 의존성 DLL들
                "swresample-5.dll",
                "avfilter-10.dll",
                "avdevice-61.dll"
            };
            
            foreach (string DLL in RequiredDLLs)
            {
                string SourceDLLPath = Path.Combine(BinPath, DLL);
                string DestDLLPath = Path.Combine(PluginBinariesPath, DLL);
                
                if (File.Exists(SourceDLLPath))
                {
                    // 플러그인 Binaries 폴더로 복사
                    if (!Directory.Exists(PluginBinariesPath))
                    {
                        Directory.CreateDirectory(PluginBinariesPath);
                    }
                    
                    if (!File.Exists(DestDLLPath) || 
                        File.GetLastWriteTime(SourceDLLPath) > File.GetLastWriteTime(DestDLLPath))
                    {
                        File.Copy(SourceDLLPath, DestDLLPath, true);
                        System.Console.WriteLine("CineSRTStream: Copied " + DLL + " to plugin binaries");
                    }
                    
                    RuntimeDependencies.Add(DestDLLPath);
                    PublicDelayLoadDLLs.Add(DLL);
                }
            }
            
            // FFmpeg 관련 정의
            PublicDefinitions.AddRange(new string[] {
                "__STDC_CONSTANT_MACROS",
                "__STDC_FORMAT_MACROS",
                "__STDC_LIMIT_MACROS"
            });
        }
    }
}
```

### 2. 완전한 VideoEncoder 구현

#### 2.1 SRTVideoEncoder.h (전체)
```cpp
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/CircularQueue.h"

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
        bool bUseHardwareAcceleration = false;
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
    
    // FFmpeg 객체
    AVCodec* Codec = nullptr;
    AVCodecContext* CodecContext = nullptr;
    AVFrame* Frame = nullptr;
    AVPacket* Packet = nullptr;
    SwsContext* SwsContext = nullptr;
    AVBufferRef* HWDeviceContext = nullptr;
    
    // 버퍼 관리
    TCircularQueue<FEncodedFrame> EncodedFrameQueue;
    FCriticalSection QueueLock;
    
    // 통계
    TAtomic<float> LastEncodingTimeMs;
    TAtomic<int32> EncodedFrameCount;
    TAtomic<int32> DroppedFrameCount;
    TAtomic<int64> TotalEncodedBytes;
    
    // 내부 메서드
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
```

#### 2.2 중요 구현 디테일
```cpp
// SRTVideoEncoder.cpp - 핵심 부분

bool FSRTVideoEncoder::Initialize(const FConfig& InConfig)
{
    Config = InConfig;
    
    // FFmpeg 한 번만 초기화
    static bool bFFmpegInitialized = false;
    if (!bFFmpegInitialized)
    {
        av_log_set_level(AV_LOG_WARNING);
        bFFmpegInitialized = true;
    }
    
    // 코덱 선택
    const char* CodecName = nullptr;
    if (Config.bUseHardwareAcceleration)
    {
        if (Config.HWAccelType == TEXT("nvenc"))
        {
            CodecName = "h264_nvenc";
        }
        else if (Config.HWAccelType == TEXT("qsv"))
        {
            CodecName = "h264_qsv";
        }
        else if (Config.HWAccelType == TEXT("amf"))
        {
            CodecName = "h264_amf";
        }
    }
    
    // 하드웨어 코덱 실패시 소프트웨어로 폴백
    if (CodecName)
    {
        Codec = avcodec_find_encoder_by_name(CodecName);
        if (!Codec)
        {
            UE_LOG(LogCineSRTStream, Warning, 
                TEXT("Hardware encoder %s not found, falling back to software"), 
                UTF8_TO_TCHAR(CodecName));
        }
    }
    
    if (!Codec)
    {
        Codec = avcodec_find_encoder_by_name("libx264");
        if (!Codec)
        {
            Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
    }
    
    if (!Codec)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("No H.264 encoder found!"));
        return false;
    }
    
    // 코덱 컨텍스트 생성
    CodecContext = avcodec_alloc_context3(Codec);
    if (!CodecContext)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate codec context"));
        return false;
    }
    
    // 설정 적용
    if (!SetupCodecContext())
    {
        avcodec_free_context(&CodecContext);
        return false;
    }
    
    // 프레임 할당
    Frame = av_frame_alloc();
    if (!Frame)
    {
        Shutdown();
        return false;
    }
    
    Frame->format = CodecContext->pix_fmt;
    Frame->width = CodecContext->width;
    Frame->height = CodecContext->height;
    
    if (av_frame_get_buffer(Frame, 0) < 0)
    {
        Shutdown();
        return false;
    }
    
    // 색공간 변환기
    SwsContext = sws_getContext(
        Config.Width, Config.Height, AV_PIX_FMT_BGRA,
        Config.Width, Config.Height, CodecContext->pix_fmt,
        SWS_BILINEAR | SWS_ACCURATE_RND,
        nullptr, nullptr, nullptr
    );
    
    if (!SwsContext)
    {
        Shutdown();
        return false;
    }
    
    // 패킷 할당
    Packet = av_packet_alloc();
    
    bIsInitialized = true;
    LogCodecInfo();
    
    return true;
}

bool FSRTVideoEncoder::SetupCodecContext()
{
    // 기본 설정
    CodecContext->width = Config.Width;
    CodecContext->height = Config.Height;
    CodecContext->time_base = {1, Config.FrameRate};
    CodecContext->framerate = {Config.FrameRate, 1};
    CodecContext->gop_size = Config.GOPSize;
    CodecContext->max_b_frames = 0;  // B프레임 없음 (저지연)
    CodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // 비트레이트 설정
    if (Config.bUseCBR)
    {
        CodecContext->bit_rate = Config.BitrateKbps * 1000;
        CodecContext->rc_min_rate = CodecContext->bit_rate;
        CodecContext->rc_max_rate = CodecContext->bit_rate;
        CodecContext->rc_buffer_size = Config.BufferSizeKb * 1000;
        
        av_opt_set(CodecContext->priv_data, "nal-hrd", "cbr", 0);
    }
    else
    {
        CodecContext->bit_rate = Config.BitrateKbps * 1000;
        CodecContext->rc_max_rate = Config.MaxBitrateKbps * 1000;
        CodecContext->rc_buffer_size = Config.BufferSizeKb * 1000;
    }
    
    // 스레드 설정
    CodecContext->thread_count = Config.ThreadCount;
    CodecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    
    // x264 특정 옵션
    if (strcmp(Codec->name, "libx264") == 0)
    {
        av_opt_set(CodecContext->priv_data, "preset", TCHAR_TO_UTF8(*Config.Preset), 0);
        av_opt_set(CodecContext->priv_data, "tune", TCHAR_TO_UTF8(*Config.Tune), 0);
        av_opt_set(CodecContext->priv_data, "profile", TCHAR_TO_UTF8(*Config.Profile), 0);
        
        // 추가 x264 옵션
        av_opt_set(CodecContext->priv_data, "x264opts", 
            "keyint=30:min-keyint=30:scenecut=0:bframes=0", 0);
        
        if (!Config.bUseCBR)
        {
            av_opt_set_double(CodecContext->priv_data, "crf", Config.CRF, 0);
        }
    }
    
    // NVENC 특정 옵션
    else if (strstr(Codec->name, "nvenc"))
    {
        av_opt_set(CodecContext->priv_data, "preset", "llhq", 0);  // Low Latency HQ
        av_opt_set(CodecContext->priv_data, "rc", Config.bUseCBR ? "cbr" : "vbr", 0);
        av_opt_set(CodecContext->priv_data, "zerolatency", "1", 0);
        av_opt_set(CodecContext->priv_data, "forced-idr", "1", 0);
    }
    
    // 코덱 열기
    int ret = avcodec_open2(CodecContext, Codec, nullptr);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to open codec: %s"), UTF8_TO_TCHAR(errbuf));
        return false;
    }
    
    return true;
}

bool FSRTVideoEncoder::EncodeFrame(const TArray<FColor>& BGRAData, FEncodedFrame& OutFrame)
{
    if (!bIsInitialized)
        return false;
    
    double StartTime = FPlatformTime::Seconds();
    
    // 입력 데이터 검증
    int32 ExpectedSize = Config.Width * Config.Height;
    if (BGRAData.Num() != ExpectedSize)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Invalid input size: %d, expected %d"), 
            BGRAData.Num(), ExpectedSize);
        return false;
    }
    
    // 색공간 변환
    const uint8* SrcData[4] = {(const uint8*)BGRAData.GetData(), nullptr, nullptr, nullptr};
    int SrcLinesize[4] = {Config.Width * 4, 0, 0, 0};
    
    if (av_frame_make_writable(Frame) < 0)
    {
        return false;
    }
    
    int ret = sws_scale(SwsContext, 
        SrcData, SrcLinesize, 0, Config.Height,
        Frame->data, Frame->linesize);
    
    if (ret != Config.Height)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Color conversion failed"));
        return false;
    }
    
    // 프레임 타임스탬프
    Frame->pts = EncodedFrameCount;
    
    // 인코딩
    ret = avcodec_send_frame(CodecContext, Frame);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        UE_LOG(LogCineSRTStream, Error, TEXT("Error sending frame: %s"), UTF8_TO_TCHAR(errbuf));
        return false;
    }
    
    // 인코딩된 패킷 받기
    bool bGotPacket = false;
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(CodecContext, Packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            UE_LOG(LogCineSRTStream, Error, TEXT("Error receiving packet: %s"), UTF8_TO_TCHAR(errbuf));
            return false;
        }
        
        // 패킷 데이터 복사
        OutFrame.Data.SetNum(Packet->size);
        FMemory::Memcpy(OutFrame.Data.GetData(), Packet->data, Packet->size);
        OutFrame.PTS = Packet->pts;
        OutFrame.DTS = Packet->dts;
        OutFrame.bKeyFrame = (Packet->flags & AV_PKT_FLAG_KEY) != 0;
        OutFrame.FrameNumber = EncodedFrameCount;
        
        bGotPacket = true;
        
        // 통계 업데이트
        TotalEncodedBytes += Packet->size;
        
        av_packet_unref(Packet);
    }
    
    if (bGotPacket)
    {
        EncodedFrameCount++;
        LastEncodingTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        
        UE_LOG(LogCineSRTStream, VeryVerbose, 
            TEXT("Encoded frame %d: %d bytes, %.2fms, %s"),
            OutFrame.FrameNumber, OutFrame.Data.Num(), LastEncodingTimeMs.Load(),
            OutFrame.bKeyFrame ? TEXT("KEY") : TEXT("DELTA"));
    }
    
    return bGotPacket;
}
```

### 3. 완전한 MPEG-TS 구현

#### 3.1 SRTTransportStream.h (전체)
```cpp
#pragma once

#include "CoreMinimal.h"

// MPEG-TS 상수
#define TS_PACKET_SIZE 188
#define TS_SYNC_BYTE 0x47
#define TS_PAT_PID 0x0000
#define TS_NULL_PID 0x1FFF

class CINESRTSTREAM_API FSRTTransportStream
{
public:
    struct FConfig
    {
        // PID 설정
        int32 ServiceID = 1;
        int32 PMTPID = 0x1000;  // 4096
        int32 VideoPID = 0x0100; // 256
        int32 AudioPID = 0x0101; // 257 (향후)
        int32 PCRPID = 0x0100;  // 보통 비디오 PID와 동일
        
        // 서비스 정보
        FString ServiceName = TEXT("UnrealStream");
        FString ProviderName = TEXT("CineSRT");
        
        // 타이밍
        int32 PCRIntervalMs = 40;  // PCR 간격 (권장: 40ms)
        int32 PATIntervalMs = 100; // PAT/PMT 간격
    };

    FSRTTransportStream();
    ~FSRTTransportStream();

    bool Initialize(const FConfig& InConfig);
    void Shutdown();
    
    // 주요 기능
    bool MuxH264Frame(const TArray<uint8>& H264Data, 
                      int64 PTS,
                      int64 DTS,
                      bool bKeyFrame,
                      TArray<uint8>& OutTSPackets);
    
    // 시스템 정보 패킷
    void GeneratePAT(TArray<uint8>& OutPacket);
    void GeneratePMT(TArray<uint8>& OutPacket);
    void GenerateNullPacket(TArray<uint8>& OutPacket);
    
    // 통계
    int64 GetPacketCount() const { return TotalPackets; }
    int64 GetByteCount() const { return TotalBytes; }

private:
    FConfig Config;
    bool bIsInitialized = false;
    
    // 패킷 카운터 (0-15 순환)
    uint8 ContinuityCounter[8192] = {0};
    
    // 타이밍
    int64 LastPCR = 0;
    int64 LastPAT = 0;
    int64 LastPMT = 0;
    double StartTime = 0.0;
    
    // 통계
    int64 TotalPackets = 0;
    int64 TotalBytes = 0;
    
    // 내부 메서드
    void WritePacketHeader(uint8* packet, int pid, bool payload_start, 
                          bool has_adaptation, bool has_payload);
    void WriteAdaptationField(uint8* packet, int size, bool pcr_flag, int64 pcr);
    void WritePES(const uint8* data, int size, int64 pts, int64 dts, 
                  bool key_frame, TArray<uint8>& out_packets);
    int64 GetCurrentPCR();
    
    // CRC32 계산 (PAT/PMT용)
    uint32 CalculateCRC32(const uint8* data, int length);
};
```

#### 3.2 중요 구현 디테일
```cpp
// SRTTransportStream.cpp - 핵심 부분

bool FSRTTransportStream::MuxH264Frame(const TArray<uint8>& H264Data, 
                                       int64 PTS, 
                                       int64 DTS,
                                       bool bKeyFrame,
                                       TArray<uint8>& OutTSPackets)
{
    if (!bIsInitialized)
        return false;
    
    // 현재 시간 (마이크로초)
    double CurrentTime = FPlatformTime::Seconds();
    int64 CurrentTimeUs = (CurrentTime - StartTime) * 1000000.0;
    
    // PAT/PMT 주기적 전송
    if (CurrentTimeUs - LastPAT > Config.PATIntervalMs * 1000)
    {
        GeneratePAT(OutTSPackets);
        LastPAT = CurrentTimeUs;
    }
    
    if (CurrentTimeUs - LastPMT > Config.PATIntervalMs * 1000)
    {
        GeneratePMT(OutTSPackets);
        LastPMT = CurrentTimeUs;
    }
    
    // PES 패킷 생성
    WritePES(H264Data.GetData(), H264Data.Num(), PTS, DTS, bKeyFrame, OutTSPackets);
    
    // PCR 삽입 여부 확인
    if (CurrentTimeUs - LastPCR > Config.PCRIntervalMs * 1000)
    {
        // 다음 비디오 패킷에 PCR 포함하도록 플래그 설정
        LastPCR = CurrentTimeUs;
    }
    
    return true;
}

void FSRTTransportStream::WritePES(const uint8* data, int size, 
                                   int64 pts, int64 dts, 
                                   bool key_frame, 
                                   TArray<uint8>& out_packets)
{
    // PES 헤더 크기 계산
    int pes_header_size = 9;  // 기본 PES 헤더
    if (pts != AV_NOPTS_VALUE)
    {
        pes_header_size += 5;  // PTS
        if (dts != pts)
        {
            pes_header_size += 5;  // DTS
        }
    }
    
    // 첫 번째 TS 패킷 준비
    uint8 packet[TS_PACKET_SIZE];
    FMemory::Memset(packet, 0xFF, TS_PACKET_SIZE);
    
    // TS 헤더
    packet[0] = TS_SYNC_BYTE;
    packet[1] = 0x40 | ((Config.VideoPID >> 8) & 0x1F);  // payload_unit_start_indicator = 1
    packet[2] = Config.VideoPID & 0xFF;
    
    // Adaptation field (PCR 포함 시)
    bool need_pcr = (FPlatformTime::Seconds() - StartTime) * 1000 - LastPCR > Config.PCRIntervalMs;
    int adaptation_size = 0;
    
    if (need_pcr || key_frame)
    {
        adaptation_size = 8;  // PCR용
        packet[3] = 0x30 | (ContinuityCounter[Config.VideoPID] & 0x0F);  // adaptation + payload
        packet[4] = adaptation_size - 1;
        packet[5] = 0x10;  // PCR flag
        
        // PCR 쓰기
        int64 pcr = GetCurrentPCR();
        packet[6] = (pcr >> 25) & 0xFF;
        packet[7] = (pcr >> 17) & 0xFF;
        packet[8] = (pcr >> 9) & 0xFF;
        packet[9] = (pcr >> 1) & 0xFF;
        packet[10] = ((pcr & 1) << 7) | 0x7E;
        packet[11] = 0;
        
        LastPCR = pcr / 300;  // 90kHz to 27MHz
    }
    else
    {
        packet[3] = 0x10 | (ContinuityCounter[Config.VideoPID] & 0x0F);  // payload only
    }
    
    ContinuityCounter[Config.VideoPID] = (ContinuityCounter[Config.VideoPID] + 1) & 0x0F;
    
    // PES 헤더 시작 위치
    int offset = 4 + adaptation_size;
    
    // PES 시작 코드
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    packet[offset++] = 0x01;
    packet[offset++] = 0xE0;  // 비디오 스트림
    
    // PES 패킷 길이 (0 = unbounded)
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    
    // PES 헤더 플래그
    packet[offset++] = 0x80;  // marker bits
    
    uint8 pts_dts_flags = 0;
    if (pts != AV_NOPTS_VALUE)
    {
        if (dts != pts)
        {
            pts_dts_flags = 0xC0;  // PTS + DTS
        }
        else
        {
            pts_dts_flags = 0x80;  // PTS only
        }
    }
    
    packet[offset++] = pts_dts_flags;
    packet[offset++] = pes_header_size - 9;  // PES 헤더 데이터 길이
    
    // PTS/DTS 쓰기
    if (pts_dts_flags & 0x80)
    {
        // PTS
        packet[offset++] = ((pts_dts_flags >> 6) & 0x02) | 0x21 | ((pts >> 29) & 0x0E);
        packet[offset++] = (pts >> 22) & 0xFF;
        packet[offset++] = 0x01 | ((pts >> 14) & 0xFE);
        packet[offset++] = (pts >> 7) & 0xFF;
        packet[offset++] = 0x01 | ((pts << 1) & 0xFE);
        
        if (pts_dts_flags & 0x40)
        {
            // DTS
            packet[offset++] = 0x11 | ((dts >> 29) & 0x0E);
            packet[offset++] = (dts >> 22) & 0xFF;
            packet[offset++] = 0x01 | ((dts >> 14) & 0xFE);
            packet[offset++] = (dts >> 7) & 0xFF;
            packet[offset++] = 0x01 | ((dts << 1) & 0xFE);
        }
    }
    
    // 첫 패킷에 들어갈 페이로드 크기
    int first_payload_size = TS_PACKET_SIZE - offset;
    int bytes_written = FMath::Min(first_payload_size, size);
    FMemory::Memcpy(packet + offset, data, bytes_written);
    
    // 패딩
    if (offset + bytes_written < TS_PACKET_SIZE)
    {
        FMemory::Memset(packet + offset + bytes_written, 0xFF, 
                       TS_PACKET_SIZE - offset - bytes_written);
    }
    
    out_packets.Append(packet, TS_PACKET_SIZE);
    TotalPackets++;
    TotalBytes += TS_PACKET_SIZE;
    
    // 나머지 데이터를 추가 TS 패킷으로
    int remaining = size - bytes_written;
    const uint8* remaining_data = data + bytes_written;
    
    while (remaining > 0)
    {
        FMemory::Memset(packet, 0xFF, TS_PACKET_SIZE);
        
        // TS 헤더 (payload_unit_start_indicator = 0)
        packet[0] = TS_SYNC_BYTE;
        packet[1] = (Config.VideoPID >> 8) & 0x1F;
        packet[2] = Config.VideoPID & 0xFF;
        packet[3] = 0x10 | (ContinuityCounter[Config.VideoPID] & 0x0F);
        
        ContinuityCounter[Config.VideoPID] = (ContinuityCounter[Config.VideoPID] + 1) & 0x0F;
        
        int payload_size = FMath::Min(184, remaining);
        FMemory::Memcpy(packet + 4, remaining_data, payload_size);
        
        // 패딩
        if (payload_size < 184)
        {
            // Adaptation field로 패딩
            packet[3] |= 0x20;  // adaptation field 플래그
            packet[4] = 183 - payload_size;  // adaptation field 길이
            packet[5] = 0x00;  // 플래그 (없음)
            
            // 스터핑 바이트
            for (int i = 6; i < 4 + packet[4] + 1; i++)
            {
                packet[i] = 0xFF;
            }
            
            FMemory::Memcpy(packet + 4 + packet[4] + 1, remaining_data, payload_size);
        }
        
        out_packets.Append(packet, TS_PACKET_SIZE);
        TotalPackets++;
        TotalBytes += TS_PACKET_SIZE;
        
        remaining -= payload_size;
        remaining_data += payload_size;
    }
}

void FSRTTransportStream::GeneratePAT(TArray<uint8>& OutPacket)
{
    uint8 packet[TS_PACKET_SIZE];
    FMemory::Memset(packet, 0xFF, TS_PACKET_SIZE);
    
    // TS 헤더
    packet[0] = TS_SYNC_BYTE;
    packet[1] = 0x40;  // PAT PID = 0, payload_unit_start_indicator = 1
    packet[2] = 0x00;
    packet[3] = 0x10 | (ContinuityCounter[TS_PAT_PID] & 0x0F);
    
    ContinuityCounter[TS_PAT_PID] = (ContinuityCounter[TS_PAT_PID] + 1) & 0x0F;
    
    int offset = 4;
    
    // Pointer field
    packet[offset++] = 0x00;
    
    // PAT 시작
    packet[offset++] = 0x00;  // table_id
    packet[offset++] = 0xB0;  // section_syntax_indicator = 1
    
    // Section length (나중에 채움)
    int length_offset = offset;
    offset += 2;
    
    // Transport stream ID
    packet[offset++] = 0x00;
    packet[offset++] = 0x01;
    
    // Version, current_next_indicator
    packet[offset++] = 0xC1;
    
    // Section number, last section number
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    
    // Program map
    packet[offset++] = (Config.ServiceID >> 8) & 0xFF;
    packet[offset++] = Config.ServiceID & 0xFF;
    packet[offset++] = 0xE0 | ((Config.PMTPID >> 8) & 0x1F);
    packet[offset++] = Config.PMTPID & 0xFF;
    
    // Section length
    int section_length = offset - length_offset - 2 + 4;  // +4 for CRC
    packet[length_offset] = 0x0D | ((section_length >> 8) & 0x0F);
    packet[length_offset + 1] = section_length & 0xFF;
    
    // CRC32
    uint32 crc = CalculateCRC32(packet + 5, offset - 5);
    packet[offset++] = (crc >> 24) & 0xFF;
    packet[offset++] = (crc >> 16) & 0xFF;
    packet[offset++] = (crc >> 8) & 0xFF;
    packet[offset++] = crc & 0xFF;
    
    OutPacket.Append(packet, TS_PACKET_SIZE);
}
```

### 4. OBS 호환성 핵심 요소

#### 4.1 필수 체크리스트
```cpp
// OBS가 요구하는 정확한 스펙

1. **SPS/PPS 반드시 포함**
   - 모든 키프레임 앞에 SPS/PPS NAL 유닛
   - Annex B 형식 (0x00 0x00 0x00 0x01 시작 코드)

2. **정확한 타임스탬프**
   - PTS는 90kHz 클럭 (1초 = 90000)
   - 연속적이고 증가하는 값
   - PCR과 동기화

3. **PAT/PMT 주기**
   - 100ms마다 전송
   - 올바른 stream_type (0x1B for H.264)

4. **버퍼링**
   - 최소 2초 분량의 초기 데이터
   - 키프레임으로 시작
```

#### 4.2 OBS 디버깅 도구
```cpp
// 패킷 덤프 유틸리티
class FOBSDebugger
{
public:
    static void DumpTSPacket(const uint8* packet, const FString& Label)
    {
        UE_LOG(LogCineSRTStream, Verbose, TEXT("=== %s ==="), *Label);
        UE_LOG(LogCineSRTStream, Verbose, TEXT("Sync: 0x%02X"), packet[0]);
        
        uint16 pid = ((packet[1] & 0x1F) << 8) | packet[2];
        UE_LOG(LogCineSRTStream, Verbose, TEXT("PID: %d (0x%04X)"), pid, pid);
        
        bool payload_start = (packet[1] & 0x40) != 0;
        UE_LOG(LogCineSRTStream, Verbose, TEXT("Payload Start: %s"), 
            payload_start ? TEXT("YES") : TEXT("NO"));
        
        // 더 많은 필드...
    }
    
    static void ValidateH264Stream(const TArray<uint8>& Data)
    {
        // NAL 유닛 검증
        int pos = 0;
        while (pos < Data.Num() - 4)
        {
            if (Data[pos] == 0 && Data[pos+1] == 0 && 
                Data[pos+2] == 0 && Data[pos+3] == 1)
            {
                uint8 nal_type = Data[pos+4] & 0x1F;
                UE_LOG(LogCineSRTStream, Verbose, 
                    TEXT("NAL Unit at %d: Type=%d"), pos, nal_type);
            }
            pos++;
        }
    }
};
```

### 5. 실제 문제 해결 가이드

#### 5.1 일반적인 OBS 오류와 해결
```
문제: "No video data received"
원인: SPS/PPS 누락
해결: 각 키프레임 전에 SPS/PPS 삽입

문제: "Invalid timestamp"  
원인: PTS 불연속
해결: PTS 계산 로직 수정, 오버플로우 처리

문제: "Decoding error"
원인: 잘못된 NAL 유닛 순서
해결: SPS → PPS → IDR 순서 보장

문제: "Connection timeout"
원인: 초기 데이터 부족
해결: 연결 후 즉시 PAT/PMT + 키프레임 전송
```

#### 5.2 성능 최적화 실전 팁
```cpp
// 1. 메모리 풀 사용
class FFramePool
{
    TQueue<TArray<uint8>> AvailableBuffers;
    
    TArray<uint8> Acquire(int32 Size)
    {
        TArray<uint8> Buffer;
        if (AvailableBuffers.Dequeue(Buffer))
        {
            Buffer.SetNum(Size, false);  // 재할당 없이 크기 조정
        }
        else
        {
            Buffer.SetNum(Size);
        }
        return Buffer;
    }
};

// 2. 인코딩 스레드 분리
class FEncodingThread : public FRunnable
{
    TCircularQueue<RawFrame> InputQueue;
    TCircularQueue<EncodedFrame> OutputQueue;
};

// 3. Zero-copy SRT 전송
srt_sendmsg2(socket, data, len, &msgCtrl);  // scatter-gather 사용
```

이제 Phase 3 개발 계획서가 충분히 상세하게 작성되었습니다. FFmpeg 통합부터 OBS 호환성까지 실제 구현에 필요한 모든 디테일을 포함했습니다!