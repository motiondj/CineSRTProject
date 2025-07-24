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
    
    // 안전한 SRT 라이브러리 초기화
    try {
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT 라이브러리 초기화 시도 중..."));
        
        // SRT 라이브러리 초기화
        if (!SRTWrapper::Initialize()) {
            UE_LOG(LogCineSRTStream, Error, TEXT("SRT 라이브러리 초기화 실패"));
            bSRTInitialized = false;
            return;
        }
        
        // 버전 정보는 일단 건너뛰고 기본 성공 메시지만 출력
        UE_LOG(LogCineSRTStream, Log, TEXT("SRT 라이브러리 초기화 성공"));
        
        bSRTInitialized = true;
        UE_LOG(LogCineSRTStream, Log, TEXT("✅ CineSRTStream module initialized successfully"));
        
    } catch (const std::exception& e) {
        UE_LOG(LogCineSRTStream, Error, TEXT("SRT 라이브러리 초기화 중 예외 발생: %s"), 
               UTF8_TO_TCHAR(e.what()));
        bSRTInitialized = false;
    } catch (...) {
        UE_LOG(LogCineSRTStream, Error, TEXT("SRT 라이브러리 초기화 중 알 수 없는 예외 발생"));
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