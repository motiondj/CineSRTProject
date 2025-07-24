// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCineSRTStream, Log, All);

class FCineSRTStreamModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    
    /** Gets the module singleton */
    static inline FCineSRTStreamModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FCineSRTStreamModule>("CineSRTStream");
    }
    
    /** Checks if the module is loaded and available */
    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("CineSRTStream");
    }
    
    /** SRT 라이브러리 상태 확인 */
    bool IsSRTInitialized() const { return bSRTInitialized; }
    
private:
    bool bSRTInitialized = false;
    
    /** SRT 라이브러리 초기화 */
    bool InitializeSRT();
    
    /** SRT 라이브러리 정리 */
    void CleanupSRT();
};
