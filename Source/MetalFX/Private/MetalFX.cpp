// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalFX.h"
#include "MetalFXHelper.h"
#include "MetalFXSpatialUpscalerCore.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscalerCore.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFXViewExtension.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RenderingThread.h"

IMPLEMENT_MODULE(FMetalFXModule, MetalFX)

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

static void ApplyMetalFXSettingsToCVars(const UMetalFXSettings& Settings)
{
	// CVar 세팅
	// Use the registered CVar objects directly. Repeating case-sensitive string
	// lookups here made settings import vulnerable to spelling drift.
	SetBoolCVarWithCurrentPriorityIfChanged(CVarEnableMetalFXInEditor.AsVariable(), Settings.bEnableInEditor);
	SetBoolCVarWithCurrentPriorityIfChanged(CVarMetalFXDebugDisplay.AsVariable(), Settings.bDebugDisplay);
	SetIntCVarWithCurrentPriorityIfChanged(CVarMetalFXUpscalerMode.AsVariable(), static_cast<int32>(Settings.UpscalerMode));
	SetFloatCVarWithCurrentPriorityIfChanged(CVarMetalFXSharpness.AsVariable(), Settings.Sharpness);
	SetIntCVarWithCurrentPriorityIfChanged(CVarMetalFXQualityMode.AsVariable(), static_cast<int32>(Settings.QualityMode));
	SetBoolCVarWithCurrentPriorityIfChanged(CVarMetalFXAutoScalingFromEngine.AsVariable(), Settings.bAutoScalingFromEngine);
	SetIntCVarWithCurrentPriorityIfChanged(CVarMetalFXJitterMode.AsVariable(), Settings.JitterMode);
	SetFloatCVarWithCurrentPriorityIfChanged(CVarMetalFXMotionVectorScaleX.AsVariable(), Settings.MotionVectorScaleX);
	SetFloatCVarWithCurrentPriorityIfChanged(CVarMetalFXMotionVectorScaleY.AsVariable(), Settings.MotionVectorScaleY);

	// Refresh the controller only after both quality inputs are finalized.
	ApplyMetalFXQualityModeToScreenPercentage(Settings.QualityMode);

	// Apply Enabled last so its change callback observes the finalized
	// quality CVar.
	SetBoolCVarWithCurrentPriorityIfChanged(CVarEnableMetalFX.AsVariable(), Settings.bEnabled);
}

template <typename TCore>
static TCore* GetTypedMetalFXCore(FMetalFXUpscalerCore* Core, EMetalFXUpscalerType ExpectedType, const TCHAR* CoreName)
{
	if (!Core)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX %s Core request failed because the Core has not been created."), CoreName);
		return nullptr;
	}

	if (!Core->IsInitialized())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX %s Core request failed because the Core is not initialized."), CoreName);
		return nullptr;
	}

	if (Core->GetUpscalerType() != ExpectedType)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX %s Core request failed because the selected Core mode is %d."), CoreName, static_cast<int32>(Core->GetUpscalerType()));
		return nullptr;
	}

	return static_cast<TCore*>(Core);
}

//----------------------Macro Checker--------------------
#if METALFX_PLUGIN_ENABLED
//Valid Mac Environment Check
#if (WITH_METALFX_TARGET_MAC && !PLATFORM_MAC) || (!WITH_METALFX_TARGET_MAC && PLATFORM_MAC)
#error "MetalFX Mac target definitions do not match PLATFORM_MAC."
#endif

//Valid iOS Environment Check
#if (WITH_METALFX_TARGET_IOS && !PLATFORM_IOS) || (!WITH_METALFX_TARGET_IOS && PLATFORM_IOS)
#error "MetalFX iOS target definitions do not match PLATFORM_IOS."
#endif

//Type Duplicated Check
#if METALFX_NATIVE && METALFX_METALCPP
#error "MetalFX must select exactly one Metal API binding."
#endif

//Type Not Selected Check
#if !(METALFX_NATIVE || METALFX_METALCPP)
#error "MetalFX must select a Metal API binding when the plugin is enabled."
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
		// OnPostEngineInit runs after RHI initialization. One callback guarantees
		// settings are applied before RHI-dependent support detection and Core creation.
		OnPostRHIInitialized = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMetalFXModule::HandlePostRHIInitialized);
		UE_LOG(LogMetalFX, Log, TEXT("MetalFX Upscaling Module Start"));
	}
}

void FMetalFXModule::ShutdownModule()
{
	// Finalize Screen Percentage ownership. Production restores the activation
	// snapshot, while METALFX_DEBUG preserves the current test value.
	RestoreMetalFXScreenPercentage();

	FCoreDelegates::OnPostEngineInit.Remove(OnPostRHIInitialized);

	// Stop new ViewExtension activation before destroying the module-owned Core.
	MetalSupport = EMetalSupportDevice::NotSupported;
	MetalFXSupport = EMetalFXSupportReason::NotSupported;
	SupportedUpscalerType = EMetalFXUpscalerType::None;
	MetalFXViewExtension = nullptr;

	// Existing ViewFamily upscaler adapters and queued RDG/RHI lambdas only keep
	// non-owning Core pointers, so finish render work before releasing the Core.
	if (MetalFXUpscaler)
	{
		FlushRenderingCommands();
	}

	MetalFXUpscaler.Reset();
	SelectedUpscalerType = EMetalFXUpscalerType::None;
	UE_LOG(LogMetalFX, Log, TEXT("MetalFX Upscaling Module Shutdown"));
}

EMetalSupportDevice FMetalFXModule::GetMetalSupport() const
{
	return MetalSupport;
}

EMetalFXSupportReason FMetalFXModule::GetMetalFXSupportReason() const
{
	return MetalFXSupport;
}

EMetalFXUpscalerType FMetalFXModule::GetSupportedUpscalerType() const
{
	return SupportedUpscalerType;
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
	return GetTypedMetalFXCore<FMetalFXTemporalUpscalerCore>(GetMetalFXUpscaler(), EMetalFXUpscalerType::Temporal, TEXT("Temporal"));
#endif
	return nullptr;
}

FMetalFXSpatialUpscalerCore* FMetalFXModule::GetMetalFXSpatialUpscaler()
{
#if METALFX_PLUGIN_ENABLED
	return GetTypedMetalFXCore<FMetalFXSpatialUpscalerCore>(GetMetalFXUpscaler(), EMetalFXUpscalerType::Spatial, TEXT("Spatial"));
#endif
	return nullptr;
}

bool FMetalFXModule::IsMetalFXSupported() const
{
	const bool bMetalRHISupported = MetalSupport == EMetalSupportDevice::Supported;
	const bool bMetalFXRuntimeSupported = MetalFXSupport == EMetalFXSupportReason::Supported;
	return bMetalRHISupported && bMetalFXRuntimeSupported;
}

FMetalFXUpscalerCore* FMetalFXModule::CreateMetalFXUpscaler(EMetalFXUpscalerType RequestedType)
{
#if METALFX_PLUGIN_ENABLED
	check(IsInGameThread());

	const bool bMetalFXSupported = IsMetalFXSupported();
	const bool bRequestedTypeSelected = RequestedType != EMetalFXUpscalerType::None;
	const bool bRequestedTypeSupported = SupportedUpscalerType == RequestedType;
	if (!bMetalFXSupported || !bRequestedTypeSelected || !bRequestedTypeSupported)
	{
		return nullptr;
	}

	//이론적으로는 진입해서는 안되는 구간임
	// Reject sentinel and out-of-range values before selecting a concrete Core.
	const bool bTemporalRequested = RequestedType == EMetalFXUpscalerType::Temporal;
	const bool bSpatialRequested = RequestedType == EMetalFXUpscalerType::Spatial;
	if (!bTemporalRequested && !bSpatialRequested)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Ignoring invalid MetalFX Core mode request: %d"), static_cast<int32>(RequestedType));
		return nullptr;
	}

	if (MetalFXUpscaler)
	{
		if (SelectedUpscalerType != RequestedType)
		{
			UE_LOG(LogMetalFX, Warning, TEXT("Ignoring MetalFX Core mode change from %d to %d. The first requested mode remains fixed for this run."), static_cast<int32>(SelectedUpscalerType), static_cast<int32>(RequestedType));
			return nullptr;
		}

		return MetalFXUpscaler.Get();
	}

	// The startup RHI initialization path creates exactly one concrete Core.
	// Runtime Enabled changes only control adapter activation and never recreate
	// the Core or replace its concrete type.
	switch (RequestedType)
	{
	case EMetalFXUpscalerType::Temporal:
		MetalFXUpscaler = MakeUnique<FMetalFXTemporalUpscalerCore>();
		break;
	case EMetalFXUpscalerType::Spatial:
		MetalFXUpscaler = MakeUnique<FMetalFXSpatialUpscalerCore>();
		break;
	default:
		return nullptr;
	}

	if (!MetalFXUpscaler)
	{
		return nullptr;
	}

	SelectedUpscalerType = RequestedType;
	MetalFXUpscaler->Initialize();

	UE_LOG(LogMetalFX, Log, TEXT("Created MetalFX Core for mode %d."), static_cast<int32>(SelectedUpscalerType));

	return MetalFXUpscaler.Get();
#else
	return nullptr;
#endif
}

void FMetalFXModule::HandlePostRHIInitialized()
{
	// Apply the startup CVars before selecting and creating the single Core.
	// Import project settings after the engine has registered its CVars, then use
	// that finalized startup configuration for the module-lifetime Core.
	if (const UMetalFXSettings* Settings = GetDefault<UMetalFXSettings>())
	{
		ApplyMetalFXSettingsToCVars(*Settings);
	}

	if (!GDynamicRHI)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX requires an Apple Metal RHI."));
		MetalSupport = EMetalSupportDevice::NotSupported;
		return;
	}

	MetalSupport = (IsMetalPlatform(GMaxRHIShaderPlatform) ? EMetalSupportDevice::Supported : EMetalSupportDevice::NotSupported);

	if (MetalSupport == EMetalSupportDevice::Supported)
	{
	#if METALFX_PLUGIN_ENABLED
		SupportedUpscalerType = FMetalFXUpscalerCore::QuerySupportedUpscalerType();
		MetalFXSupport = FMetalFXUpscalerCore::QuerySupportReason(SupportedUpscalerType);
		//SupportedUpscalerType = EMetalFXUpscalerType::Spatial;
		const bool bMetalFXRuntimeSupported = MetalFXSupport == EMetalFXSupportReason::Supported;
		const bool bHasSupportedUpscaler = SupportedUpscalerType != EMetalFXUpscalerType::None;
		if (bMetalFXRuntimeSupported && bHasSupportedUpscaler)
		{
			CreateMetalFXUpscaler(SupportedUpscalerType);
		}
#endif
	}

	if (MetalSupport == EMetalSupportDevice::Supported)
	{
		// The view extension is also used to show debug status on Metal RHI,
		// even when the current device cannot activate MetalFX.
		MetalFXViewExtension = FSceneViewExtensions::NewExtension<FMetalFXViewExtension>();
	}

	if (IsMetalFXSupported())
	{
#if METALFX_PLUGIN_ENABLED
		if (MetalFXUpscaler)
		{
			UE_LOG(LogMetalFX, Log, TEXT("MetalFX is available and its upscaler Core is ready."));
		}
		else
		{
			UE_LOG(LogMetalFX, Error, TEXT("MetalFX is supported, but its upscaler Core could not be created."));
		}
#endif
	}
	else if (MetalSupport != EMetalSupportDevice::Supported)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is unavailable because the active RHI is not Metal."));
	}
	else
	{
		// QuerySupportReason already emitted the specific device/framework reason.
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX activation is unavailable. SupportReason=%d."), static_cast<int32>(MetalFXSupport));
	}
}
