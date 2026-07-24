#include "MetalFXViewExtension.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFX.h"
#include "MetalFXHelper.h"
#include "MetalFXSharpeningUpscaler.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscaler.h"
#include "MetalFXSpatialUpscaler.h"
#include "MetalFXSpatialUpscalerCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "RenderingThread.h"

#if !UE_BUILD_SHIPPING
static FString FormatMetalFXRect(const FIntRect& Rect)
{
	return FString::Printf(TEXT("Size=%dx%d"), Rect.Width(), Rect.Height());
}

static FIntPoint GetMetalFXExpectedInputSize(const FIntRect& OutputRect, float ScreenPercentage)
{
	const float ResolutionFraction = ScreenPercentage > 0.0f ? (ScreenPercentage / METALFX_FULL_SCREEN_PERCENTAGE) : 1.0f;
	return FIntPoint(
		FMath::RoundToInt(static_cast<float>(OutputRect.Width()) * ResolutionFraction),
		FMath::RoundToInt(static_cast<float>(OutputRect.Height()) * ResolutionFraction));
}

static FVector2D GetMetalFXActualScreenPercentage(const FIntRect& InputRect, const FIntRect& OutputRect)
{
	if (OutputRect.Width() <= 0 || OutputRect.Height() <= 0)
	{
		return FVector2D::ZeroVector;
	}

	return FVector2D(
		static_cast<double>(InputRect.Width()) / static_cast<double>(OutputRect.Width()) * 100.0,
		static_cast<double>(InputRect.Height()) / static_cast<double>(OutputRect.Height()) * 100.0);
}

static const TCHAR* GetMetalFXUpscalerTypeName(EMetalFXUpscalerType UpscalerType)
{
	switch (UpscalerType)
	{
	case EMetalFXUpscalerType::Spatial:
		return TEXT("Spatial");
	case EMetalFXUpscalerType::Temporal:
		return TEXT("Temporal");
	default:
		return TEXT("Off");
	}
}

static void AddMetalFXStatusDebugMessages(bool bCanActivate, bool bEnableInEditor, bool bIsActive, EMetalFXUpscalerType UpscalerType, const FMetalFXActiveDebugInfo* ActiveDebugInfo = nullptr)
{
	// Stable, explicit keys avoid accidental collisions from character sums.
	constexpr uint64 MetalFXMessageKeyBase = 0x4D465800ull; // "MFX\0"
	constexpr uint64 ActivationMessageKey = MetalFXMessageKeyBase + 1;
	constexpr uint64 InEditorMessageKey = MetalFXMessageKeyBase + 2;
	constexpr uint64 RuntimeMessageKey = MetalFXMessageKeyBase + 3;
	constexpr uint64 ActiveDetailsMessageKey = MetalFXMessageKeyBase + 4;
	constexpr float MessageDuration = 0.1f;

	// Availability means the current RHI and MetalFX device support checks passed.
	GEngine->AddOnScreenDebugMessage(ActivationMessageKey, MessageDuration, bCanActivate ? FColor::Emerald : FColor::Red, FString::Printf(TEXT("Apple MetalFX : %s Activate"), bCanActivate ? TEXT("Can") : TEXT("Can NOT")), true);

	// Editor activation has its own status line because PIE requires opt-in.
	if (GIsEditor)
	{
		GEngine->AddOnScreenDebugMessage(InEditorMessageKey, MessageDuration, bEnableInEditor ? FColor::Emerald : FColor::Yellow, FString::Printf(TEXT("Apple MetalFX Editor SupportState : %s"), bEnableInEditor ? TEXT("Enabled") : TEXT("Disabled")), true);
	}

	if (bCanActivate)
	{
		// Runtime state means MetalFX is actively selected for the current view family.
		GEngine->AddOnScreenDebugMessage(RuntimeMessageKey, MessageDuration, bIsActive ? FColor::Emerald : FColor::Yellow, FString::Printf(TEXT("Apple MetalFX Mode : %s"), bIsActive ? GetMetalFXUpscalerTypeName(UpscalerType) : TEXT("Off")), true);

		if (ActiveDebugInfo && ActiveDebugInfo->Resolution.bIsValid)
		{
			const FMetalFXResolutionDebugInfo& Resolution = ActiveDebugInfo->Resolution;
			const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(Resolution.QualityMode);
			FString Details;

			if (bIsActive && ActiveDebugInfo->bIsValid)
			{
				const float RequestedPrimaryScreenPercentage = Resolution.PrimaryResolutionFraction * METALFX_FULL_SCREEN_PERCENTAGE;
				const FIntPoint ExpectedInputSize = GetMetalFXExpectedInputSize(ActiveDebugInfo->OutputRect, RequestedPrimaryScreenPercentage);
				const FVector2D ActualScreenPercentage = GetMetalFXActualScreenPercentage(ActiveDebugInfo->InputRect, ActiveDebugInfo->OutputRect);
				Details = FString::Printf(TEXT("Apple MetalFX Active Rects : Input[%s] Output[%s] ExpectedInput[Size=%dx%d]\nMetalFX Scale : Mode=%s Requested=%.2f%% Actual=%.2f%%/%.2f%%"), *FormatMetalFXRect(ActiveDebugInfo->InputRect), *FormatMetalFXRect(ActiveDebugInfo->OutputRect), ExpectedInputSize.X, ExpectedInputSize.Y, Quality.Name, RequestedPrimaryScreenPercentage, ActualScreenPercentage.X, ActualScreenPercentage.Y);
			}
			else
			{
				Details = FString::Printf(TEXT("Apple MetalFX Config : Mode=%s (Deactivated)"), Quality.Name);
			}

			Details += FString::Printf(TEXT("\nEngine Resolution : DynamicRes=%s Base=%.2f%% Output=%.2f%%\n%s Resolution : Primary=%.2f%% FinalInput=%.2f%% AutoScaling=%s"), Resolution.bDynamicResolutionActive ? TEXT("On") : TEXT("Off"), Resolution.EngineBaseResolutionFraction * METALFX_FULL_SCREEN_PERCENTAGE, Resolution.OutputResolutionFraction * METALFX_FULL_SCREEN_PERCENTAGE, bIsActive ? TEXT("Applied") : TEXT("Configured"), Resolution.PrimaryResolutionFraction * METALFX_FULL_SCREEN_PERCENTAGE, Resolution.FinalInputResolutionFraction * METALFX_FULL_SCREEN_PERCENTAGE, Resolution.bAutoScalingFromEngine ? TEXT("On") : TEXT("Off"));

			GEngine->AddOnScreenDebugMessage(ActiveDetailsMessageKey, MessageDuration, bIsActive ? FColor::Emerald : FColor::Yellow, Details, true);
		}
	}
}
#endif

static void UpdateMetalFXDebugStatus(const FSceneViewFamily& ViewFamily, FMetalFXModule& MetalFXModule, bool bMetalFXSupported, EMetalFXUpscalerType UpscalerType, bool bIsActive)
{
	#if !UE_BUILD_SHIPPING
	if (!GEngine || !CVarMetalFXDebugDisplay.GetValueOnGameThread())
	{
		return;
	}

	FMetalFXActiveDebugInfo ActiveDebugInfo;
#if METALFX_PLUGIN_ENABLED
	if (FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler())
	{
		ActiveDebugInfo = Upscaler->GetActiveDebugInfo();
	}
#endif

	if (!bIsActive || !ActiveDebugInfo.Resolution.bIsValid)
	{
		// An inactive scaler has no current rect. Use a fresh read-only preview
		// instead of retaining rect or resolution data from an older frame.
		ActiveDebugInfo.Resolution = GetConfiguredMetalFXResolutionDebugInfo(ViewFamily, static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread()));
	}

	AddMetalFXStatusDebugMessages(bMetalFXSupported, CVarEnableMetalFXInEditor.GetValueOnGameThread(), bIsActive, UpscalerType, &ActiveDebugInfo);
#endif
}

static void QueueMetalFXResolutionDebugInfo(FMetalFXModule& MetalFXModule, bool bIsActive, const FMetalFXResolutionDebugInfo& ResolutionDebugInfo)
{
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
	if (!bIsActive || !ResolutionDebugInfo.bIsValid)
	{
		return;
	}

	if (FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler())
	{
		// Keep the resolution plan in the same render command stream as the
		// actual rect update so the debug display never pairs different frames.
		ENQUEUE_RENDER_COMMAND(FMetalFXCommitResolutionDebugInfo)([Upscaler, ResolutionDebugInfo](FRHICommandListImmediate&)
			{
				Upscaler->UpdateResolutionDebugInfo(ResolutionDebugInfo);
			});
	}
#endif
}

static bool LogMetalFXActivationFailure(const TCHAR* Reason, ELogVerbosity::Type Verbosity = ELogVerbosity::Verbose)
{
	const bool bCompiledIn = (Verbosity & ELogVerbosity::VerbosityMask) <= LogMetalFX.GetCompileTimeVerbosity();
	if (bCompiledIn && !LogMetalFX.IsSuppressed(Verbosity))
	{
		FMsg::Logf(__FILE__, __LINE__, LogMetalFX.GetCategoryName(), Verbosity, TEXT("MetalFX skipped view family activation: %s"), Reason);
		LogMetalFX.PostTrigger(Verbosity);
	}
	return false;
}

static bool IsMetalFXSupportedForCurrentContext(const FMetalFXModule& MetalFXModule)
{
	if (!MetalFXModule.IsMetalFXSupported())
	{
		return false;
	}

	// Editor activation is an explicit opt-in. IsActiveThisFrame_Internal also restricts the extension to PIE,
	// while this check keeps status and activation decisions based on the same setting.
	return ((!GIsEditor) || (CVarEnableMetalFXInEditor.GetValueOnGameThread()));
}

FMetalFXViewExtension::FMetalFXViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
	// The extension can exist on Metal RHI even when MetalFX itself is not supported.
}

void FMetalFXViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
#if METALFX_PLUGIN_ENABLED
	FMetalFXModule& MetalFXModule = FMetalFXModule::Get();
	const EMetalFXUpscalerType RequestedUpscalerType = static_cast<EMetalFXUpscalerType>(CVarMetalFXUpscalerMode.GetValueOnGameThread());
	if (!CanActivateForViewFamily(InViewFamily, MetalFXModule, false))
	{
		return;
	}

	FMetalFXUpscalerCore* Core = MetalFXModule.GetMetalFXUpscaler();
	const bool bCoreExists = Core != nullptr;
	const bool bCoreInitialized = bCoreExists && Core->IsInitialized();
	const bool bRequestedCoreSelected = bCoreInitialized && Core->GetUpscalerType() == RequestedUpscalerType;
	if (!bRequestedCoreSelected)
	{
		return;
	}

	if (RequestedUpscalerType == EMetalFXUpscalerType::Temporal)
	{
		if (InViewFamily.GetTemporalUpscalerInterface())
		{
			return;
		}

		for (const FSceneView* View : InViewFamily.Views)
		{
			const bool bHasViewState = View->State != nullptr;
			const bool bUsesTemporalUpscale = View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale;
			if (!bHasViewState || !bUsesTemporalUpscale)
			{
				return;
			}
		}
	}
	else if (RequestedUpscalerType == EMetalFXUpscalerType::Spatial)
	{
		if (InViewFamily.GetPrimarySpatialUpscalerInterface())
		{
			return;
		}

		for (const FSceneView* View : InViewFamily.Views)
		{
			const EPrimaryScreenPercentageMethod Method = View->PrimaryScreenPercentageMethod;
			const bool bUsesSpatialUpscale = Method == EPrimaryScreenPercentageMethod::SpatialUpscale;
			const bool bUsesTemporalUpscale = Method == EPrimaryScreenPercentageMethod::TemporalUpscale;
			if (!bUsesSpatialUpscale && !bUsesTemporalUpscale)
			{
				return;
			}
		}

		if (InView.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale)
		{
			// This is an intentional fallback: if MetalFX Temporal is unavailable but
			// the startup-selected Core is Spatial, route a Temporal primary request
			// through the engine's Spatial primary-upscale stage.
			InView.PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::SpatialUpscale;
		}

		if (InView.PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::SpatialUpscale)
		{
			return;
		}
	}
	else
	{
		return;
	}

	const EMetalFXQualityMode QualityMode = static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread());
	// SetupView provides the mutable FSceneView intended for per-view overrides.
	// Use the same composed resolution plan that BeginRenderViewFamily applies
	// to the family-wide Secondary output fraction.
	const FMetalFXResolutionDebugInfo ResolutionInfo = GetConfiguredMetalFXResolutionDebugInfo(InViewFamily, QualityMode);
	InView.SceneViewInitOptions.OverridePrimaryResolutionFraction = ResolutionInfo.PrimaryResolutionFraction;
#endif
}

bool FMetalFXViewExtension::CanActivateForViewFamily(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule, bool bLogFailure) const
{
	const auto Reject = [bLogFailure](const TCHAR* Reason)
	{
		return bLogFailure ? LogMetalFXActivationFailure(Reason) : false;
	};

	if (!CVarEnableMetalFX.GetValueOnGameThread())
	{
		return Reject(TEXT("r.MetalFX.Enabled is false"));
	}
	if (!IsMetalFXSupportedForCurrentContext(MetalFXModule))
	{
		return Reject(TEXT("MetalFX is unsupported for this RHI or editor context"));
	}
	if (!ViewFamily.Scene)
	{
		return Reject(TEXT("the view family has no scene"));
	}
	if (ViewFamily.ViewMode != EViewModeIndex::VMI_Lit)
	{
		return Reject(TEXT("the view mode is not Lit"));
	}
	const EShadingPath ShadingPath = ViewFamily.Scene->GetShadingPath();
	const bool bUsesDeferredShading = ShadingPath == EShadingPath::Deferred;
	const bool bUsesMobileShading = ShadingPath == EShadingPath::Mobile;
	if (!bUsesDeferredShading && !bUsesMobileShading)
	{
		return Reject(TEXT("the shading path is neither Deferred nor Mobile"));
	}
	if (!ViewFamily.bRealtimeUpdate)
	{
		return Reject(TEXT("the view family is not realtime"));
	}
	if (ViewFamily.Views.Num() == 0)
	{
		return Reject(TEXT("the view family contains no views"));
	}
	const EMetalFXUpscalerType SelectedUpscalerType = MetalFXModule.GetSelectedUpscalerType();
	const bool bSpatialSelected = SelectedUpscalerType == EMetalFXUpscalerType::Spatial;
	const bool bTemporalSelected = SelectedUpscalerType == EMetalFXUpscalerType::Temporal;
	if (!bSpatialSelected && !bTemporalSelected)
	{
		return Reject(TEXT("the selected upscaler mode is Off or invalid"));
	}
	const EMetalFXUpscalerType RequestedUpscalerType = static_cast<EMetalFXUpscalerType>(CVarMetalFXUpscalerMode.GetValueOnGameThread());
	if (RequestedUpscalerType != SelectedUpscalerType)
	{
		return Reject(TEXT("the current UpscalerMode does not match the startup-created Core type"));
	}
	for (const FSceneView* View : ViewFamily.Views)
	{
		if (!View)
		{
			return Reject(TEXT("the view family contains a null view"));
		}
		if (View->bIsSceneCapture)
		{
			return Reject(TEXT("scene captures are unsupported"));
		}
	}
	return true;
}

bool FMetalFXViewExtension::CanActivateTemporal(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule) const
{
#if METALFX_PLUGIN_ENABLED
	const FMetalFXUpscalerCore* Core = MetalFXModule.GetMetalFXUpscaler();
	const bool bCoreExists = Core != nullptr;
	const bool bCoreInitialized = bCoreExists && Core->IsInitialized();
	const bool bTemporalCoreSelected = bCoreInitialized && Core->GetUpscalerType() == EMetalFXUpscalerType::Temporal;
	if (!bTemporalCoreSelected)
	{
		return LogMetalFXActivationFailure(TEXT("the initialized Core is not Temporal"), ELogVerbosity::Warning);
	}

	if (ViewFamily.GetTemporalUpscalerInterface())
	{
		return LogMetalFXActivationFailure(TEXT("another temporal upscaler is already installed"), ELogVerbosity::Warning);
	}

	for (const FSceneView* View : ViewFamily.Views)
	{
		if (!View->State)
		{
			return LogMetalFXActivationFailure(TEXT("Temporal mode requires a persistent ViewState"));
		}
		if (View->PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::TemporalUpscale)
		{
			return LogMetalFXActivationFailure(TEXT("Temporal mode requires TemporalUpscale as the primary screen-percentage method"));
		}
	}
	return true;
#else
	return LogMetalFXActivationFailure(TEXT("MetalFX was not compiled for this platform"));
#endif
}

bool FMetalFXViewExtension::CanActivateSpatial(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule) const
{
#if METALFX_PLUGIN_ENABLED
	const FMetalFXUpscalerCore* Core = MetalFXModule.GetMetalFXUpscaler();
	const bool bCoreExists = Core != nullptr;
	const bool bCoreInitialized = bCoreExists && Core->IsInitialized();
	const bool bSpatialCoreSelected = bCoreInitialized && Core->GetUpscalerType() == EMetalFXUpscalerType::Spatial;
	if (!bSpatialCoreSelected)
	{
		return LogMetalFXActivationFailure(TEXT("the initialized Core is not Spatial"), ELogVerbosity::Warning);
	}
	if (ViewFamily.GetPrimarySpatialUpscalerInterface())
	{
		return LogMetalFXActivationFailure(TEXT("another primary spatial upscaler is already installed"), ELogVerbosity::Warning);
	}
	for (const FSceneView* View : ViewFamily.Views)
	{
		if (View->PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::SpatialUpscale)
		{
			return LogMetalFXActivationFailure(TEXT("Spatial mode requires SpatialUpscale as the primary screen-percentage method"));
		}
	}
	return true;
#else
	return LogMetalFXActivationFailure(TEXT("MetalFX was not compiled for this platform"));
#endif
}

void FMetalFXViewExtension::InstallTemporalUpscaler(FSceneViewFamily& ViewFamily, FMetalFXModule& MetalFXModule) const
{
#if METALFX_PLUGIN_ENABLED
	if (FMetalFXTemporalUpscalerCore* TemporalCore = MetalFXModule.GetMetalFXTemporalUpscaler())
	{
		ViewFamily.SetTemporalUpscalerInterface(new FMetalFXTemporalUpscaler(TemporalCore));
	}
#endif
}

void FMetalFXViewExtension::InstallSpatialUpscaler(FSceneViewFamily& ViewFamily, FMetalFXModule& MetalFXModule) const
{
#if METALFX_PLUGIN_ENABLED
	if (FMetalFXSpatialUpscalerCore* SpatialCore = MetalFXModule.GetMetalFXSpatialUpscaler())
	{
		ViewFamily.SetPrimarySpatialUpscalerInterface(new FMetalFXSpatialUpscaler(SpatialCore));
	}
#endif
}

void FMetalFXViewExtension::InstallSharpeningUpscaler(FSceneViewFamily& ViewFamily) const
{
#if METALFX_PLUGIN_ENABLED
	const float Sharpness = CVarMetalFXSharpness.GetValueOnGameThread();
	if (Sharpness <= 0.0f)
	{
		return;
	}

	if (ViewFamily.GetSecondarySpatialUpscalerInterface())
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX RCAS skipped because another secondary spatial upscaler is already installed."));
		return;
	}

	ViewFamily.SetSecondarySpatialUpscalerInterface(new FMetalFXSharpeningUpscaler());
#endif
}

void FMetalFXViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	FMetalFXModule& MetalFXModule = FMetalFXModule::Get();
	const EMetalFXUpscalerType UpscalerType = MetalFXModule.GetSelectedUpscalerType();
	const bool bMetalFXSupported = IsMetalFXSupportedForCurrentContext(MetalFXModule);

	if (!CanActivateForViewFamily(InViewFamily, MetalFXModule))
	{
		UpdateMetalFXDebugStatus(InViewFamily, MetalFXModule, bMetalFXSupported, UpscalerType, false);
		return;
	}

	const bool bTemporalModeSelected = UpscalerType == EMetalFXUpscalerType::Temporal;
	bool bCanActivateSelectedMode = false;
	if (bTemporalModeSelected)
	{
		bCanActivateSelectedMode = CanActivateTemporal(InViewFamily, MetalFXModule);
	}
	else
	{
		bCanActivateSelectedMode = CanActivateSpatial(InViewFamily, MetalFXModule);
	}
	if (!bCanActivateSelectedMode)
	{
		UpdateMetalFXDebugStatus(InViewFamily, MetalFXModule, bMetalFXSupported, UpscalerType, false);
		return;
	}

	FMetalFXResolutionDebugInfo ResolutionDebugInfo;
	if (!ApplyMetalFXScreenPercentageToViewFamily(InViewFamily, static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread()), &ResolutionDebugInfo))
	{
		UpdateMetalFXDebugStatus(InViewFamily, MetalFXModule, bMetalFXSupported, UpscalerType, false);
		return;
	}

	if (UpscalerType == EMetalFXUpscalerType::Temporal)
	{
		InstallTemporalUpscaler(InViewFamily, MetalFXModule);
	}
	else
	{
		InstallSpatialUpscaler(InViewFamily, MetalFXModule);
	}

	const bool bTemporalUpscalerInstalled = InViewFamily.GetTemporalUpscalerInterface() != nullptr;
	const bool bSpatialUpscalerInstalled = InViewFamily.GetPrimarySpatialUpscalerInterface() != nullptr;
	const bool bTemporalUpscalerActive = bTemporalModeSelected && bTemporalUpscalerInstalled;
	const bool bSpatialUpscalerActive = !bTemporalModeSelected && bSpatialUpscalerInstalled;
	const bool bIsActive = bTemporalUpscalerActive || bSpatialUpscalerActive;
	if (bIsActive)
	{
		InstallSharpeningUpscaler(InViewFamily);
	}

	UpdateMetalFXDebugStatus(InViewFamily, MetalFXModule, bMetalFXSupported, UpscalerType, bIsActive);
	QueueMetalFXResolutionDebugInfo(MetalFXModule, bIsActive, ResolutionDebugInfo);
}

bool FMetalFXViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!FSceneViewExtensionBase::IsActiveThisFrame_Internal(Context))
	{
		return false;
	}

	//에디터인 경우 추가 체크
	// Editor builds only allow MetalFX during PIE.
	if (GIsEditor)
	{
		const bool bEnableInEditor = CVarEnableMetalFXInEditor.GetValueOnGameThread();
		const UWorld* World = Context.GetWorld();

		if (!IsValid(World))
		{
			return false;
		}

		return bEnableInEditor && World->IsPlayInEditor();
	}

	return true;
}
