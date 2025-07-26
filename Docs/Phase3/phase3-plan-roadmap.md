# CineSRT Phase 3 개발 계획서
## Part 3: 구현 로드맵

### 1. 전체 일정 (6주)

```
Week 1-2: FFmpeg 통합 및 기본 인코딩
Week 3:   MPEG-TS 멀티플렉싱 구현
Week 4:   실시간 파이프라인 통합
Week 5:   OBS 호환성 및 최적화
Week 6:   테스트 및 안정화
```

### 2. Week 1-2: FFmpeg 통합 및 기본 인코딩

#### 2.1 준비 작업 (Day 1-2)

**FFmpeg 라이브러리 준비**
```powershell
# BuildTools/ffmpeg 폴더 생성
mkdir C:\CineSRTProject\BuildTools\ffmpeg

# Pre-built 바이너리 다운로드 (권장)
# https://www.gyan.dev/ffmpeg/builds/
# ffmpeg-6.0-full_build-shared.7z

# 필요 파일 복사
Plugins/CineSRTStream/ThirdParty/FFmpeg/
├── include/
│   ├── libavcodec/
│   ├── libavformat/
│   ├── libavutil/
│   └── libswscale/
├── lib/
│   ├── avcodec.lib
│   ├── avformat.lib
│   ├── avutil.lib
│   └── swscale.lib
└── bin/
    └── *.dll (배포시 필요)
```

**Build.cs 업데이트**
```csharp
// FFmpeg 라이브러리 추가
if (Target.Platform == UnrealTargetPlatform.Win64)
{
    string FFmpegPath = Path.Combine(ThirdPartyPath, "FFmpeg");
    
    PublicIncludePaths.AddRange(new string[] {
        Path.Combine(FFmpegPath, "include")
    });
    
    PublicAdditionalLibraries.AddRange(new string[] {
        Path.Combine(FFmpegPath, "lib", "avcodec.lib"),
        Path.Combine(FFmpegPath, "lib", "avformat.lib"),
        Path.Combine(FFmpegPath, "lib", "avutil.lib"),
        Path.Combine(FFmpegPath, "lib", "swscale.lib")
    });
}
```

#### 2.2 VideoEncoder 구현 (Day 3-5)

**새 파일: SRTVideoEncoder.h**
```cpp
#pragma once

#include "CoreMinimal.h"

// Forward declarations
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class CINESRTSTREAM_API FSRTVideoEncoder
{
public:
    struct Config
    {
        int32 Width = 1920;
        int32 Height = 1080;
        int32 FrameRate = 30;
        int32 BitrateKbps = 5000;
        FString Preset = TEXT("ultrafast");
        bool bUseHardwareAcceleration = false;
    };

    FSRTVideoEncoder();
    ~FSRTVideoEncoder();

    bool Initialize(const Config& InConfig);
    bool EncodeFrame(const TArray<FColor>& BGRAData, TArray<uint8>& OutH264Data);
    void Shutdown();

    // 통계
    float GetEncodingTimeMs() const { return LastEncodingTimeMs; }
    int32 GetEncodedFrameCount() const { return EncodedFrameCount; }

private:
    Config EncoderConfig;
    
    // FFmpeg 객체들
    AVCodecContext* CodecContext = nullptr;
    AVFrame* Frame = nullptr;
    SwsContext* SwsContext = nullptr;
    
    // 통계
    float LastEncodingTimeMs = 0.0f;
    int32 EncodedFrameCount = 0;
    
    // 내부 메서드
    bool InitializeSoftwareEncoder();
    bool InitializeHardwareEncoder();
    bool ConvertBGRAToYUV420P(const TArray<FColor>& BGRAData);
};
```

**구현 예제: SRTVideoEncoder.cpp (핵심 부분)**
```cpp
bool FSRTVideoEncoder::Initialize(const Config& InConfig)
{
    EncoderConfig = InConfig;
    
    // 코덱 찾기
    const AVCodec* codec = nullptr;
    if (EncoderConfig.bUseHardwareAcceleration)
    {
        // NVENC 시도
        codec = avcodec_find_encoder_by_name("h264_nvenc");
        if (!codec)
        {
            UE_LOG(LogCineSRTStream, Warning, TEXT("NVENC not available, falling back to software"));
        }
    }
    
    if (!codec)
    {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    
    // 코덱 컨텍스트 생성
    CodecContext = avcodec_alloc_context3(codec);
    
    // 인코더 설정
    CodecContext->width = EncoderConfig.Width;
    CodecContext->height = EncoderConfig.Height;
    CodecContext->time_base = {1, EncoderConfig.FrameRate};
    CodecContext->framerate = {EncoderConfig.FrameRate, 1};
    CodecContext->bit_rate = EncoderConfig.BitrateKbps * 1000;
    CodecContext->gop_size = EncoderConfig.FrameRate; // 1초마다 키프레임
    CodecContext->max_b_frames = 0; // B프레임 없음 (저지연)
    CodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // 프리셋 적용
    av_opt_set(CodecContext->priv_data, "preset", TCHAR_TO_UTF8(*EncoderConfig.Preset), 0);
    av_opt_set(CodecContext->priv_data, "tune", "zerolatency", 0);
    
    // 코덱 열기
    if (avcodec_open2(CodecContext, codec, nullptr) < 0)
    {
        return false;
    }
    
    // 프레임 할당
    Frame = av_frame_alloc();
    Frame->format = CodecContext->pix_fmt;
    Frame->width = CodecContext->width;
    Frame->height = CodecContext->height;
    av_frame_get_buffer(Frame, 0);
    
    // 색공간 변환기 초기화
    SwsContext = sws_getContext(
        EncoderConfig.Width, EncoderConfig.Height, AV_PIX_FMT_BGRA,
        EncoderConfig.Width, EncoderConfig.Height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    return true;
}
```

#### 2.3 테스트 프로그램 (Day 6-7)

**TestPrograms/test_encoder/test_encoder.cpp**
```cpp
// 단순 인코딩 테스트
// 1. 테스트 패턴 생성
// 2. H.264 인코딩
// 3. 파일로 저장
// 4. FFplay로 재생 확인

int main()
{
    FSRTVideoEncoder Encoder;
    FSRTVideoEncoder::Config Config;
    Config.Width = 1920;
    Config.Height = 1080;
    Config.BitrateKbps = 5000;
    
    if (!Encoder.Initialize(Config))
    {
        std::cout << "Failed to initialize encoder" << std::endl;
        return 1;
    }
    
    // 100 프레임 인코딩 테스트
    std::ofstream output("test_output.h264", std::ios::binary);
    
    for (int i = 0; i < 100; i++)
    {
        TArray<FColor> TestFrame;
        GenerateTestPattern(TestFrame, i);
        
        TArray<uint8> EncodedData;
        if (Encoder.EncodeFrame(TestFrame, EncodedData))
        {
            output.write((char*)EncodedData.GetData(), EncodedData.Num());
            std::cout << "Frame " << i << " encoded: " << EncodedData.Num() << " bytes" << std::endl;
        }
    }
    
    output.close();
    std::cout << "Test complete. Play with: ffplay test_output.h264" << std::endl;
    return 0;
}
```

#### 2.4 Week 1-2 검증 포인트
- [ ] FFmpeg 라이브러리 정상 링크
- [ ] 테스트 패턴 H.264 인코딩 성공
- [ ] FFplay에서 재생 가능
- [ ] 인코딩 시간 < 10ms/frame
- [ ] 메모리 누수 없음

### 3. Week 3: MPEG-TS 멀티플렉싱

#### 3.1 TSMuxer 구현 (Day 1-3)

**새 파일: SRTTransportStream.h**
```cpp
#pragma once

#include "CoreMinimal.h"

class CINESRTSTREAM_API FSRTTransportStream
{
public:
    struct Config
    {
        int32 ServiceID = 1;
        int32 VideoPID = 256;
        int32 PMTPID = 4096;
        FString ServiceName = TEXT("UnrealStream");
    };

    FSRTTransportStream();
    ~FSRTTransportStream();

    bool Initialize(const Config& InConfig);
    bool MuxH264Frame(const TArray<uint8>& H264Data, 
                      int64 PTS,
                      TArray<uint8>& OutTSPackets);
    void Shutdown();

private:
    Config MuxerConfig;
    AVFormatContext* FormatContext = nullptr;
    AVStream* VideoStream = nullptr;
    uint8* IOBuffer = nullptr;
    
    // TS 패킷 생성
    void CreateTSPacket(uint8* packet, int pid, int counter, 
                       const uint8* payload, int payload_size);
    void UpdatePCR(int64 pcr_base);
};
```

#### 3.2 메모리 기반 MPEG-TS 출력

```cpp
// 커스텀 I/O 콜백
static int WritePacket(void* opaque, uint8* buf, int buf_size)
{
    TArray<uint8>* OutputArray = (TArray<uint8>*)opaque;
    OutputArray->Append(buf, buf_size);
    return buf_size;
}

bool FSRTTransportStream::Initialize(const Config& InConfig)
{
    MuxerConfig = InConfig;
    
    // MPEG-TS 포맷으로 출력 컨텍스트 생성
    avformat_alloc_output_context2(&FormatContext, nullptr, "mpegts", nullptr);
    
    // 메모리 기반 I/O 설정
    IOBuffer = (uint8*)av_malloc(32768);
    AVIOContext* avio_ctx = avio_alloc_context(
        IOBuffer, 32768,
        1, // write flag
        nullptr, // opaque는 나중에 설정
        nullptr, // read callback
        WritePacket,
        nullptr  // seek callback
    );
    
    FormatContext->pb = avio_ctx;
    FormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
    
    // 비디오 스트림 추가
    VideoStream = avformat_new_stream(FormatContext, nullptr);
    VideoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    VideoStream->codecpar->codec_id = AV_CODEC_ID_H264;
    VideoStream->codecpar->bit_rate = MuxerConfig.BitrateKbps * 1000;
    
    return true;
}
```

#### 3.3 Week 3 검증 포인트
- [ ] PAT/PMT 정확히 생성
- [ ] TS 패킷 188 bytes 정렬
- [ ] PCR 100ms마다 삽입
- [ ] VLC에서 TS 파일 인식
- [ ] 타임스탬프 연속성

### 4. Week 4: 실시간 파이프라인 통합

#### 4.1 SendFrameData 재구현 (Day 1-2)

```cpp
bool FSRTStreamWorker::SendFrameData()
{
    // 1. 프레임 버퍼에서 가져오기
    FrameBuffer::Frame Frame;
    if (!Owner->FrameBuffer->GetFrame(Frame))
        return false;
    
    // 2. BGRA → H.264 인코딩
    TArray<uint8> H264Data;
    double EncodeStart = FPlatformTime::Seconds();
    
    if (!VideoEncoder->EncodeFrame(Frame.PixelData, H264Data))
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to encode frame"));
        return false;
    }
    
    double EncodeTime = (FPlatformTime::Seconds() - EncodeStart) * 1000.0;
    
    // 3. H.264 → MPEG-TS 패키징
    TArray<uint8> TSPackets;
    int64 PTS = Frame.Timestamp * 90000; // 90kHz 시간축
    
    if (!TSMuxer->MuxH264Frame(H264Data, PTS, TSPackets))
    {
        UE_LOG(LogCineSRTStream, Error, TEXT("Failed to mux frame"));
        return false;
    }
    
    // 4. SRT 전송 (188 bytes 단위)
    const int PacketsPerSend = 7; // 1316 bytes
    const uint8* DataPtr = TSPackets.GetData();
    int32 TotalSize = TSPackets.Num();
    int32 Sent = 0;
    
    while (Sent < TotalSize && !Owner->bStopRequested)
    {
        int32 ChunkSize = FMath::Min(PacketsPerSend * 188, TotalSize - Sent);
        int result = SRTNetwork::Send(SRTSocket, (char*)(DataPtr + Sent), ChunkSize);
        
        if (result < 0)
        {
            // 에러 처리
            return false;
        }
        
        Sent += result;
    }
    
    // 통계 업데이트
    Owner->TotalFramesSent++;
    Owner->LastEncodingTimeMs = EncodeTime;
    
    UE_LOG(LogCineSRTStream, VeryVerbose, 
        TEXT("Frame %d sent: %d bytes encoded, %d bytes sent, %.2fms"),
        Owner->TotalFramesSent, H264Data.Num(), TSPackets.Num(), EncodeTime);
    
    return true;
}
```

#### 4.2 파이프라인 최적화 (Day 3-4)

**더블 버퍼링 구현**
```cpp
class FPipelineOptimizer
{
private:
    // 인코딩과 전송을 분리
    TQueue<TSharedPtr<EncodedFrame>> EncodingQueue;
    TQueue<TSharedPtr<TSPacket>> TransmissionQueue;
    
    // 별도 스레드
    FRunnableThread* EncodingThread;
    FRunnableThread* TransmissionThread;
    
public:
    void ProcessFrame(const FrameData& Input)
    {
        // 1. 인코딩 큐에 추가
        EncodingQueue.Enqueue(Input);
        
        // 2. 인코딩 스레드에서 처리
        // 3. 전송 큐로 이동
        // 4. 전송 스레드에서 SRT 전송
    }
};
```

#### 4.3 Week 4 검증 포인트
- [ ] 언리얼 → SRT 실시간 전송
- [ ] 30fps 안정적 유지
- [ ] 파이프라인 지연 < 100ms
- [ ] 메모리 사용 안정적
- [ ] CPU 사용률 목표치 달성

### 5. Week 5: OBS 호환성 및 최적화

#### 5.1 OBS 테스트 (Day 1-2)

**OBS 설정**
```
1. Sources → Add → Media Source
2. Properties:
   - Local File: ❌ (체크 해제)
   - Input: srt://127.0.0.1:9001?mode=listener
   - Input Format: (자동)
   - Reconnect Delay: 2s
   - Use hardware decoding: ✓
```

**디버깅 체크리스트**
- [ ] OBS 로그 확인
- [ ] Wireshark로 패킷 캡처
- [ ] 타임스탬프 검증
- [ ] 키프레임 간격 확인

#### 5.2 호환성 문제 해결 (Day 3-4)

**일반적인 문제와 해결**
```cpp
// 1. SPS/PPS 누락
// → 각 키프레임마다 SPS/PPS 포함

// 2. 타임스탬프 불연속
// → PTS 계산 로직 수정

// 3. 버퍼 언더런
// → 초기 버퍼링 추가

// 4. 패킷 순서
// → 시퀀스 번호 관리
```

#### 5.3 성능 최적화 (Day 5)

**프로파일링 및 최적화**
```cpp
// 1. 병목 지점 찾기
SCOPE_CYCLE_COUNTER(STAT_VideoEncoding);
SCOPE_CYCLE_COUNTER(STAT_TSMuxing);
SCOPE_CYCLE_COUNTER(STAT_SRTSending);

// 2. 최적화 적용
- SIMD 색공간 변환
- 멀티스레드 인코딩
- Zero-copy 전송
```

### 6. Week 6: 테스트 및 안정화

#### 6.1 종합 테스트 시나리오

**기능 테스트**
- [ ] 720p/1080p/4K 해상도
- [ ] 24/30/60 fps
- [ ] 1-20 Mbps 비트레이트
- [ ] 10분/1시간/24시간 연속

**스트레스 테스트**
- [ ] 네트워크 끊김/재연결
- [ ] CPU 100% 상황
- [ ] 메모리 부족
- [ ] 동시 다중 스트림

#### 6.2 최종 검증

**품질 기준**
```
✅ OBS 수신 성공률: 100%
✅ 평균 지연시간: < 200ms
✅ 프레임 드롭: < 0.1%
✅ CPU 사용률: < 30%
✅ 메모리 증가: < 1MB/hour
```

### 7. 출시 준비

#### 7.1 문서화
- API 레퍼런스
- 사용자 가이드
- 트러블슈팅 가이드
- 성능 튜닝 가이드

#### 7.2 배포 패키지
- 플러그인 바이너리
- 필수 DLL 파일
- 샘플 프로젝트
- 설정 프리셋

---

다음 문서: Part 4 - 검증 및 품질 보증