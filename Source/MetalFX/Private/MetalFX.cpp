// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalFX.h"
#include "MetalFXSettings.h"
#include "MetalFXViewExtension.h"
#include "MetalFXTemporalUpscaler.h"
#include "Interfaces/IPluginManager.h"
#include "DataDrivenShaderPlatformInfo.h"

IMPLEMENT_MODULE(FMetalFXModule, MetalFX)

#define LOCTEXT_NAMESPACE "FMetalFXModule"
DEFINE_LOG_CATEGORY(LogMetalFX);

//----------------------Macro Checker--------------------
#if METALFX_PLUGIN_ENABLED
	#if (WITH_METALFX_TARGET_MAC && !PLATFORM_MAC) || (!WITH_METALFX_TARGET_MAC && PLATFORM_MAC)
		#error "Setting on Mac Platform, but Plugin and Platfrom Validation Failed."
	#endif

	#if (WITH_METALFX_TARGET_IOS && !PLATFORM_IOS) || (!WITH_METALFX_TARGET_IOS && PLATFORM_IOS)
		#error "Setting on ios Platform, but Plugin and Platfrom Validation Failed."
	#endif
#endif

void FMetalFXModule::StartupModule()
{
	OnPostRHIInitialized = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMetalFXModule::HandlePostRHIInitialized);
	OnPostWorldBeginPlay = FWorldDelegates::OnPostWorldInitialization.AddRaw(this, &FMetalFXModule::HandleWorldBeginPlay);

	//Shader 부착
	FString BaseDir = IPluginManager::Get().FindPlugin("MetalFX")->GetBaseDir();
	FString ShaderDir = FPaths::Combine(BaseDir, TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/MetalFX"), ShaderDir);
	
//콘솔 설정 추가
	FCoreDelegates::OnPostEngineInit.AddLambda([]()
		{
			const UMetalFXSettings* Settings = GetDefault<UMetalFXSettings>();
			if (!Settings)
			{
				return;
			}

			// CVar 세팅
			if (IConsoleVariable* CvarMetalFXEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.Enabled")))
			{				
				CvarMetalFXEnable->Set(Settings->bEnabled, ECVF_SetByCode);
			}

			if (IConsoleVariable* CvarMetalFXMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.UpscalerMode")))
			{				
				CvarMetalFXMode->Set(Settings->Mode, ECVF_SetByCode);
			}
			
			if (IConsoleVariable* CvarMetalFSharpness = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.Sharpness")))
			{
				CvarMetalFSharpness->Set(Settings->Sharpness, ECVF_SetByCode);
			}
			
			if (IConsoleVariable* CvarMetalFXQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.QualityMode")))
			{
				CvarMetalFXQuality->Set(static_cast<int32>(Settings->QualityMode), ECVF_SetByCode);
			}
		});
	
	UE_LOG(LogMetalFX, Log, TEXT("MetalFX Temporal Upscaling Module Start"));
}

void FMetalFXModule::ShutdownModule()
{
	MetalSupport = EMetalSupportDevice::NotSupported;
	MetalFXSupport = EMetalFXSupportReason::NotSupported;
	MetalFXViewExtension = nullptr;
	FCoreDelegates::OnPostEngineInit.Remove(OnPostRHIInitialized);
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldBeginPlay);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	UE_LOG(LogMetalFX, Log, TEXT("MetalFX Temporal Upscaling Module Shutdown"));
}

EMetalSupportDevice FMetalFXModule::QueryMetalSupport() const
{
	return MetalSupport;
}

EMetalFXSupportReason FMetalFXModule::QueryMetalFXSupport() const
{
	return MetalFXSupport;
}

FMetalFXUpscalerCore* FMetalFXModule::GetMetalFXUpscaler() const
{
#if METALFX_PLUGIN_ENABLED
		return MetalFXUpscaler.Get();
#endif
	return nullptr;
}

bool FMetalFXModule::GetIsSupportedByRHI() const
{
	return ((MetalSupport == EMetalSupportDevice::Supported) && (MetalFXSupport == EMetalFXSupportReason::Supported));	
}

void FMetalFXModule::SetMetalFXUpscaler(TSharedPtr<FMetalFXUpscalerCore, ESPMode::ThreadSafe> Upscaler)
{
	MetalFXUpscaler = Upscaler;
}

void FMetalFXModule::HandlePostRHIInitialized()
{
	if (!GDynamicRHI)
	{
		UE_LOG(LogMetalFX, Log, TEXT("Apple MetalFX requires an Apple's RHI"));
		MetalSupport = EMetalSupportDevice::NotSupported;
	}
	
	MetalSupport = (IsMetalPlatform(GMaxRHIShaderPlatform) ? EMetalSupportDevice::Supported : EMetalSupportDevice::NotSupported);
	
	if (MetalSupport == EMetalSupportDevice::Supported)
	{
		MetalFXSupport = FMetalFXUpscalerCore::GetIsSupportedDevice();
#if METALFX_PLUGIN_ENABLED		
		if (MetalSupport == EMetalSupport::Supported)
		{
			MetalFXUpscaler = MakeShared<FMetalFXUpscalerCore, ESPMode::ThreadSafe>();
		}
#endif
	}
	else
	{
		MetalFXSupport = EMetalFXSupportReason::NotSupported;
	}

	if (GetIsSupportedByRHI())
	{
		//View Extension 등록
		MetalFXViewExtension = FSceneViewExtensions::NewExtension<FMetalFXViewExtension>();
		
		UE_LOG(LogMetalFX, Log, TEXT("Apple MetalFX Enabled! Now Can Activate MetalFX."));
	}
	else
	{
		UE_LOG(LogMetalFX, Log, TEXT("Apple MetalFX Disabled Because It is not Supported Environment."));			
	}
}

void FMetalFXModule::HandleWorldBeginPlay(UWorld* World, const UWorld::InitializationValues InitValue)
{
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
	if (World->IsGameWorld())
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, GetIsSupportedByRHI() ? FColor::Emerald : FColor::Red, FString::Printf(TEXT("Apple MetaFX %s"), GetIsSupportedByRHI() ? TEXT("Enabled") : TEXT("Disabled")), true);
	}
#endif
}
#undef LOCTEXT_NAMESPACE
	
