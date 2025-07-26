// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRTTransportStream.h"
#include "CineSRTStream.h"  // 이것도 추가!
#include "HAL/PlatformTime.h"

// AV_NOPTS_VALUE 정의
#ifndef AV_NOPTS_VALUE
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))
#endif

FSRTTransportStream::FSRTTransportStream()
    : bIsInitialized(false)
    , TotalPackets(0)
    , TotalBytes(0)
    , LastPCR(0)
    , LastPAT(0)
    , LastPMT(0)
    , StartTime(0.0)
{
    FMemory::Memset(ContinuityCounter, 0, sizeof(ContinuityCounter));
}

FSRTTransportStream::~FSRTTransportStream()
{
    Shutdown();
}

bool FSRTTransportStream::Initialize(const FConfig& InConfig)
{
    Config = InConfig;
    StartTime = FPlatformTime::Seconds();
    bIsInitialized = true;
    
    UE_LOG(LogCineSRTStream, Log, TEXT("SRTTransportStream: Initialized with service ID %d, video PID 0x%04X"),
        Config.ServiceID, Config.VideoPID);
    
    return true;
}

void FSRTTransportStream::Shutdown()
{
    bIsInitialized = false;
    UE_LOG(LogCineSRTStream, Log, TEXT("SRTTransportStream: Shutdown complete"));
}

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
        
        // PCR 쓰기 (임시 구현)
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

void FSRTTransportStream::GeneratePMT(TArray<uint8>& OutPacket)
{
    uint8 packet[TS_PACKET_SIZE];
    FMemory::Memset(packet, 0xFF, TS_PACKET_SIZE);
    
    // TS 헤더
    packet[0] = TS_SYNC_BYTE;
    packet[1] = 0x40 | ((Config.PMTPID >> 8) & 0x1F);  // payload_unit_start_indicator = 1
    packet[2] = Config.PMTPID & 0xFF;
    packet[3] = 0x10 | (ContinuityCounter[Config.PMTPID] & 0x0F);
    
    ContinuityCounter[Config.PMTPID] = (ContinuityCounter[Config.PMTPID] + 1) & 0x0F;
    
    int offset = 4;
    
    // Pointer field
    packet[offset++] = 0x00;
    
    // PMT 시작
    packet[offset++] = 0x02;  // table_id
    packet[offset++] = 0xB0;  // section_syntax_indicator = 1
    
    // Section length (나중에 채움)
    int length_offset = offset;
    offset += 2;
    
    // Program number
    packet[offset++] = (Config.ServiceID >> 8) & 0xFF;
    packet[offset++] = Config.ServiceID & 0xFF;
    
    // Version, current_next_indicator
    packet[offset++] = 0xC1;
    
    // Section number, last section number
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    
    // PCR PID
    packet[offset++] = 0xE0 | ((Config.PCRPID >> 8) & 0x1F);
    packet[offset++] = Config.PCRPID & 0xFF;
    
    // Program info length
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    
    // Video stream
    packet[offset++] = 0x1B;  // stream_type (H.264)
    packet[offset++] = 0xE0 | ((Config.VideoPID >> 8) & 0x1F);
    packet[offset++] = Config.VideoPID & 0xFF;
    packet[offset++] = 0xF0;  // ES info length
    packet[offset++] = 0x00;
    
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

void FSRTTransportStream::GenerateNullPacket(TArray<uint8>& OutPacket)
{
    uint8 packet[TS_PACKET_SIZE];
    FMemory::Memset(packet, 0xFF, TS_PACKET_SIZE);
    
    // TS 헤더
    packet[0] = TS_SYNC_BYTE;
    packet[1] = (TS_NULL_PID >> 8) & 0x1F;
    packet[2] = TS_NULL_PID & 0xFF;
    packet[3] = 0x10 | (ContinuityCounter[TS_NULL_PID] & 0x0F);
    
    ContinuityCounter[TS_NULL_PID] = (ContinuityCounter[TS_NULL_PID] + 1) & 0x0F;
    
    OutPacket.Append(packet, TS_PACKET_SIZE);
}

uint32 FSRTTransportStream::CalculateCRC32(const uint8* data, int length)
{
    // MPEG-2 TS용 CRC32 테이블
    static const uint32 crc32_table[256] = {
        0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
        0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
        0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
        0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
        0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
        0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
        0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
        0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
        0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
        0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
        0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
        0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
        0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
        0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
        0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
        0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
        0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
        0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
        0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
        0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
        0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
        0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
        0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
        0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
        0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
        0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
        0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
        0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
        0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
        0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
        0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
        0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
        0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
        0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
        0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
        0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
        0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
        0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
        0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
        0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
        0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
        0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
        0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
    };
    
    uint32 crc = 0xffffffff;
    for (int i = 0; i < length; i++) {
        crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ data[i]) & 0xff];
    }
    return crc;
}

int64 FSRTTransportStream::GetCurrentPCR()
{
    // PCR은 27MHz 클럭 (90kHz * 300)
    double CurrentTime = FPlatformTime::Seconds() - StartTime;
    int64 pcr_base = (int64)(CurrentTime * 90000.0);
    int64 pcr_ext = 0;  // 확장 필드는 0으로
    
    return (pcr_base * 300) + pcr_ext;
} 