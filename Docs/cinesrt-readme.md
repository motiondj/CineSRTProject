# CineSRTStream - 언리얼 엔진 SRT 스트리밍 플러그인

언리얼 엔진에서 실시간으로 카메라 영상을 SRT(Secure Reliable Transport) 프로토콜로 스트리밍하는 플러그인입니다.

## 🚨 중요: Git Clone 후 필수 확인 사항

이 저장소는 **빌드하기 어려운 Windows 라이브러리들을 포함**하고 있습니다:
- ✅ OpenSSL 정적 라이브러리 (libssl.lib, libcrypto.lib)
- ✅ SRT 정적 라이브러리 (srt_static.lib)
- ✅ pthread-win32 라이브러리 (pthreadVC3.lib)

**이 파일들이 없으면 다시 빌드하는데 1-2주가 걸릴 수 있습니다!**

## 📁 프로젝트 구조

```
CineSRTProject/
├── 📁 BuildTools/                    # 라이브러리 빌드 도구 (선택적 포함)
│   ├── OpenSSL/install/              # ⭐ OpenSSL 빌드 결과물
│   │   ├── lib/                      # libssl.lib, libcrypto.lib
│   │   └── include/                  # 헤더 파일들
│   ├── srt/srt/_build/Release/      # ⭐ SRT 빌드 결과물
│   │   └── srt_static.lib            # SRT 정적 라이브러리
│   └── vcpkg/installed/              # ⭐ vcpkg 패키지들
│       └── x64-windows-static/lib/   # pthreadVC3.lib 등
│
├── 📁 UnrealProject/
│   └── SRTStreamTest/
│       └── 📁 Plugins/
│           └── 📁 CineSRTStream/     # ⭐ 플러그인 핵심
│               ├── CineSRTStream.uplugin
│               ├── 📁 Source/
│               │   └── CineSRTStream/
│               │       ├── CineSRTStream.Build.cs
│               │       ├── Public/
│               │       └── Private/
│               └── 📁 ThirdParty/    # ⭐ 필수 라이브러리들
│                   └── SRT/
│                       ├── include/   # SRT 헤더 파일들
│                       └── lib/Win64/ # 모든 .lib 파일들
│
└── 📁 TestPrograms/                  # 테스트 프로그램들
    ├── test_openssl/                 # OpenSSL 테스트
    ├── test_srt/                     # SRT 통신 테스트
    └── receiver/                     # 수신 테스트
```

## 🔑 핵심 파일 위치

### 꼭 있어야 하는 라이브러리 파일들

**플러그인 내장 라이브러리:**
```
Plugins/CineSRTStream/ThirdParty/SRT/lib/Win64/
├── srt_static.lib      (10-20 MB) - SRT 라이브러리
├── libssl.lib          (5 MB)     - OpenSSL SSL
├── libcrypto.lib       (30 MB)    - OpenSSL Crypto
└── pthreadVC3.lib      (100 KB)   - Windows pthread
```

**빌드 도구 (선택적):**
```
BuildTools/
├── OpenSSL/install/lib/          # OpenSSL 원본
├── srt/srt/_build/Release/       # SRT 원본
└── vcpkg/installed/.../lib/      # vcpkg 라이브러리들
```

## 🚀 빠른 시작

### 1. 저장소 클론
```bash
git clone https://github.com/yourname/CineSRTStream.git
cd CineSRTStream
```

### 2. 라이브러리 확인
```powershell
# 필수 라이브러리 확인
dir UnrealProject\SRTStreamTest\Plugins\CineSRTStream\ThirdParty\SRT\lib\Win64\

# 다음 파일들이 있어야 함:
# - srt_static.lib
# - libssl.lib  
# - libcrypto.lib
# - pthreadVC3.lib
```

### 3. 언리얼 프로젝트 열기
1. `UnrealProject/SRTStreamTest/SRTStreamTest.uproject` 우클릭
2. "Generate Visual Studio project files" 선택
3. Visual Studio에서 열기
4. Development Editor로 빌드

## ⚠️ 주의사항

### Git LFS 사용 권장
큰 라이브러리 파일들 때문에 Git LFS 사용을 권장합니다:
```bash
# Git LFS 설치 후
git lfs track "*.lib"
git lfs track "*.dll"
git add .gitattributes
```

### 라이브러리가 없을 경우
만약 ThirdParty 라이브러리들이 없다면:
1. `docs/Windows빌드가이드.md` 참조
2. 최소 1-2주 예상
3. Windows + OpenSSL + SRT는 매우 어려움

## 📚 문서

- `docs/개발계획서_v1.0.md` - 전체 개발 가이드
- `docs/Windows빌드가이드.md` - 라이브러리 빌드 방법
- `docs/사용법.md` - 플러그인 사용 방법

## 🛠️ 개발 환경

- **OS**: Windows 10/11 (64-bit)
- **언리얼 엔진**: 5.3 이상
- **Visual Studio**: 2022 Community 이상
- **GPU**: RTX 4090 권장 (NVENC 사용시)

## 📋 개발 현황

- [x] Phase 1: OpenSSL + SRT 통합
- [x] Phase 2: 언리얼 플러그인 기본 구조
- [ ] Phase 3: H.264/MPEG-TS 인코딩
- [ ] Phase 4: 멀티 카메라 지원

## 🤝 기여하기

1. 이 저장소를 Fork
2. Feature 브랜치 생성 (`git checkout -b feature/AmazingFeature`)
3. 변경사항 Commit (`git commit -m 'Add some AmazingFeature'`)
4. 브랜치에 Push (`git push origin feature/AmazingFeature`)
5. Pull Request 생성

## 📄 라이선스

- 프로젝트 코드: MIT License
- SRT: MPL-2.0 License
- OpenSSL: Apache License 2.0
- FFmpeg: LGPL 2.1 (사용시)

## 💡 팁

**빌드 시간 단축:**
- ThirdParty 폴더를 통째로 백업해두세요
- 다른 프로젝트에서도 재사용 가능합니다
- Windows SRT 빌드는 정말 어렵습니다

**문제 해결:**
- 링크 에러: ThirdParty 라이브러리 확인
- 헤더 못찾음: include 경로 확인
- 실행시 크래시: DLL 의존성 확인