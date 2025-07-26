# CineSRT Phase 3 개발 계획서
## Part 4: 검증 및 품질 보증

### 1. 테스트 전략 개요

#### 1.1 테스트 피라미드
```
         ╱ E2E 테스트 ╲      (10%)
        ╱─────────────╲     OBS 통합, 장시간 스트리밍
       ╱ 통합 테스트   ╲    (30%)
      ╱───────────────╲   파이프라인, 성능
     ╱  단위 테스트    ╲  (60%)
    ╱─────────────────╲ 인코딩, 패킷화, 타이밍
```

#### 1.2 테스트 환경
- **개발**: Windows 10/11, VS2022, UE5.3+
- **CI/CD**: GitHub Actions, 자동 빌드
- **성능**: i7-9700K, RTX 3070, 32GB RAM
- **네트워크**: 로컬 및 WAN 시뮬레이션

### 2. 단위 테스트 (60%)

#### 2.1 인코더 테스트
```cpp
// TestPrograms/unit_tests/test_encoder.cpp

TEST_CASE("VideoEncoder_BasicEncoding")
{
    FSRTVideoEncoder Encoder;
    FSRTVideoEncoder::Config Config;
    Config.Width = 1920;
    Config.Height = 1080;
    Config.BitrateKbps = 5000;
    
    REQUIRE(Encoder.Initialize(Config));
    
    // 검은 화면 인코딩
    TArray<FColor> BlackFrame;
    BlackFrame.SetNum(1920 * 1080);
    
    TArray<uint8> Output;
    REQUIRE(Encoder.EncodeFrame(BlackFrame, Output));
    REQUIRE(Output.Num() > 0);
    REQUIRE(Output.Num() < 100000); // 압축 확인
}

TEST_CASE("VideoEncoder_Performance")
{
    FSRTVideoEncoder Encoder;
    // ... 초기화 ...
    
    double TotalTime = 0;
    const int TestFrames = 100;
    
    for (int i = 0; i < TestFrames; i++)
    {
        auto Start = FPlatformTime::Seconds();
        Encoder.EncodeFrame(TestFrame, Output);
        TotalTime += FPlatformTime::Seconds() - Start;
    }
    
    double AvgTime = TotalTime / TestFrames * 1000.0;
    REQUIRE(AvgTime < 10.0); // 10ms 이하
}
```

#### 2.2 MPEG-TS 테스트
```cpp
TEST_CASE("TSMuxer_PacketStructure")
{
    // TS 패킷 구조 검증
    uint8 packet[188];
    CreateTSPacket(packet, 256, 0, payload, 184);
    
    REQUIRE(packet[0] == 0x47); // Sync byte
    REQUIRE((packet[1] & 0x1F) == 0x01); // PID 상위 5비트
    REQUIRE(packet[2] == 0x00); // PID 하위 8비트
}

TEST_CASE("TSMuxer_Timing")
{
    // PTS/DTS 계산 검증
    int64 timestamp_ms = 1000;
    int64 pts = ConvertToPTS(timestamp_ms);
    REQUIRE(pts == 90000); // 90kHz 클럭
}
```

### 3. 통합 테스트 (30%)

#### 3.1 파이프라인 테스트
```cpp
TEST_CASE("Pipeline_EndToEnd")
{
    // 전체 파이프라인 테스트
    auto Component = NewObject<USRTStreamComponent>();
    Component->StreamIP = TEXT("127.0.0.1");
    Component->StreamPort = 9999;
    Component->bCallerMode = false;
    
    // 수신 서버 시작
    auto Receiver = StartTestReceiver(9999);
    
    // 스트리밍 시작
    Component->StartStreaming();
    
    // 10초 대기
    FPlatformProcess::Sleep(10.0f);
    
    // 검증
    REQUIRE(Receiver->ReceivedFrames > 250); // 최소 25fps
    REQUIRE(Receiver->ValidTSPackets > 0);
    REQUIRE(Receiver->H264Detected == true);
    
    Component->StopStreaming();
}
```

#### 3.2 메모리 누수 테스트
```cpp
TEST_CASE("Memory_LeakTest")
{
    size_t InitialMemory = FPlatformMemory::GetStats().UsedPhysical;
    
    // 100번 시작/중지 반복
    for (int i = 0; i < 100; i++)
    {
        auto Component = NewObject<USRTStreamComponent>();
        Component->StartStreaming();
        FPlatformProcess::Sleep(0.1f);
        Component->StopStreaming();
        Component->ConditionalBeginDestroy();
    }
    
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    size_t FinalMemory = FPlatformMemory::GetStats().UsedPhysical;
    
    // 메모리 증가량 체크 (10MB 이하)
    REQUIRE((FinalMemory - InitialMemory) < 10 * 1024 * 1024);
}
```

### 4. E2E 테스트 (10%)

#### 4.1 OBS 자동화 테스트
```python
# test_obs_integration.py
import obswebsocket
import time
import subprocess

def test_obs_reception():
    # OBS WebSocket 연결
    client = obswebsocket.obsws("localhost", 4444, "password")
    client.connect()
    
    # SRT 소스 추가
    client.call(obswebsocket.requests.CreateInput(
        sceneName="TestScene",
        inputName="SRTTest",
        inputKind="ffmpeg_source",
        inputSettings={
            "input": "srt://127.0.0.1:9001?mode=listener",
            "is_local_file": False
        }
    ))
    
    # 언리얼 스트리밍 시작
    unreal_process = subprocess.Popen([
        "UnrealEditor.exe",
        "SRTStreamTest.uproject",
        "-ExecCmds=SRTStream.Start"
    ])
    
    time.sleep(30)  # 30초 스트리밍
    
    # 통계 확인
    stats = client.call(obswebsocket.requests.GetInputPropertiesListPropertyItems(
        inputName="SRTTest"
    ))
    
    assert stats.fps > 29.0
    assert stats.dropped_frames < 10
    
    client.disconnect()
```

#### 4.2 장시간 안정성 테스트
```cpp
TEST_CASE("LongTerm_Stability")
{
    auto Component = NewObject<USRTStreamComponent>();
    Component->StartStreaming();
    
    const int TestHours = 24;
    const int CheckInterval = 3600; // 1시간마다
    
    for (int hour = 0; hour < TestHours; hour++)
    {
        FPlatformProcess::Sleep(CheckInterval);
        
        // 상태 체크
        REQUIRE(Component->IsStreaming());
        REQUIRE(Component->ConnectionState == ESRTConnectionState::Streaming);
        
        // 성능 체크
        float CPU = FPlatformMisc::GetCPUPercentage();
        REQUIRE(CPU < 40.0f);
        
        // 메모리 체크
        SIZE_T Memory = FPlatformMemory::GetStats().UsedPhysical;
        REQUIRE(Memory < InitialMemory * 1.1f); // 10% 이하 증가
        
        UE_LOG(LogTest, Log, TEXT("Hour %d: CPU=%.1f%%, Memory=%dMB"), 
            hour + 1, CPU, Memory / 1024 / 1024);
    }
}
```

### 5. 성능 벤치마크

#### 5.1 인코딩 성능 측정
```cpp
void BenchmarkEncoding()
{
    struct Result {
        FString Resolution;
        int32 FPS;
        float AvgEncodeTime;
        float MaxEncodeTime;
        float CPUUsage;
    };
    
    TArray<Result> Results;
    
    // 다양한 해상도 테스트
    TArray<FIntPoint> Resolutions = {
        {1280, 720},
        {1920, 1080},
        {3840, 2160}
    };
    
    for (auto& Res : Resolutions)
    {
        Result R;
        R.Resolution = FString::Printf(TEXT("%dx%d"), Res.X, Res.Y);
        
        // 성능 측정
        MeasurePerformance(Res.X, Res.Y, R);
        Results.Add(R);
    }
    
    // 리포트 생성
    GeneratePerformanceReport(Results);
}
```

#### 5.2 네트워크 성능 측정
```cpp
void BenchmarkNetwork()
{
    // 다양한 네트워크 조건 시뮬레이션
    struct NetworkCondition {
        int32 Bandwidth; // Mbps
        float PacketLoss; // %
        int32 Latency; // ms
    };
    
    TArray<NetworkCondition> Conditions = {
        {100, 0.0f, 1},    // LAN
        {20, 0.1f, 20},    // Good Internet
        {5, 1.0f, 50},     // Poor Internet
        {2, 5.0f, 200}     // Very Poor
    };
    
    for (auto& Condition : Conditions)
    {
        ApplyNetworkCondition(Condition);
        TestStreaming();
        RecordResults();
    }
}
```

### 6. 문제 추적 및 디버깅

#### 6.1 로깅 시스템
```cpp
// 상세 로깅 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogSRTEncoder, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSRTMuxer, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSRTNetwork, Log, All);

// 디버그 모드에서 패킷 덤프
#if !UE_BUILD_SHIPPING
void DumpTSPacket(const uint8* packet)
{
    UE_LOG(LogSRTMuxer, Verbose, TEXT("TS Packet:"));
    UE_LOG(LogSRTMuxer, Verbose, TEXT("  Sync: 0x%02X"), packet[0]);
    UE_LOG(LogSRTMuxer, Verbose, TEXT("  PID: %d"), 
        ((packet[1] & 0x1F) << 8) | packet[2]);
    // ... 더 많은 필드
}
#endif
```

#### 6.2 진단 도구
```cpp
UCLASS()
class USRTDiagnostics : public UObject
{
public:
    UFUNCTION(BlueprintCallable)
    static void CaptureNetworkTrace(float Duration);
    
    UFUNCTION(BlueprintCallable)
    static void DumpEncoderStats();
    
    UFUNCTION(BlueprintCallable)
    static void SimulateNetworkIssue(float PacketLoss, int32 Latency);
    
    UFUNCTION(BlueprintCallable)
    static FString GetSystemInfo();
};
```

### 7. 배포 전 체크리스트

#### 7.1 코드 품질
- [ ] 모든 단위 테스트 통과
- [ ] 코드 커버리지 > 80%
- [ ] 정적 분석 경고 0개
- [ ] 메모리 누수 없음 (Valgrind/AddressSanitizer)

#### 7.2 성능 기준
- [ ] 1080p 30fps @ < 10ms 인코딩
- [ ] CPU 사용률 < 30%
- [ ] 메모리 사용 < 500MB
- [ ] 네트워크 대역폭 < 10Mbps

#### 7.3 호환성
- [ ] OBS 2.8+ 모든 버전
- [ ] VLC 3.0+ 모든 버전
- [ ] Windows 10 1909+
- [ ] 언리얼 엔진 5.3+

#### 7.4 문서화
- [ ] API 문서 100% 완성
- [ ] 사용자 가이드 작성
- [ ] 트러블슈팅 FAQ
- [ ] 성능 튜닝 가이드

### 8. 출시 후 모니터링

#### 8.1 텔레메트리
```cpp
// 익명 사용 통계 수집 (opt-in)
struct TelemetryData {
    FString Version;
    FString Platform;
    int32 StreamingSessions;
    float AvgBitrate;
    float AvgCPUUsage;
    int32 CrashCount;
};
```

#### 8.2 자동 업데이트
- 버전 체크 시스템
- 패치 노트 표시
- 자동 다운로드 (선택)

### 9. 결론

이 계획서를 따라 구현하면:
1. **OBS/VLC 완벽 호환** 달성
2. **상용 품질의 안정성** 확보
3. **우수한 성능** 보장
4. **확장 가능한 구조** 구축

Phase 3 완료 시 CineSRT는 실제 방송 현장에서 사용 가능한 전문가급 도구가 될 것입니다.

---

**다음 단계**: Week 1 시작 - FFmpeg 통합부터 차근차근 진행