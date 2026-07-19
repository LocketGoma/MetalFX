// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalFX.h"
#include "MetalFXSpatialUpscalerCore.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscalerCore.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFXViewExtension.h"
#include "MetalFXTemporalUpscaler.h"
#include "Interfaces/IPluginManager.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RenderingThread.h"

IMPLEMENT_MODULE(FMetalFXModule, MetalFX)

#define LOCTEXT_NAMESPACE "FMetalFXModule"
DEFINE_LOG_CATEGORY(LogMetalFX);

FMetalFXModule::~FMetalFXModule() = default;

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
		//콘솔 설정 추가
		OnPostEngineInitSettings = FCoreDelegates::OnPostEngineInit.AddLambda([]() {
			const UMetalFXSettings* Settings = GetDefault<UMetalFXSettings>();
			if (!Settings)
			{
				return;
			}
			
			// CVar 세팅
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

			// Apply Enabled last so its change callback observes the finalized
			// quality CVar.
			if (IConsoleVariable* CvarMetalFXEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MetalFX.Enabled")))
			{
				SetBoolCVarWithCurrentPriorityIfChanged(CvarMetalFXEnable, Settings->bEnabled);
			}
		});
		// Apply the startup CVars before selecting and creating the single Core.
		OnPostRHIInitialized = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMetalFXModule::HandlePostRHIInitialized);
		UE_LOG(LogMetalFX, Log, TEXT("MetalFX Temporal Upscaling Module Start"));
	}
}

void FMetalFXModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitSettings);
	FCoreDelegates::OnPostEngineInit.Remove(OnPostRHIInitialized);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	// Stop new ViewExtension activation before destroying the module-owned Core.
	MetalSupport = EMetalSupportDevice::NotSupported;
	MetalFXSupport = EMetalFXSupportReason::NotSupported;
	MetalFXSupportedType = EMetalFXSupportedType::None;
	MetalFXViewExtension = nullptr;

	// Existing ViewFamily upscaler adapters and queued RDG/RHI lambdas only keep
	// non-owning Core pointers, so finish render work before releasing the Core.
	if (MetalFXUpscaler)
	{
		FlushRenderingCommands();
	}

	MetalFXUpscaler.Reset();
	SelectedUpscalerMode = EMetalFXUpscalerMode::None;
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

EMetalFXSupportedType FMetalFXModule::QueryMetalFXSupportedType() const
{
	return MetalFXSupportedType;
}

FMetalFXUpscalerCore* FMetalFXModule::GetMetalFXUpscaler() const
{
#if METALFX_PLUGIN_ENABLED
	return MetalFXUpscaler.Get();
#endif
	return nullptr;
}

FMetalFXTemporalUpscalerCore* FMetalFXModule::GetMetalFXTemporalUpscaler()
{
#if METALFX_PLUGIN_ENABLED
	FMetalFXUpscalerCore* Core = GetMetalFXUpscaler();
	if (!Core)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal Core request failed because the Core has not been created."));
		return nullptr;
	}

	if (!Core->IsInitialized())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal Core request failed because the Core is not initialized."));
		return nullptr;
	}

	if (Core->GetUpscalerMode() == EMetalFXUpscalerMode::Temporal)
	{
		return static_cast<FMetalFXTemporalUpscalerCore*>(Core);
	}

	UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal Core request failed because the selected Core mode is %d."), static_cast<int32>(Core->GetUpscalerMode()));
#endif
	return nullptr;
}

FMetalFXSpatialUpscalerCore* FMetalFXModule::GetMetalFXSpatialUpscaler()
{
#if METALFX_PLUGIN_ENABLED
	FMetalFXUpscalerCore* Core = GetMetalFXUpscaler();
	if (!Core)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Spatial Core request failed because the Core has not been created."));
		return nullptr;
	}

	if (!Core->IsInitialized())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Spatial Core request failed because the Core is not initialized."));
		return nullptr;
	}

	if (Core->GetUpscalerMode() == EMetalFXUpscalerMode::Spatial)
	{
		return static_cast<FMetalFXSpatialUpscalerCore*>(Core);
	}

	UE_LOG(LogMetalFX, Error, TEXT("MetalFX Spatial Core request failed because the selected Core mode is %d."), static_cast<int32>(Core->GetUpscalerMode()));
#endif
	return nullptr;
}

bool FMetalFXModule::GetIsSupportedByRHI() const
{
	return ((MetalSupport == EMetalSupportDevice::Supported) && (MetalFXSupport == EMetalFXSupportReason::Supported));	
}

FMetalFXUpscalerCore* FMetalFXModule::CreateMetalFXUpscaler(EMetalFXUpscalerMode RequestedMode)
{
#if METALFX_PLUGIN_ENABLED
	check(IsInGameThread());

	if (!GetIsSupportedByRHI()
		|| RequestedMode == EMetalFXUpscalerMode::None
		|| !FMetalFXUpscalerCore::IsUpscalerModeSupported(MetalFXSupportedType, RequestedMode))
	{
		return nullptr;
	}

	if (RequestedMode != EMetalFXUpscalerMode::Temporal
		&& RequestedMode != EMetalFXUpscalerMode::Spatial)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Ignoring invalid MetalFX Core mode request: %d"), static_cast<int32>(RequestedMode));
		return nullptr;
	}

	if (MetalFXUpscaler)
	{
		if (SelectedUpscalerMode != RequestedMode)
		{
			UE_LOG(LogMetalFX, Warning, TEXT("Ignoring MetalFX Core mode change from %d to %d. The first requested mode remains fixed for this run."), static_cast<int32>(SelectedUpscalerMode), static_cast<int32>(RequestedMode));
			return nullptr;
		}

		return MetalFXUpscaler.Get();
	}

	// The startup RHI initialization path creates exactly one concrete Core.
	// Runtime Enabled changes only control adapter activation and never recreate
	// the Core or replace its concrete type.
	switch (RequestedMode)
	{
	case EMetalFXUpscalerMode::Temporal:
		MetalFXUpscaler = MakeUnique<FMetalFXTemporalUpscalerCore>();
		break;
	case EMetalFXUpscalerMode::Spatial:
		MetalFXUpscaler = MakeUnique<FMetalFXSpatialUpscalerCore>();
		break;
	default:
		return nullptr;
	}

	if (!MetalFXUpscaler)
	{
		return nullptr;
	}

	SelectedUpscalerMode = RequestedMode;
	MetalFXUpscaler->Initialize();

	UE_LOG(LogMetalFX, Log, TEXT("Created MetalFX Core for mode %d."), static_cast<int32>(SelectedUpscalerMode));

	return MetalFXUpscaler.Get();
#else
	return nullptr;
#endif
}

void FMetalFXModule::HandlePostRHIInitialized()
{
	if (!GDynamicRHI)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Apple MetalFX requires an Apple's RHI"));
		MetalSupport = EMetalSupportDevice::NotSupported;
		return;
	}

	EMetalFXUpscalerMode StartupUpscalerMode = EMetalFXUpscalerMode::None;
#if METALFX_PLUGIN_ENABLED
	StartupUpscalerMode = static_cast<EMetalFXUpscalerMode>(CVarMetalFXUpscalerMode.GetValueOnGameThread());
#endif
	
	MetalSupport = (IsMetalPlatform(GMaxRHIShaderPlatform) ? EMetalSupportDevice::Supported : EMetalSupportDevice::NotSupported);
	
	if (MetalSupport == EMetalSupportDevice::Supported)
	{
#if METALFX_PLUGIN_ENABLED
		MetalFXSupportedType = FMetalFXUpscalerCore::GetMetalFXSupportedType();
		MetalFXSupport = FMetalFXUpscalerCore::GetMetalFXSupportReason();
		if (MetalFXSupport == EMetalFXSupportReason::Supported && FMetalFXUpscalerCore::IsUpscalerModeSupported(MetalFXSupportedType, StartupUpscalerMode))
		{
			CreateMetalFXUpscaler(StartupUpscalerMode);
		}
#endif
	}

	if (MetalSupport == EMetalSupportDevice::Supported)
	{
		// The view extension is also used to show debug status on Metal RHI,
		// even when the current device cannot activate MetalFX.
		MetalFXViewExtension = FSceneViewExtensions::NewExtension<FMetalFXViewExtension>();
	}

	if (GetIsSupportedByRHI())
	{
#if METALFX_PLUGIN_ENABLED
		if (StartupUpscalerMode == EMetalFXUpscalerMode::None)
		{
			UE_LOG(LogMetalFX, Log, TEXT("Apple MetalFX is available. Core creation was skipped because the startup mode is Off."));
		}
		else if (!FMetalFXUpscalerCore::IsUpscalerModeSupported(MetalFXSupportedType, StartupUpscalerMode))
		{
			UE_LOG(LogMetalFX, Warning, TEXT("Apple MetalFX is available, but the selected upscaler mode %d is not supported by this device."), static_cast<int32>(StartupUpscalerMode));
		}
		else if (MetalFXUpscaler)
		{
			UE_LOG(LogMetalFX, Log, TEXT("Apple MetalFX Enabled! Now Can Activate MetalFX."));
		}
		else
		{
			UE_LOG(LogMetalFX, Error, TEXT("Apple MetalFX Disabled. Upscaler Core not Generated."));
		}
#endif
	}
	else
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Apple MetalFX Disabled Because It is not Supported Environment."));
	}
}

#undef LOCTEXT_NAMESPACE
	
