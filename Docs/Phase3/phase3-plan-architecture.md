# CineSRT Phase 3 개발 계획서
## Part 2: 기술 아키텍처 설계

### 1. 시스템 아키텍처

#### 1.1 전체 파이프라인
```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   언리얼    │     │   인코딩     │     │   패키징    │
│   엔진      │────▶│   파이프     │────▶│   시스템    │
│ (BGRA)      │     │ (H.264)      │     │ (MPEG-TS)   │
└─────────────┘     └──────────────┘     └─────────────┘
                            │                     │
                            ▼                     ▼
                    ┌──────────────┐     ┌─────────────┐
                    │  프레임      │     │    SRT      │
                    │  버퍼 관리   │     │   전송      │
                    └──────────────┘     └─────────────┘
                                                 │
                                                 ▼
                                         ┌─────────────┐
                                         │  OBS/VLC    │
                                         │   수신      │
                                         └─────────────┘
```

#### 1.2 컴포넌트 상세 설계

##### 1.2.1 VideoEncoder 클래스
```cpp
class FSRTVideoEncoder {
public:
    struct EncoderConfig {
        int32 Width = 1920;
        int32 Height = 1080;
        int32 FrameRate = 30;
        int32 BitrateKbps = 5000;
        FString Preset = "ultrafast";  // ultrafast, fast, medium
        FString Profile = "baseline";  // baseline, main, high
    };

    bool Initialize(const EncoderConfig& Config);
    bool EncodeFrame(const TArray<FColor>& RGBAData, TArray<uint8>& OutH264Data);
    void Shutdown();

private:
    AVCodecContext* CodecContext = nullptr;
    AVFrame* Frame = nullptr;
    SwsContext* ColorConverter = nullptr;
    TQueue<AVPacket*> PacketQueue;
};
```

##### 1.2.2 TSMuxer 클래스
```cpp
class FSRTTransportStreamMuxer {
public:
    struct MuxerConfig {
        int32 VideoPID = 256;
        int32 AudioPID = 257;  // 향후 오디오용
        int32 PMTPID = 4096;
        int32 ServiceID = 1;
    };

    bool Initialize(const MuxerConfig& Config);
    bool MuxVideoPacket(const TArray<uint8>& H264Data, 
                       int64 PTS, 
                       TArray<uint8>& OutTSPackets);
    void GeneratePAT(TArray<uint8>& OutPacket);
    void GeneratePMT(TArray<uint8>& OutPacket);

private:
    int64 LastPCR = 0;
    uint8 ContinuityCounter[8192] = {0};
    AVFormatContext* FormatContext = nullptr;
};
```

##### 1.2.3 업데이트된 StreamWorker
```cpp
class FSRTStreamWorker : public FRunnable {
private:
    // 새로운 멤버 추가
    TUniquePtr<FSRTVideoEncoder> VideoEncoder;
    TUniquePtr<FSRTTransportStreamMuxer> TSMuxer;
    
    // 파이프라인 버퍼
    TCircularQueue<EncodedFrame> EncodedFrameQueue;
    TCircularQueue<TSPacket> TransportStreamQueue;
    
    // 성능 모니터링
    FStreamingMetrics Metrics;
};
```

### 2. 데이터 플로우

#### 2.1 프레임 처리 과정
```
1. Scene Capture (GPU)
   ↓ [1920x1080 BGRA]
2. GPU Readback
   ↓ [8,294,400 bytes]
3. Color Conversion (BGRA → YUV420)
   ↓ [3,110,400 bytes]
4. H.264 Encoding
   ↓ [~150,000 bytes]
5. MPEG-TS Packetization
   ↓ [188 byte packets]
6. SRT Transmission
   ↓ [1316 bytes chunks]
7. OBS Reception
```

#### 2.2 타이밍 다이어그램
```
Frame N:   [Capture]────[Convert]────[Encode]────[Mux]────[Send]
Frame N+1:          [Capture]────[Convert]────[Encode]────[Mux]────[Send]
Frame N+2:                   [Capture]────[Convert]────[Encode]────[Mux]

시간:      0ms      33ms      66ms      100ms    133ms    166ms
목표:      각 프레임 총 처리시간 < 100ms
```

### 3. 핵심 구현 세부사항

#### 3.1 색공간 변환 최적화
```cpp
// BGRA → YUV420P 고속 변환
void ConvertBGRAToYUV420P(const uint8* BGRAData, 
                         uint8* YPlane, 
                         uint8* UPlane, 
                         uint8* VPlane,
                         int Width, 
                         int Height) {
    // SIMD 최적화 적용
    // 캐시 친화적 메모리 접근
    // 멀티스레드 처리
}
```

#### 3.2 H.264 인코딩 설정
```cpp
// x264 최적 파라미터
x264_param_default_preset(&params, "ultrafast", "zerolatency");
params.i_csp = X264_CSP_I420;
params.i_width = Width;
params.i_height = Height;
params.i_fps_num = 30;
params.i_fps_den = 1;
params.i_keyint_max = 30;  // 1초마다 키프레임
params.i_bframe = 0;       // B프레임 없음 (저지연)
params.i_threads = 4;      // 멀티스레드
params.b_vfr_input = 0;    // 고정 프레임레이트
params.rc.i_rc_method = X264_RC_ABR;  // 평균 비트레이트
params.rc.i_bitrate = 5000;  // 5 Mbps
```

#### 3.3 MPEG-TS 패킷 구조
```
┌─────────────┬──────────────┬────────────────┐
│  TS Header  │  Adaptation  │    Payload     │
│  (4 bytes)  │    Field     │  (184 bytes)   │
└─────────────┴──────────────┴────────────────┘

TS Header:
- Sync Byte: 0x47
- PID: 13 bits
- Continuity Counter: 4 bits
- Payload Start Indicator

Timing:
- PCR every 100ms
- PTS on every access unit
```

### 4. 메모리 관리

#### 4.1 버퍼 풀 설계
```cpp
class FFrameBufferPool {
private:
    TArray<TSharedPtr<FFrameBuffer>> AvailableBuffers;
    FCriticalSection PoolLock;
    
public:
    TSharedPtr<FFrameBuffer> Acquire() {
        FScopeLock Lock(&PoolLock);
        if (AvailableBuffers.Num() > 0) {
            return AvailableBuffers.Pop();
        }
        return MakeShared<FFrameBuffer>(1920 * 1080 * 4);
    }
    
    void Release(TSharedPtr<FFrameBuffer> Buffer) {
        Buffer->Reset();
        FScopeLock Lock(&PoolLock);
        AvailableBuffers.Add(Buffer);
    }
};
```

#### 4.2 Zero-Copy 전략
- GPU → System Memory: 불가피한 복사
- Color Conversion: In-place 변환 시도
- Encoding: 직접 포인터 전달
- SRT Send: Scatter-Gather I/O

### 5. 에러 처리 및 복구

#### 5.1 에러 시나리오
1. **인코더 실패**
   - 프레임 스킵
   - 키프레임 강제 생성
   - 비트레이트 조정

2. **네트워크 혼잡**
   - 동적 비트레이트 감소
   - 프레임레이트 조정
   - 버퍼 크기 증가

3. **OBS 연결 끊김**
   - 자동 재연결
   - 상태 유지
   - 키프레임 재전송

#### 5.2 모니터링 메트릭
```cpp
struct FStreamingMetrics {
    // 인코딩 성능
    float AvgEncodingTimeMs;
    int32 EncodedFrames;
    int32 SkippedFrames;
    
    // 네트워크 성능  
    float NetworkBitrateKbps;
    float PacketLossRate;
    float RoundTripTimeMs;
    
    // 시스템 리소스
    float CPUUsagePercent;
    int64 MemoryUsedMB;
    float GPUUsagePercent;
};
```

### 6. 테스트 전략

#### 6.1 단위 테스트
- Color Conversion 정확도
- H.264 NAL 유닛 검증
- MPEG-TS 패킷 구조
- 타임스탬프 계산

#### 6.2 통합 테스트
- End-to-End 파이프라인
- OBS 수신 검증
- 장시간 안정성
- 네트워크 장애 시뮬레이션

#### 6.3 성능 테스트
- CPU/GPU 사용률
- 메모리 누수 검사
- 프레임 드롭률
- 지연시간 측정

---

다음 문서: Part 3 - 구현 로드맵