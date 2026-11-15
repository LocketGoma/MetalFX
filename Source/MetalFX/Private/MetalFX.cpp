#include "MetalFX.h"
#include "MetalFXHelper.h"
#include "MetalFXSpatialUpscalerCore.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscalerCore.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFXViewExtension.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDevice.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCore.h"

IMPLEMENT_MODULE(FMetalFXModule, MetalFX)

DEFINE_LOG_CATEGORY(LogMetalFX);

FMetalFXModule::~FMetalFXModule() = default;

#if METALFX_PLUGIN_ENABLED
static TAutoConsoleVariable<bool> CVarMetalFXInitializeOnStartup(
	TEXT("r.MetalFX.InitializeOnStartup"),
	false,
	TEXT("Initialize MetalFX after the RHI is ready without changing r.MetalFX.Enabled. Set in Engine.ini or pass -MetalFXInitializeOnStartup."),
	ECVF_ReadOnly);
#endif

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

	// Apply the project default while honoring higher-priority console variable settings.
	IConsoleVariable* Enabled = CVarEnableMetalFX.AsVariable();
	if ((Enabled->GetFlags() & ECVF_SetByMask) <= ECVF_SetByProjectSetting && Enabled->GetBool() != Settings.bEnabled)
	{
		Enabled->Set(Settings.bEnabled ? 1 : 0, ECVF_SetByProjectSetting);
	}
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
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MetalFX"));
	if (Plugin)
	{
		const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/MetalFX"), ShaderDirectory);
	}

	if (IsRunningCommandlet())
	{
		return;
	}

	if (FApp::IsGame() || GIsEditor)
	{
		//콘솔 설정 추가
		// Apply startup settings after the RHI is ready so MetalFX can initialize on demand.
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
	for (IConsoleCommand*& Command : InitializeConsoleCommands)
	{
		if (Command)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command, false);
			Command = nullptr;
		}
	}
	if (OnMetalFXEnabledChanged.IsValid())
	{
		CVarEnableMetalFX.AsVariable()->OnChangedDelegate().Remove(OnMetalFXEnabledChanged);
		CVarMetalFXUpscalerMode.AsVariable()->OnChangedDelegate().Remove(OnMetalFXUpscalerModeChanged);
		CVarEnableMetalFXInEditor.AsVariable()->OnChangedDelegate().Remove(OnMetalFXEnableInEditorChanged);
	}

	// Stop new ViewExtension activation before destroying the module-owned Core.
	EligibleUpscalerType = EMetalFXUpscalerType::None;
	bRuntimeInitializationAttempted = false;
	bWasMetalFXEnabled = false;
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

	//이미 만들어져 있으면 만들어진걸 리턴. 잘못 만들어져 있으면 리셋
	if (MetalFXUpscaler)
	{
		const EMetalFXUpscalerType ExistingUpscalerType = MetalFXUpscaler->GetUpscalerType();
		const bool bExistingCoreMatches = ExistingUpscalerType == RequestedType && SelectedUpscalerType == RequestedType;
		if (bExistingCoreMatches)
		{
			return MetalFXUpscaler.Get();
		}

		UE_LOG(LogMetalFX, Warning, TEXT("Recreating mismatched MetalFX Core. ExistingType=%d, SelectedType=%d, RequestedType=%d."), static_cast<int32>(ExistingUpscalerType), static_cast<int32>(SelectedUpscalerType), static_cast<int32>(RequestedType));

		// Adapters and queued render work keep non-owning pointers to the Core.
		FlushRenderingCommands();
		MetalFXUpscaler.Reset();
		SelectedUpscalerType = EMetalFXUpscalerType::None;
	}
	// Create the concrete Core for the validated configuration.
	switch (RequestedType)
	{
	case EMetalFXUpscalerType::Temporal:
		{
			MetalFXUpscaler = MakeUnique<FMetalFXTemporalUpscalerCore>();
			break;
		}
	case EMetalFXUpscalerType::Spatial:
		{
			MetalFXUpscaler = MakeUnique<FMetalFXSpatialUpscalerCore>();
			break;
		}
	default:
		{
			checkNoEntry();
			return nullptr;
		}
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
	if (!GDynamicRHI)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX requires an Apple Metal RHI."));
		MetalSupport = EMetalSupportDevice::NotSupported;
		return;
	}
	
	// Apply the startup CVars before listening for runtime activation changes.
	if (const UMetalFXSettings* Settings = GetDefault<UMetalFXSettings>())
	{
		ApplyMetalFXSettingsToCVars(*Settings);
	}

	MetalSupport = (IsMetalPlatform(GMaxRHIShaderPlatform) ? EMetalSupportDevice::Supported : EMetalSupportDevice::NotSupported);

	if (MetalSupport != EMetalSupportDevice::Supported)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is unavailable because the active RHI is not Metal."));
	}
	
#if METALFX_PLUGIN_ENABLED
	constexpr const TCHAR* Help = TEXT("Initialize MetalFX for this session. Use 1 to initialize; omit the value to show its status. 0 is unsupported.");
	const FConsoleCommandWithArgsAndOutputDeviceDelegate InitializeHandler = FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FMetalFXModule::HandleMetalFXInitializeCommand);
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	InitializeConsoleCommands[0] = ConsoleManager.RegisterConsoleCommand(TEXT("r.MetalFX.Initialize"), Help, InitializeHandler);
	InitializeConsoleCommands[1] = ConsoleManager.RegisterConsoleCommand(TEXT("r.MetalFX.Init"), Help, InitializeHandler);

	if (MetalSupport == EMetalSupportDevice::Supported)
	{
		IConsoleVariable* Enabled = CVarEnableMetalFX.AsVariable();
		OnMetalFXEnabledChanged = Enabled->OnChangedDelegate().AddRaw(this, &FMetalFXModule::HandleMetalFXActivationSettingsChanged);
		OnMetalFXUpscalerModeChanged = CVarMetalFXUpscalerMode.AsVariable()->OnChangedDelegate().AddRaw(this, &FMetalFXModule::HandleMetalFXActivationSettingsChanged);
		OnMetalFXEnableInEditorChanged = CVarEnableMetalFXInEditor.AsVariable()->OnChangedDelegate().AddRaw(this, &FMetalFXModule::HandleMetalFXActivationSettingsChanged);
		HandleMetalFXActivationSettingsChanged(Enabled);
	}

	const bool bInitializeOnStartup = CVarMetalFXInitializeOnStartup.GetValueOnGameThread() ||
		FParse::Param(FCommandLine::Get(), TEXT("MetalFXInitializeOnStartup"));
	if (bInitializeOnStartup && !bRuntimeInitializationAttempted)
	{
		InitializeMetalFXRuntime();
	}
	if (!bRuntimeInitializationAttempted && MetalSupport == EMetalSupportDevice::Supported)
	{
		UE_LOG(LogMetalFX, Log, TEXT("MetalFX runtime initialization deferred until r.MetalFX.Init 1 or r.MetalFX.Enabled 1."));
	}
#endif
}

void FMetalFXModule::InitializeMetalFXRuntime()
{
#if METALFX_PLUGIN_ENABLED
	check(IsInGameThread());
	bRuntimeInitializationAttempted = true;
	if (MetalSupport != EMetalSupportDevice::Supported)
	{
		return;
	}

	SupportedUpscalerType = FMetalFXUpscalerCore::QuerySupportedUpscalerType();
	MetalFXSupport = FMetalFXUpscalerCore::QuerySupportReason(SupportedUpscalerType);
	if (MetalFXSupport != EMetalFXSupportReason::Supported)
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX activation is unavailable. SupportReason=%d."), static_cast<int32>(MetalFXSupport));
	}
	else if (!CreateMetalFXUpscaler(SupportedUpscalerType))
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX is supported, but its upscaler Core could not be created."));
	}

	// Keep the status display available after an enable attempt, including on unsupported devices.
	if (!MetalFXViewExtension.IsValid())
	{
		MetalFXViewExtension = FSceneViewExtensions::NewExtension<FMetalFXViewExtension>();
	}
#endif
}

void FMetalFXModule::HandleMetalFXInitializeCommand(const TArray<FString>& Args, FOutputDevice& Output)
{
	check(IsInGameThread());
	if (Args.Num() == 1 && Args[0] == TEXT("0"))
	{
		Output.Log(TEXT("MetalFX initialization cannot be undone during this session. Use r.MetalFX.Enabled 0 to disable upscaling."));
		return;
	}
	if (!Args.IsEmpty())
	{
		if (Args.Num() != 1 || Args[0] != TEXT("1"))
		{
			Output.Log(TEXT("Usage: r.MetalFX.Init [1]"));
			return;
		}
		if (!bRuntimeInitializationAttempted)
		{
			InitializeMetalFXRuntime();
		}
	}

	if (!bRuntimeInitializationAttempted)
	{
		Output.Log(TEXT("MetalFX Initialize = 0 (not attempted)."));
	}
	else if (IsMetalFXSupported() && MetalFXUpscaler && MetalFXUpscaler->IsInitialized())
	{
		Output.Log(TEXT("MetalFX Initialize = 1 (ready)."));
	}
	else
	{
		Output.Log(TEXT("MetalFX Initialize = 1 (initialization unavailable or failed; see LogMetalFX)."));
	}
}

void FMetalFXModule::HandleMetalFXActivationSettingsChanged(IConsoleVariable*)
{
#if METALFX_PLUGIN_ENABLED
	check(IsInGameThread());
	const bool bEnabled = CVarEnableMetalFX.GetValueOnGameThread();
	const bool bJustEnabled = bEnabled && !bWasMetalFXEnabled;
	bWasMetalFXEnabled = bEnabled;
	EligibleUpscalerType = EMetalFXUpscalerType::None;
	if (!bEnabled)
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX activation skipped because r.MetalFX.Enabled is false."));
		return;
	}

	if (bJustEnabled)
	{
		InitializeMetalFXRuntime();
	}

	const bool bAllowedInEditor = !GIsEditor || CVarEnableMetalFXInEditor.GetValueOnGameThread();
	if (!bAllowedInEditor || !IsMetalFXSupported())
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX activation skipped because MetalFX is unsupported for this RHI or editor context."));
		return;
	}

	if (SelectedUpscalerType != EMetalFXUpscalerType::Temporal && SelectedUpscalerType != EMetalFXUpscalerType::Spatial)
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX activation skipped because the selected upscaler mode is Off or invalid."));
		return;
	}

	const EMetalFXUpscalerType RequestedType = static_cast<EMetalFXUpscalerType>(CVarMetalFXUpscalerMode.GetValueOnGameThread());
	if (RequestedType != SelectedUpscalerType)
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX activation skipped because UpscalerMode (%d) does not match the initialized Core type (%d)."),
			static_cast<int32>(RequestedType), static_cast<int32>(SelectedUpscalerType));
		return;
	}

	if (!MetalFXUpscaler || !MetalFXUpscaler->IsInitialized() || MetalFXUpscaler->GetUpscalerType() != RequestedType)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX activation skipped because the initialized Core is not %s."),
			RequestedType == EMetalFXUpscalerType::Temporal ? TEXT("Temporal") : TEXT("Spatial"));
		return;
	}

	EligibleUpscalerType = RequestedType;
#endif
}
