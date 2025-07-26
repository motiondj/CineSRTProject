# CineSRTStream Plugin

언리얼 엔진에서 실시간 SRT 스트리밍을 위한 플러그인

## 📋 시스템 요구사항

### 필수 소프트웨어
1. **Unreal Engine 5.3+**
2. **Visual Studio 2022**
3. **FFmpeg** (시스템 설치 필요)
   - 다운로드: https://www.gyan.dev/ffmpeg/builds/
   - 버전: 6.0 이상 (shared 빌드)
   - PATH 환경변수 설정 필수

### FFmpeg 설치 방법
```powershell
# PowerShell (관리자)
cd C:\
Invoke-WebRequest -Uri "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z" -OutFile "ffmpeg.7z"
# 7z로 압축 해제 후 C:\ffmpeg로 이동
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\ffmpeg\bin", "Machine")
```

## 🚀 플러그인 설치

1. `Plugins/CineSRTStream` 폴더를 프로젝트에 복사
2. 프로젝트 파일 재생성
3. Visual Studio에서 빌드
4. 언리얼 에디터에서 플러그인 활성화

## ⚠️ 중요 사항

**이 플러그인은 시스템에 설치된 FFmpeg를 사용합니다.**
- 플러그인에 FFmpeg DLL 미포함 (크기 및 라이선스 고려)
- 사용자가 직접 FFmpeg 설치 필요
- 업데이트 및 보안 패치 자동 적용

## 🔧 문제 해결

### "FFmpeg not found" 오류
1. FFmpeg가 C:\ffmpeg에 설치되었는지 확인
2. PATH에 C:\ffmpeg\bin이 추가되었는지 확인
3. 언리얼 엔진 재시작

### 플러그인이 로드되지 않음
1. FFmpeg shared 버전인지 확인 (static 아님)
2. Visual C++ 2022 재배포 패키지 설치
3. 프로젝트 다시 빌드

## 📄 라이선스
- 플러그인: MIT License
- FFmpeg: LGPL 2.1+ (별도 설치)
- SRT: MPL-2.0