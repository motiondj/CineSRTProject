// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class CineSRTStream : ModuleRules
{
    public CineSRTStream(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        // Unreal Engine 5.5 C++20 지원
        CppStandard = CppStandardVersion.Cpp20;
        
        // 예외 처리 활성화 (SRT 필수)
        bEnableExceptions = true;
        bUseRTTI = true;
        
        // 경고 레벨 조정 (SRT 헤더 경고 방지)
        UndefinedIdentifierWarningLevel = WarningLevel.Warning;
        
        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Public")
            }
        );
        
        PrivateIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Private")
            }
        );
        
        // 언리얼 엔진 모듈 의존성
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "RenderCore",
                "RHI",
                "Renderer",
                "Projects"
            }
        );
        
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Sockets",
                "Networking",
                "Media",
                "MediaAssets"
            }
        );
        
        // 소스 파일 추가
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "RenderCore",
                "RHI"
            }
        );
        
        // 에디터 전용 모듈
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "UnrealEd",
                    "ToolMenus",
                    "EditorSubsystem"
                }
            );
        }
        
        // Windows 플랫폼 전용 설정
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // ThirdParty 경로 설정
            string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty"));
            string SRTPath = Path.Combine(ThirdPartyPath, "SRT");

            // Include 경로 추가 (SRT 헤더를 항상 최우선으로)
            PublicIncludePaths.Insert(0, Path.Combine(SRTPath, "include"));
            PublicIncludePaths.AddRange(new string[] {
                Path.Combine(SRTPath, "include", "srtcore"),
                Path.Combine(SRTPath, "include", "common"),
                Path.Combine(SRTPath, "include", "win")
            });

            // 라이브러리 경로
            string LibPath = Path.Combine(SRTPath, "lib", "Win64");
            
            // 라이브러리 파일 확인 및 추가
            string[] RequiredLibs = new string[] {
                "srt_static.lib",
                "libssl.lib",
                "libcrypto.lib",
                "pthreadVC3.lib"
            };
            
            foreach (string LibName in RequiredLibs)
            {
                string LibFullPath = Path.Combine(LibPath, LibName);
                if (File.Exists(LibFullPath))
                {
                    PublicAdditionalLibraries.Add(LibFullPath);
                    System.Console.WriteLine("CineSRTStream: Added library " + LibName);
                }
                else
                {
                    System.Console.WriteLine("CineSRTStream: WARNING - Missing library " + LibFullPath);
                }
            }
            
            // Windows 시스템 라이브러리
            PublicSystemLibraries.AddRange(new string[] {
                "ws2_32.lib",
                "Iphlpapi.lib",
                "Crypt32.lib",
                "Advapi32.lib",
                "User32.lib",
                "Gdi32.lib",
                "Ole32.lib",
                "Shell32.lib"
            });
            
                            // 전처리기 정의
                PublicDefinitions.AddRange(new string[] {
                    "WIN32_LEAN_AND_MEAN",
                    "NOMINMAX",
                    "SRT_STATIC=1",
                    "SRT_ENABLE_ENCRYPTION=1",
                    "SRT_VERSION_MAJOR=1",
                    "SRT_VERSION_MINOR=5",
                    "SRT_VERSION_PATCH=3",
                    "_CRT_SECURE_NO_WARNINGS",
                    "_WINSOCK_DEPRECATED_NO_WARNINGS",
                    "WINDOWS_IGNORE_PACKING_MISMATCH",
                    "__STDC_WANT_LIB_EXT1__=1",
                    "_HAS_EXCEPTIONS=1",
                    "_ALLOW_RUNTIME_LIBRARY_MISMATCH",
                    "_ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH"
                });
                
                // C 파일 컴파일 설정 (Private 소스 파일로 추가)
                PrivateIncludePathModuleNames.Add("CineSRTStream");
        }
    }
}
