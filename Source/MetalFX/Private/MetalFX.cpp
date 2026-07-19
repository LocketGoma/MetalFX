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

static void SetBoolCVarWithCurrentPriorityIfChanged(IConsoleVariable* CVar, bool Value)
{
	if (CVar && CVar->GetBool() != Value)
	{
		CVar->SetWithCurrentPriority(Value);
	}
}

static void SetIntCVarWithCurrentPriorityIfChanged(IConsoleVariable* CVar, int32 Value)
{
	if (CVar && CVar->GetInt() != Value)
	{
		CVar->SetWithCurrentPriority(Value);
	}
}

static void SetFloatCVarWithCurrentPriorityIfChanged(IConsoleVariable* CVar, float Value)
{
	if (CVar && !FMath::IsNearlyEqual(CVar->GetFloat(), Value))
	{
		CVar->SetWithCurrentPriority(Value);
	}
}

//----------------------Macro Checker--------------------
#if METALFX_PLUGIN_ENABLED
	//Valid Mac Environment Check
	#if (WITH_METALFX_TARGET_MAC && !PLATFORM_MAC) || (!WITH_METALFX_TARGET_MAC && PLATFORM_MAC)
		#error "Setting on Mac Platform, but Plugin and Platfrom Validation Failed."
	#endif

	//Valid iOS Environment Check
	#if (WITH_METALFX_TARGET_IOS && !PLATFORM_IOS) || (!WITH_METALFX_TARGET_IOS && PLATFORM_IOS)
		#error "Setting on ios Platform, but Plugin and Platfrom Validation Failed."
	#endif

	//Type Duplicated Check
	#if (METALFX_NATIVE && METALFX_METALCPP)
		#error "You must select a specific Metal SDK type. Cannot use multiple types."
	#endif

	//Type Not Selected Check
	#if (METALFX_PLUGIN_ENABLED && !(METALFX_NATIVE || METALFX_METALCPP))
		#error "You must select a specific Metal SDK type While Activate MetalFX."
	#endif
#endif
//----------------------Macro Checker--------------------(End)

void FMetalFXModule::StartupModule()
{
	if (IsRunningCommandlet())
	{
		return;
	}
	
	if (FApp::IsGame() || GIsEditor)
	{
		OnPostRHIInitialized = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMetalFXModule::HandlePostRHIInitialized);
		
		//콘솔 설정 추가
		FCoreDelegates::OnPostEngineInit.AddLambda([]() {
			const UMetalFXSettings* Settings = GetDefault<UMetalFXSettings>();
			if (!Settings)
			{
				return;
			}
			
			// CVar 세팅
			if (IConsoleVariable* CvarMetalFXEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.Enabled")))
			{				
				SetBoolCVarWithCurrentPriorityIfChanged(CvarMetalFXEnable, Settings->bEnabled);
			}

			if (IConsoleVariable* CvarMetalFXEnableInEditor = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.EnableInEditor")))
			{
				SetBoolCVarWithCurrentPriorityIfChanged(CvarMetalFXEnableInEditor, Settings->bEnableInEditor);
			}

			if (IConsoleVariable* CvarMetalFXDebugDisplay = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.DebugDisplay")))
			{
				SetBoolCVarWithCurrentPriorityIfChanged(CvarMetalFXDebugDisplay, Settings->bDebugDisplay);
			}
			
			if (IConsoleVariable* CvarMetalFXMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.UpscalerMode")))
			{				
				SetIntCVarWithCurrentPriorityIfChanged(CvarMetalFXMode, static_cast<int32>(Settings->UpscalerMode));
			}
			
			if (IConsoleVariable* CvarMetalFSharpness = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.Sharpness")))
			{
				SetFloatCVarWithCurrentPriorityIfChanged(CvarMetalFSharpness, Settings->Sharpness);
			}
			
			if (IConsoleVariable* CvarMetalFXQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.QualityMode")))
			{
				SetIntCVarWithCurrentPriorityIfChanged(CvarMetalFXQuality, static_cast<int32>(Settings->QualityMode));
				ApplyMetalFXQualityModeToScreenPercentage(Settings->QualityMode);
			}

			if (IConsoleVariable* CvarMetalFXJitterMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.JitterMode")))
			{
				SetIntCVarWithCurrentPriorityIfChanged(CvarMetalFXJitterMode, Settings->JitterMode);
			}

			if (IConsoleVariable* CvarMetalFXMotionVectorScaleX = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.MotionVectorScaleX")))
			{
				SetFloatCVarWithCurrentPriorityIfChanged(CvarMetalFXMotionVectorScaleX, Settings->MotionVectorScaleX);
			}

			if (IConsoleVariable* CvarMetalFXMotionVectorScaleY = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.MotionVectorScaleY")))
			{
				SetFloatCVarWithCurrentPriorityIfChanged(CvarMetalFXMotionVectorScaleY, Settings->MotionVectorScaleY);
			}
		});
		UE_LOG(LogMetalFX, Log, TEXT("MetalFX Temporal Upscaling Module Start"));
	}
}

void FMetalFXModule::ShutdownModule()
{
	MetalSupport = EMetalSupportDevice::NotSupported;
	MetalFXSupport = EMetalFXSupportReason::NotSupported;
	MetalFXViewExtension = nullptr;
	FCoreDelegates::OnPostEngineInit.Remove(OnPostRHIInitialized);
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
		UE_LOG(LogMetalFX, Warning, TEXT("Apple MetalFX requires an Apple's RHI"));
		MetalSupport = EMetalSupportDevice::NotSupported;
		return;
	}
	
	MetalSupport = (IsMetalPlatform(GMaxRHIShaderPlatform) ? EMetalSupportDevice::Supported : EMetalSupportDevice::NotSupported);
	
	if (MetalSupport == EMetalSupportDevice::Supported)
	{
		MetalFXSupport = FMetalFXUpscalerCore::GetIsSupportedDevice();
#if METALFX_PLUGIN_ENABLED		
		if (MetalFXSupport == EMetalFXSupportReason::Supported)
		{
			MetalFXUpscaler = MakeShared<FMetalFXUpscalerCore, ESPMode::ThreadSafe>();
			
			if(MetalFXUpscaler)
			{
				MetalFXUpscaler->Initialize();
			}
		}
#endif
	}
	else
	{
		MetalFXSupport = EMetalFXSupportReason::NotSupported;
	}

	if (MetalSupport == EMetalSupportDevice::Supported)
	{
		// The view extension is also used to show debug status on Metal RHI,
		// even when the current device cannot activate MetalFX.
		MetalFXViewExtension = FSceneViewExtensions::NewExtension<FMetalFXViewExtension>();
	}

	if (GetIsSupportedByRHI())
	{
		if (MetalFXUpscaler != nullptr)
		{
			UE_LOG(LogMetalFX, Log, TEXT("Apple MetalFX Enabled! Now Can Activate MetalFX."));
		}
		else
		{
			UE_LOG(LogMetalFX, Error, TEXT("Apple MetalFX Disabled. Upscaler Core not Generated."));	
		}
	}
	else
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Apple MetalFX Disabled Because It is not Supported Environment."));
	}
}

#undef LOCTEXT_NAMESPACE
	
