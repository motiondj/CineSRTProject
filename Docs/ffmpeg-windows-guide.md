# FFmpeg Windows 설치 및 사용 가이드

## 설치 방법

### 방법 1: 직접 다운로드 (권장) ⭐

#### 1. 다운로드
- 공식 빌드 사이트: https://www.gyan.dev/ffmpeg/builds/
- **"release builds"** 섹션에서 **"full"** 버전 다운로드
- 파일명: `ffmpeg-release-full.7z` (약 80MB)

#### 2. 압축 해제
```powershell
# C:\ffmpeg 폴더에 압축 해제
# 7-Zip 필요: https://www.7-zip.org/

# 압축 해제 후 폴더 구조:
# C:\ffmpeg\
#   ├── bin\
#   │   ├── ffmpeg.exe   (인코더/디코더)
#   │   ├── ffplay.exe   (플레이어)
#   │   └── ffprobe.exe  (분석 도구)
#   ├── doc\
#   └── presets\
```

#### 3. 환경 변수 추가

**PowerShell 방법 (관리자 권한)**:
```powershell
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\ffmpeg\bin", [EnvironmentVariableTarget]::Machine)
```

**수동 방법**:
1. Windows 키 + X → 시스템
2. 고급 시스템 설정 → 환경 변수
3. 시스템 변수에서 `Path` 선택 → 편집
4. 새로 만들기 → `C:\ffmpeg\bin` 입력
5. 확인 → 확인 → 확인

#### 4. 설치 확인
```powershell
# 새 PowerShell 또는 CMD 창을 열고
ffmpeg -version
ffplay -version
ffprobe -version
```

### 방법 2: Chocolatey 패키지 관리자
```powershell
# Chocolatey가 설치되어 있다면
choco install ffmpeg

# 또는 전체 버전
choco install ffmpeg-full
```

### 방법 3: Scoop 패키지 관리자
```powershell
# Scoop이 설치되어 있다면
scoop install ffmpeg

# 또는
scoop bucket add extras
scoop install ffmpeg-full
```

## CineSRT 프로젝트에서 사용법

### 1. Raw 이미지 재생 (언리얼 캡처 확인)
```powershell
# BGRA 형식의 raw 이미지 재생
ffplay -f rawvideo -pixel_format bgra -video_size 1920x1080 frame_1920x1080.raw

# 옵션 설명:
# -f rawvideo       : raw 비디오 형식 지정
# -pixel_format bgra: Blue-Green-Red-Alpha 픽셀 순서
# -video_size       : 이미지 해상도

# 다른 해상도 예시
ffplay -f rawvideo -pixel_format bgra -video_size 3840x2160 frame_3840x2160.raw  # 4K
ffplay -f rawvideo -pixel_format bgra -video_size 1280x720 frame_1280x720.raw    # HD
```

### 2. SRT 스트림 수신 테스트
```powershell
# SRT 스트림 실시간 재생
ffplay -i "srt://127.0.0.1:9001?mode=listener"

# 암호화된 SRT (Phase 3 이후)
ffplay -i "srt://127.0.0.1:9001?mode=listener&passphrase=YourSecurePassphrase"

# 추가 옵션
ffplay -i "srt://127.0.0.1:9001?mode=listener" -fflags nobuffer -flags low_delay
```

### 3. SRT 스트림 녹화
```powershell
# MP4로 녹화
ffmpeg -i "srt://127.0.0.1:9001?mode=listener" -c:v copy output.mp4

# H.264로 재인코딩하며 녹화
ffmpeg -i "srt://127.0.0.1:9001?mode=listener" -c:v libx264 -preset fast output.mp4

# 시간 제한 녹화 (30초)
ffmpeg -i "srt://127.0.0.1:9001?mode=listener" -t 30 output.mp4
```

### 4. Raw 이미지를 비디오로 변환
```powershell
# Raw → MP4
ffmpeg -f rawvideo -pixel_format bgra -video_size 1920x1080 -i frame_1920x1080.raw -c:v libx264 frame.mp4

# Raw → PNG
ffmpeg -f rawvideo -pixel_format bgra -video_size 1920x1080 -i frame_1920x1080.raw frame.png

# Raw → 고품질 MP4
ffmpeg -f rawvideo -pixel_format bgra -video_size 1920x1080 -i frame_1920x1080.raw \
       -c:v libx264 -crf 18 -preset slow frame_hq.mp4
```

### 5. 스트림 정보 확인
```powershell
# SRT 스트림 정보
ffprobe -i "srt://127.0.0.1:9001?mode=listener"

# Raw 파일 크기로 해상도 추측
# 파일 크기 = 너비 × 높이 × 4 (BGRA)
# 8,294,400 bytes = 1920 × 1080 × 4
```

## 유용한 FFmpeg 명령어

### 기본 정보
```powershell
# 지원 코덱 목록
ffmpeg -codecs

# 지원 포맷 목록
ffmpeg -formats

# 지원 프로토콜 목록 (SRT 확인)
ffmpeg -protocols | findstr srt

# 하드웨어 가속 확인
ffmpeg -hwaccels
```

### 디버깅
```powershell
# 상세 로그와 함께 재생
ffplay -loglevel debug -i "srt://127.0.0.1:9001?mode=listener"

# 통계 표시하며 재생
ffplay -stats -i "srt://127.0.0.1:9001?mode=listener"
```

### 성능 최적화
```powershell
# GPU 가속 사용 (NVIDIA)
ffmpeg -hwaccel cuda -i input.mp4 -c:v h264_nvenc output.mp4

# 멀티스레드
ffmpeg -threads 0 -i input.mp4 output.mp4
```

## 문제 해결

### "ffmpeg is not recognized" 오류
1. 환경 변수 Path가 제대로 설정되었는지 확인
2. 새 명령 프롬프트 창을 열어야 함
3. `where ffmpeg` 명령으로 경로 확인

### SRT 프로토콜을 찾을 수 없음
```powershell
# SRT 지원 확인
ffmpeg -protocols 2>&1 | findstr srt

# 없다면 full 버전 재설치 필요
```

### Raw 이미지가 깨져 보임
- 픽셀 형식 확인: `bgra` vs `rgba` vs `argb`
- 해상도 확인: 파일 크기 ÷ 4 = 픽셀 수
- 엔디안 확인: `-pixel_format bgra` vs `-pixel_format bgrale`

## CineSRT 프로젝트 전용 스크립트

### test_stream.bat
```batch
@echo off
echo Testing SRT stream from Unreal...
ffplay -i "srt://127.0.0.1:9001?mode=listener" -fflags nobuffer
pause
```

### convert_frames.bat
```batch
@echo off
echo Converting raw frames to MP4...
for %%f in (*.raw) do (
    ffmpeg -f rawvideo -pixel_format bgra -video_size 1920x1080 -i "%%f" "%%~nf.mp4"
)
echo Done!
pause
```

### record_stream.bat
```batch
@echo off
set TIMESTAMP=%date:~-4%%date:~3,2%%date:~0,2%_%time:~0,2%%time:~3,2%%time:~6,2%
set TIMESTAMP=%TIMESTAMP: =0%
echo Recording to stream_%TIMESTAMP%.mp4
ffmpeg -i "srt://127.0.0.1:9001?mode=listener" -c:v copy "stream_%TIMESTAMP%.mp4"
pause
```

## 참고 링크
- FFmpeg 공식 문서: https://ffmpeg.org/documentation.html
- FFmpeg Wiki: https://trac.ffmpeg.org/wiki
- SRT with FFmpeg: https://github.com/Haivision/srt/wiki/Using-SRT-with-FFmpeg