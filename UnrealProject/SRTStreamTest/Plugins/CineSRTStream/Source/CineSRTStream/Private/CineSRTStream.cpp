// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineSRTStream.h"

// C++ 헤더들을 먼저 포함
#include <string>
#include <vector>
#include <memory>

// SRT 헤더 래퍼 사용
#include "SRTWrapper.h"

#define LOCTEXT_NAMESPACE "FCineSRTStreamModule"

DEFINE_LOG_CATEGORY(LogCineSRTStream);

void FCineSRTStreamModule::StartupModule()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("=== CineSRTStream Module Starting ==="));
    try {
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT \ub77c\uc774\ube0c\ub7ec\ub9ac \ucd08\uae30\ud654 \uc2dc\ub3c4 \uc911..."));
        if (!SRTWrapper::Initialize()) {
            UE_LOG(LogCineSRTStream, Error, TEXT("SRT \ub77c\uc774\ube0c\ub7ec\ub9ac \ucd08\uae30\ud654 \uc2e4\ud328"));
            bSRTInitialized = false;
            return;
        }
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT \ub77c\uc774\ube0c\ub7ec\ub9ac \ucd08\uae30\ud654 \uc131\uacf5"));
        bSRTInitialized = true;
        UE_LOG(LogCineSRTStream, Log, TEXT("\u2705 CineSRTStream module initialized successfully"));
        // 크래시 리포트용 시스템 정보 등록
    } catch (const std::exception& e) {
        UE_LOG(LogCineSRTStream, Error, TEXT("SRT \ub77c\uc774\ube0c\ub7ec\ub9ac \ucd08\uae30\ud654 \uc911 \uc608\uc678 \ubc1c\uc0dd: %s"), UTF8_TO_TCHAR(e.what()));
        bSRTInitialized = false;
    } catch (...) {
        UE_LOG(LogCineSRTStream, Error, TEXT("SRT \ub77c\uc774\ube0c\ub7ec\ub9ac \ucd08\uae30\ud654 \uc911 \uc54c \uc218 \uc5c6\ub294 \uc608\uc678 \ubc1c\uc0dd"));
        bSRTInitialized = false;
    }
}

void FCineSRTStreamModule::ShutdownModule()
{
    UE_LOG(LogCineSRTStream, Log, TEXT("=== CineSRTStream Module Shutting Down ==="));
    
    if (bSRTInitialized) {
        // SRT 라이브러리 정리
        SRTWrapper::Cleanup();
        bSRTInitialized = false;
    }
    
    UE_LOG(LogCineSRTStream, Log, TEXT("CineSRTStream 모듈 종료"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCineSRTStreamModule, CineSRTStream)