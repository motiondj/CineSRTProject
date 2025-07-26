#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

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