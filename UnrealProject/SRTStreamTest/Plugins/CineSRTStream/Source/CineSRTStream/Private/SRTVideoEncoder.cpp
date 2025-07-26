// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRTVideoEncoder.h"
#include "CineSRTStream.h"  // 반드시 이 순서로!
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

// FFmpeg 헤더 전에 이것들 추가
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4005)
#pragma warning(disable: 4996)
#endif

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/opt.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

#ifdef _WIN32
#pragma warning(pop)
#endif

// AV_NOPTS_VALUE 정의
#ifndef AV_NOPTS_VALUE
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))
#endif

FSRTVideoEncoder::FSRTVideoEncoder()
{
    bIsInitialized = false;
    EncodedFrameCount = 0;
    DroppedFrameCount = 0;
    TotalEncodedBytes = 0;
    LastEncodingTimeMs = 0.0f;
}

FSRTVideoEncoder::~FSRTVideoEncoder()
{
    Shutdown();
}

bool FSRTVideoEncoder::Initialize(const FConfig& InConfig)
{
    FScopeLock Lock(&EncoderLock);
    
    Config = InConfig;
    
    // 이미 초기화되어 있으면 정리 먼저
    if (bIsInitialized)
    {
        Shutdown();
    }
    
    // FFmpeg 로그 레벨 설정
    av_log_set_level(AV_LOG_WARNING);
    
    // 코덱 찾기
    Codec = avcodec_find_encoder_by_name("libx264");
    if (!Codec)
    {
        Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!Codec)
        {
            UE_LOG(LogCineSRTStream, Error, TEXT("No H.264 encoder found!"));
            return false;
        }
    }
    
    // 코덱 컨텍스트 할당
    CodecContext = avcodec_alloc_context3(Codec);
    if (!CodecContext)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate codec context"));
        return false;
    }
    
    // 코덱 설정
    if (!SetupCodecContext())
    {
        avcodec_free_context(&CodecContext);
        CodecContext = nullptr;
        return false;
    }
    
    // 프레임 할당
    Frame = av_frame_alloc();
    if (!Frame)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate frame"));
        Shutdown();
        return false;
    }
    
    Frame->format = CodecContext->pix_fmt;
    Frame->width = CodecContext->width;
    Frame->height = CodecContext->height;
    
    int ret = av_frame_get_buffer(Frame, 0);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate frame buffer: %s"), UTF8_TO_TCHAR(errbuf));
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
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to create color converter"));
        Shutdown();
        return false;
    }
    
    // 패킷 할당
    Packet = av_packet_alloc();
    if (!Packet)
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to allocate packet"));
        Shutdown();
        return false;
    }
    
    bIsInitialized = true;
    LogCodecInfo();
    
    return true;
}

void FSRTVideoEncoder::Shutdown()
{
    FScopeLock Lock(&EncoderLock);
    
    bIsInitialized = false;
    
    if (Packet)
    {
        av_packet_free(&Packet);
        Packet = nullptr;
    }
    
    if (SwsContext)
    {
        sws_freeContext(SwsContext);
        SwsContext = nullptr;
    }
    
    if (Frame)
    {
        av_frame_free(&Frame);
        Frame = nullptr;
    }
    
    if (CodecContext)
    {
        avcodec_free_context(&CodecContext);
        CodecContext = nullptr;
    }
    
    // 큐 비우기
    {
        FScopeLock QueueLockScope(&QueueLock);
        FEncodedFrame Dummy;
        while (EncodedFrameQueue.Dequeue(Dummy)) {}
    }
    
    // 통계 초기화
    EncodedFrameCount = 0;
    DroppedFrameCount = 0;
    TotalEncodedBytes = 0;
    LastEncodingTimeMs = 0.0f;
}

bool FSRTVideoEncoder::SetupCodecContext()
{
    // 기본 설정
    CodecContext->width = Config.Width;
    CodecContext->height = Config.Height;
    CodecContext->time_base = {1, Config.FrameRate};
    CodecContext->framerate = {Config.FrameRate, 1};
    CodecContext->gop_size = Config.GOPSize;
    CodecContext->max_b_frames = 0;
    CodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // 비트레이트 설정
    CodecContext->bit_rate = Config.BitrateKbps * 1000;
    
    if (Config.bUseCBR)
    {
        CodecContext->rc_min_rate = CodecContext->bit_rate;
        CodecContext->rc_max_rate = CodecContext->bit_rate;
        CodecContext->rc_buffer_size = Config.BufferSizeKb * 1000;
    }
    else
    {
        CodecContext->rc_max_rate = Config.MaxBitrateKbps * 1000;
        CodecContext->rc_buffer_size = Config.BufferSizeKb * 1000;
    }
    
    // 스레드 설정
    CodecContext->thread_count = Config.ThreadCount;
    CodecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    
    // x264 특정 옵션
    if (Codec && strcmp(Codec->name, "libx264") == 0)
    {
        av_opt_set(CodecContext->priv_data, "preset", TCHAR_TO_UTF8(*Config.Preset), 0);
        av_opt_set(CodecContext->priv_data, "tune", TCHAR_TO_UTF8(*Config.Tune), 0);
        av_opt_set(CodecContext->priv_data, "profile", TCHAR_TO_UTF8(*Config.Profile), 0);
        
        // x264 옵션
        FString x264opts = FString::Printf(TEXT("keyint=%d:min-keyint=%d:scenecut=0:bframes=0"), 
            Config.GOPSize, Config.GOPSize);
        
        if (Config.bUseCBR)
        {
            x264opts += TEXT(":nal-hrd=cbr");
        }
        else
        {
            av_opt_set_double(CodecContext->priv_data, "crf", Config.CRF, 0);
        }
        
        av_opt_set(CodecContext->priv_data, "x264opts", TCHAR_TO_UTF8(*x264opts), 0);
    }
    
    // 코덱 열기
    AVDictionary* opts = nullptr;
    int ret = avcodec_open2(CodecContext, Codec, &opts);
    av_dict_free(&opts);
    
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
    FScopeLock Lock(&EncoderLock);
    
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
    if (!ConvertAndEncode(BGRAData))
    {
        return false;
    }
    
    // 프레임 타임스탬프
    Frame->pts = EncodedFrameCount;
    
    // 인코딩
    int ret = avcodec_send_frame(CodecContext, Frame);
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

bool FSRTVideoEncoder::ConvertAndEncode(const TArray<FColor>& BGRAData)
{
    // 16바이트 정렬된 버퍼 할당
    int stride = ((Config.Width * 4 + 15) / 16) * 16;
    TArray<uint8> AlignedBuffer;
    AlignedBuffer.SetNum(stride * Config.Height);
    
    // 데이터 복사 및 정렬
    for (int y = 0; y < Config.Height; y++)
    {
        FMemory::Memcpy(
            AlignedBuffer.GetData() + y * stride,
            BGRAData.GetData() + y * Config.Width,
            Config.Width * 4
        );
    }
    
    // 정렬된 데이터로 변환
    const uint8* src_data[4] = {AlignedBuffer.GetData(), nullptr, nullptr, nullptr};
    int src_linesize[4] = {stride, 0, 0, 0};
    
    if (av_frame_make_writable(Frame) < 0)
    {
        return false;
    }
    
    int ret = sws_scale(SwsContext, 
        src_data, src_linesize, 0, Config.Height,
        Frame->data, Frame->linesize);
    
    return ret == Config.Height;
}

bool FSRTVideoEncoder::EncodeFrameAsync(const TArray<FColor>& BGRAData)
{
    FEncodedFrame EncodedFrame;
    if (EncodeFrame(BGRAData, EncodedFrame))
    {
        FScopeLock Lock(&QueueLock);
        EncodedFrameQueue.Enqueue(EncodedFrame);
        return true;
    }
    return false;
}

bool FSRTVideoEncoder::GetEncodedFrame(FEncodedFrame& OutFrame)
{
    FScopeLock Lock(&QueueLock);
    return EncodedFrameQueue.Dequeue(OutFrame);
}

float FSRTVideoEncoder::GetAverageBitrateKbps() const
{
    if (EncodedFrameCount == 0)
        return 0.0f;
    
    // 평균 비트레이트 계산 (Kbps)
    double totalSeconds = (double)EncodedFrameCount / Config.FrameRate;
    return (float)((TotalEncodedBytes * 8.0) / (totalSeconds * 1000.0));
}

bool FSRTVideoEncoder::SetBitrate(int32 NewBitrateKbps)
{
    FScopeLock Lock(&EncoderLock);
    
    if (!bIsInitialized || !CodecContext)
        return false;
    
    Config.BitrateKbps = NewBitrateKbps;
    CodecContext->bit_rate = NewBitrateKbps * 1000;
    
    if (Config.bUseCBR)
    {
        CodecContext->rc_min_rate = CodecContext->bit_rate;
        CodecContext->rc_max_rate = CodecContext->bit_rate;
    }
    
    return true;
}

bool FSRTVideoEncoder::ForceKeyFrame()
{
    FScopeLock Lock(&EncoderLock);
    
    if (!bIsInitialized || !CodecContext)
        return false;
    
    // 강제 키프레임 설정
    avcodec_flush_buffers(CodecContext);
    return true;
}

void FSRTVideoEncoder::LogCodecInfo()
{
    if (Codec && CodecContext)
    {
        UE_LOG(LogCineSRTStream, Log, TEXT("Video Encoder Initialized:"));
        UE_LOG(LogCineSRTStream, Log, TEXT("  Codec: %s"), UTF8_TO_TCHAR(Codec->name));
        UE_LOG(LogCineSRTStream, Log, TEXT("  Resolution: %dx%d"), Config.Width, Config.Height);
        UE_LOG(LogCineSRTStream, Log, TEXT("  Frame Rate: %d fps"), Config.FrameRate);
        UE_LOG(LogCineSRTStream, Log, TEXT("  Bitrate: %d kbps"), Config.BitrateKbps);
        UE_LOG(LogCineSRTStream, Log, TEXT("  GOP Size: %d"), Config.GOPSize);
        UE_LOG(LogCineSRTStream, Log, TEXT("  Preset: %s"), *Config.Preset);
        UE_LOG(LogCineSRTStream, Log, TEXT("  Tune: %s"), *Config.Tune);
    }
} 