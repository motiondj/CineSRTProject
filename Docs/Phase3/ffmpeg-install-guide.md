# FFmpeg 시스템 설치 가이드

## 🚀 빠른 설치 (PowerShell)

```powershell
# 관리자 권한으로 PowerShell 실행

# 1. FFmpeg 다운로드
cd C:\
Invoke-WebRequest -Uri "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z" -OutFile "ffmpeg.7z"

# 2. 압축 해제 (7-Zip 필요)
& "C:\Program Files\7-Zip\7z.exe" x ffmpeg.7z

# 3. 폴더명 변경
$folder = Get-ChildItem "C:\ffmpeg-*" -Directory | Select-Object -First 1
Rename-Item $folder.FullName "ffmpeg"

# 4. PATH 추가 (시스템 환경변수)
[Environment]::SetEnvironmentVariable(
    "Path",
    $env:Path + ";C:\ffmpeg\bin",
    [EnvironmentVariableTarget]::Machine
)

# 5. 확인
ffmpeg -version
```

## 📋 수동 설치

### 1. FFmpeg 다운로드
- https://www.gyan.dev/ffmpeg/builds/
- `ffmpeg-release-full-shared.7z` 선택 (shared 중요!)

### 2. 압축 해제
- `C:\ffmpeg` 폴더에 압축 해제
- 구조:
  ```
  C:\ffmpeg\
  ├── bin\        (ffmpeg.exe, DLL 파일들)
  ├── include\    (헤더 파일들)
  └── lib\        (링크 라이브러리)
  ```

### 3. 시스템 PATH 추가
1. Windows 키 + X → 시스템
2. 고급 시스템 설정 → 환경 변수
3. 시스템 변수에서 `Path` 선택 → 편집
4. 새로 만들기 → `C:\ffmpeg\bin` 추가
5. 확인 → 확인 → 확인

### 4. 언리얼 엔진 재시작
- PATH 변경사항 적용을 위해 필수!

## ✅ 설치 확인

```cmd
ffmpeg -version
```

출력 예시:
```
ffmpeg version 6.1.1 Copyright (c) 2000-2023 the FFmpeg developers
built with gcc 13.2.0 (Rev5, Built by MSYS2 project)
```

## 🎯 플러그인에서 사용

이제 CineSRTStream 플러그인이 자동으로 시스템 FFmpeg를 감지하고 사용합니다!

### 장점
- ✅ FFmpeg 업데이트 독립적
- ✅ 플러그인 크기 50MB 감소
- ✅ 다른 프로그램과 공유
- ✅ 라이선스 분리

### 주의사항
- ⚠️ 사용자가 FFmpeg 설치 필요
- ⚠️ PATH 설정 필수
- ⚠️ shared 버전 사용 (DLL 포함)

## 🔧 문제 해결

### "ffmpeg is not recognized" 오류
- 언리얼 엔진 재시작
- 새 명령 프롬프트에서 테스트
- PATH 환경변수 확인

### DLL을 찾을 수 없음
- shared 버전인지 확인
- `C:\ffmpeg\bin`에 DLL 파일 확인
- Visual C++ 재배포 패키지 설치