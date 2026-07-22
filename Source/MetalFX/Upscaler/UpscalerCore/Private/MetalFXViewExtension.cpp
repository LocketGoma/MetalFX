#include "MetalFXViewExtension.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFX.h"
#include "MetalFXHelper.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscaler.h"
#include "MetalFXSpatialUpscaler.h"
#include "MetalFXSpatialUpscalerCore.h"
#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "RenderingThread.h"

#if !UE_BUILD_SHIPPING
static FString FormatMetalFXRect(const FIntRect& Rect)
{
	return FString::Printf(
		TEXT("Size=%dx%d"),
		Rect.Width(),
		Rect.Height());
}

static FIntPoint GetMetalFXExpectedInputSize(const FIntRect& OutputRect, float ScreenPercentage)
{
	const float ResolutionFraction = ScreenPercentage > 0.0f ? (ScreenPercentage / 100.0f) : 1.0f;
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

static FMetalFXResolutionDebugInfo GetConfiguredMetalFXResolutionDebugInfo(const FSceneViewFamily& ViewFamily)
{
	FMetalFXResolutionDebugInfo DebugInfo;
	DebugInfo.QualityMode = static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread());
	DebugInfo.bAutoScalingFromEngine = CVarMetalFXAutoScalingFromEngine.GetValueOnGameThread();

	float EngineBaseResolutionFraction = 1.0f;
	if (GEngine)
	{
		FDynamicResolutionStateInfos DynamicResolutionInfos;
		GEngine->GetDynamicResolutionCurrentStateInfos(DynamicResolutionInfos);
		DebugInfo.bDynamicResolutionActive = DynamicResolutionInfos.Status == EDynamicResolutionStatus::Enabled
			|| DynamicResolutionInfos.Status == EDynamicResolutionStatus::DebugForceEnabled;
		if (DebugInfo.bDynamicResolutionActive)
		{
			const float DynamicResolutionFraction = DynamicResolutionInfos.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction];
			if (FMath::IsFinite(DynamicResolutionFraction) && DynamicResolutionFraction > 0.0f)
			{
				EngineBaseResolutionFraction = DynamicResolutionFraction;
			}
		}
	}

	if (!DebugInfo.bDynamicResolutionActive)
	{
		if (const ISceneViewFamilyScreenPercentage* ScreenPercentageInterface = ViewFamily.GetScreenPercentageInterface())
		{
			const DynamicRenderScaling::TMap<float> UpperBounds = ScreenPercentageInterface->GetResolutionFractionsUpperBound();
			const float InterfaceResolutionFraction = UpperBounds[GDynamicPrimaryResolutionFraction];
			if (FMath::IsFinite(InterfaceResolutionFraction) && InterfaceResolutionFraction > 0.0f)
			{
				EngineBaseResolutionFraction = InterfaceResolutionFraction;
			}
		}
	}

	const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(DebugInfo.QualityMode);
	DebugInfo.EngineBaseResolutionFraction = FMath::Clamp(
		EngineBaseResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMinResolutionFraction,
		GetMetalFXMaxUpscaleResolutionFraction());
	DebugInfo.PrimaryResolutionFraction = Quality.GetPrimaryResolutionFraction(DebugInfo.bAutoScalingFromEngine);
	DebugInfo.OutputResolutionFraction = Quality.bForceNativeResolution
		? 1.0f
		: DebugInfo.bAutoScalingFromEngine
			? ViewFamily.SecondaryViewFraction * DebugInfo.EngineBaseResolutionFraction
			: ViewFamily.SecondaryViewFraction;
	DebugInfo.OutputResolutionFraction = FMath::Clamp(
		DebugInfo.OutputResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMinResolutionFraction,
		GetMetalFXMaxUpscaleResolutionFraction());
	DebugInfo.FinalInputResolutionFraction = DebugInfo.PrimaryResolutionFraction * DebugInfo.OutputResolutionFraction;
	DebugInfo.bIsValid = true;
	return DebugInfo;
}

static void AddMetalFXStatusDebugMessages(bool bCanActivate, bool bEnableInEditor, bool bIsActive, EMetalFXUpscalerType UpscalerType, const FMetalFXActiveDebugInfo* ActiveDebugInfo = nullptr)
{
	if (!GEngine)
	{
		return;
	}

	if (!CVarMetalFXDebugDisplay.GetValueOnGameThread())
	{
		return;
	}

	constexpr int32 ChannelCode = 'M'+'E'+'T'+'A'+'L'+'F'+'X';
	constexpr int32 ActivationMessageKey = ChannelCode + 'A';			//Can Activate
	constexpr int32 InEditorMessageKey = ChannelCode + 'E';				//Can Activate in Editor
	constexpr int32 RuntimeMessageKey = ChannelCode + 'R';				//Enabled (Running)
	constexpr int32 ActiveDetailsMessageKey = ChannelCode + 'D';		//Active Details
	constexpr float MessageDuration = 0.1f;

	// Availability means the current RHI and MetalFX device support checks passed.
	GEngine->AddOnScreenDebugMessage(
		ActivationMessageKey,
		MessageDuration,
		bCanActivate ? FColor::Emerald : FColor::Red,
		FString::Printf(TEXT("Apple MetalFX : %s Activate"), bCanActivate ? TEXT("Can") : TEXT("Can NOT")),
		true);

	//에디터인 경우 추가 체크
	if (GIsEditor)
	{
		GEngine->AddOnScreenDebugMessage(
			InEditorMessageKey,
			MessageDuration,
			bEnableInEditor ? FColor::Emerald : FColor::Yellow,
			FString::Printf(TEXT("Apple MetalFX Editor SupportState : %s"), bEnableInEditor ? TEXT("Enabled") : TEXT("Disabled")),
			true);
	}
	
	if (bCanActivate)
	{
		// Runtime state means MetalFX is actively selected for the current view family.
		GEngine->AddOnScreenDebugMessage(
			RuntimeMessageKey,
			MessageDuration,
			bIsActive ? FColor::Emerald : FColor::Yellow,
			FString::Printf(TEXT("Apple MetalFX Mode : %s"), bIsActive ? GetMetalFXUpscalerTypeName(UpscalerType) : TEXT("Off")),
			true);

		if (ActiveDebugInfo && ActiveDebugInfo->Resolution.bIsValid)
		{
			const FMetalFXResolutionDebugInfo& Resolution = ActiveDebugInfo->Resolution;
			const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(Resolution.QualityMode);
			FString Details;

			if (bIsActive && ActiveDebugInfo->bIsValid)
			{
				const float RequestedPrimaryScreenPercentage = Resolution.PrimaryResolutionFraction * 100.0f;
				const FIntPoint ExpectedInputSize = GetMetalFXExpectedInputSize(ActiveDebugInfo->OutputRect, RequestedPrimaryScreenPercentage);
				const FVector2D ActualScreenPercentage = GetMetalFXActualScreenPercentage(ActiveDebugInfo->InputRect, ActiveDebugInfo->OutputRect);
				Details = FString::Printf(
					TEXT("Apple MetalFX Active Rects : Input[%s] Output[%s] ExpectedInput[Size=%dx%d]\n")
					TEXT("MetalFX Scale : Mode=%s Requested=%.2f%% Actual=%.2f%%/%.2f%%"),
					*FormatMetalFXRect(ActiveDebugInfo->InputRect),
					*FormatMetalFXRect(ActiveDebugInfo->OutputRect),
					ExpectedInputSize.X,
					ExpectedInputSize.Y,
					Quality.Name,
					RequestedPrimaryScreenPercentage,
					ActualScreenPercentage.X,
					ActualScreenPercentage.Y);
			}
			else
			{
				Details = FString::Printf(
					TEXT("Apple MetalFX Config : Mode=%s (Deactivated)"),
					Quality.Name);
			}

			Details += FString::Printf(
				TEXT("\nEngine Resolution : DynamicRes=%s Base=%.2f%% Output=%.2f%%")
				TEXT("\n%s Resolution : Primary=%.2f%% FinalInput=%.2f%% AutoScaling=%s"),
				Resolution.bDynamicResolutionActive ? TEXT("On") : TEXT("Off"),
				Resolution.EngineBaseResolutionFraction * 100.0f,
				Resolution.OutputResolutionFraction * 100.0f,
				bIsActive ? TEXT("Applied") : TEXT("Configured"),
				Resolution.PrimaryResolutionFraction * 100.0f,
				Resolution.FinalInputResolutionFraction * 100.0f,
				Resolution.bAutoScalingFromEngine ? TEXT("On") : TEXT("Off"));

			GEngine->AddOnScreenDebugMessage(
				ActiveDetailsMessageKey,
				MessageDuration,
				bIsActive ? FColor::Emerald : FColor::Yellow,
				Details,
				true);
		}
	}
}
#endif

static bool LogMetalFXActivationFailure(const TCHAR* Reason)
{
	UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX skipped view family activation: %s"), Reason);
	return false;
}

FMetalFXViewExtension::FMetalFXViewExtension(const FAutoRegister& AutoRegister)
: FSceneViewExtensionBase(AutoRegister)
, bMetalFXEnabled(false)
, bMetalFXSupported(true)
{
	// The extension can exist on Metal RHI even when MetalFX itself is not supported.
}

void FMetalFXViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	static TConsoleVariableData<bool>* CvarMetalFXEnable = IConsoleManager::Get().FindTConsoleVariableDataBool(TEXT("r.MetalFX.Enabled"));

	FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
	bMetalFXSupported = MetalFXModule.GetIsSupportedByRHI();
	
	// In editor builds, only PIE can opt into MetalFX through EnableInEditor.
	if (GIsEditor)
	{
		static TConsoleVariableData<bool>* CvarMetalFXEditorSupported = IConsoleManager::Get().FindTConsoleVariableDataBool(TEXT("r.MetalFX.EnableInEditor"));
		bMetalFXSupported = bMetalFXSupported && (CvarMetalFXEditorSupported ? CvarMetalFXEditorSupported->GetValueOnGameThread() : false);
	}
	
	//If the CVar is not registered, MetalFX must stay disabled.
	bMetalFXEnabled = CvarMetalFXEnable ? CvarMetalFXEnable->GetValueOnGameThread() : false;

}

void FMetalFXViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
#if METALFX_PLUGIN_ENABLED
	if (!bMetalFXEnabled || !bMetalFXSupported || InView.bIsSceneCapture || !InViewFamily.Scene || InViewFamily.ViewMode != EViewModeIndex::VMI_Lit || !InViewFamily.bRealtimeUpdate)
	{
		return;
	}

	const EShadingPath ShadingPath = InViewFamily.Scene->GetShadingPath();
	if (ShadingPath != EShadingPath::Deferred && ShadingPath != EShadingPath::Mobile)
	{
		return;
	}

	FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
	const EMetalFXUpscalerType RequestedUpscalerType = static_cast<EMetalFXUpscalerType>(CVarMetalFXUpscalerMode.GetValueOnGameThread());
	if (RequestedUpscalerType != MetalFXModule.GetSelectedUpscalerType())
	{
		return;
	}

	FMetalFXUpscalerCore* Core = MetalFXModule.GetMetalFXUpscaler();
	if (!Core || !Core->IsInitialized() || Core->GetUpscalerType() != RequestedUpscalerType)
	{
		return;
	}

	if (RequestedUpscalerType == EMetalFXUpscalerType::Temporal)
	{
		if (!InView.State || InView.PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::TemporalUpscale)
		{
			return;
		}
	}
	else if (RequestedUpscalerType == EMetalFXUpscalerType::Spatial)
	{
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
	const bool bAutoScalingFromEngine = CVarMetalFXAutoScalingFromEngine.GetValueOnGameThread();
	// SetupView provides the mutable FSceneView intended for per-view overrides.
	// BeginRenderViewFamily exposes const view pointers and only adjusts the
	// family-wide Secondary output fraction.
	InView.SceneViewInitOptions.OverridePrimaryResolutionFraction = GetMetalFXQualitySettings(QualityMode)
		.GetPrimaryResolutionFraction(bAutoScalingFromEngine);
#endif
}

bool FMetalFXViewExtension::CanActivateForViewFamily(const FSceneViewFamily& ViewFamily, EMetalFXUpscalerType UpscalerType) const
{
	if (!bMetalFXEnabled)
	{
		return LogMetalFXActivationFailure(TEXT("r.MetalFX.Enabled is false"));
	}
	if (!bMetalFXSupported)
	{
		return LogMetalFXActivationFailure(TEXT("MetalFX is unsupported for this RHI or editor context"));
	}
	if (!ViewFamily.Scene)
	{
		return LogMetalFXActivationFailure(TEXT("the view family has no scene"));
	}
	if (ViewFamily.ViewMode != EViewModeIndex::VMI_Lit)
	{
		return LogMetalFXActivationFailure(TEXT("the view mode is not Lit"));
	}
	const EShadingPath ShadingPath = ViewFamily.Scene->GetShadingPath();
	if (ShadingPath != EShadingPath::Deferred && ShadingPath != EShadingPath::Mobile)
	{
		return LogMetalFXActivationFailure(TEXT("the shading path is neither Deferred nor Mobile"));
	}
	if (!ViewFamily.bRealtimeUpdate)
	{
		return LogMetalFXActivationFailure(TEXT("the view family is not realtime"));
	}
	if (ViewFamily.Views.Num() == 0)
	{
		return LogMetalFXActivationFailure(TEXT("the view family contains no views"));
	}
	if (UpscalerType != EMetalFXUpscalerType::Spatial && UpscalerType != EMetalFXUpscalerType::Temporal)
	{
		return LogMetalFXActivationFailure(TEXT("the selected upscaler mode is Off or invalid"));
	}
	const EMetalFXUpscalerType RequestedUpscalerType = static_cast<EMetalFXUpscalerType>(CVarMetalFXUpscalerMode.GetValueOnGameThread());
	if (RequestedUpscalerType != UpscalerType)
	{
		return LogMetalFXActivationFailure(TEXT("the current UpscalerMode does not match the startup-created Core type"));
	}
	for (const FSceneView* View : ViewFamily.Views)
	{
		if (!View)
		{
			return LogMetalFXActivationFailure(TEXT("the view family contains a null view"));
		}
		if (View->bIsSceneCapture)
		{
			return LogMetalFXActivationFailure(TEXT("scene captures are unsupported"));
		}
	}
	return true;
}

bool FMetalFXViewExtension::CanActivateTemporal(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule) const
{
#if METALFX_PLUGIN_ENABLED
	const FMetalFXUpscalerCore* Core = MetalFXModule.GetMetalFXUpscaler();
	if (!Core || !Core->IsInitialized() || Core->GetUpscalerType() != EMetalFXUpscalerType::Temporal)
	{
		return LogMetalFXActivationFailure(TEXT("the initialized Core is not Temporal"));
	}
	if (ViewFamily.GetTemporalUpscalerInterface())
	{
		return LogMetalFXActivationFailure(TEXT("another temporal upscaler is already installed"));
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
	if (!Core || !Core->IsInitialized() || Core->GetUpscalerType() != EMetalFXUpscalerType::Spatial)
	{
		return LogMetalFXActivationFailure(TEXT("the initialized Core is not Spatial"));
	}
	if (ViewFamily.GetPrimarySpatialUpscalerInterface())
	{
		return LogMetalFXActivationFailure(TEXT("another primary spatial upscaler is already installed"));
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

void FMetalFXViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
	const EMetalFXUpscalerType UpscalerType = MetalFXModule.GetSelectedUpscalerType();
#if !UE_BUILD_SHIPPING
	const FMetalFXResolutionDebugInfo ConfiguredResolutionDebugInfo = GetConfiguredMetalFXResolutionDebugInfo(InViewFamily);
#endif

	auto UpdateDebugStatus = [&](bool bIsActive)
	{
#if !UE_BUILD_SHIPPING
		FMetalFXActiveDebugInfo ActiveDebugInfo;
#if METALFX_PLUGIN_ENABLED
		if (FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler())
		{
			ActiveDebugInfo = Upscaler->GetActiveDebugInfo();
		}
#endif
		if (!bIsActive || !ActiveDebugInfo.Resolution.bIsValid)
		{
			// When MetalFX is off there is no current scaler rect. Show a fresh,
			// read-only preview of the selected quality and engine resolution instead
			// of reusing stale rects from the last active frame.
			ActiveDebugInfo.Resolution = ConfiguredResolutionDebugInfo;
		}
		AddMetalFXStatusDebugMessages(
			bMetalFXSupported,
			CvarEnableMetalFXInEditor.GetValueOnGameThread(),
			bIsActive,
			UpscalerType,
			&ActiveDebugInfo);
#endif
	};

	auto CommitResolutionDebugInfo = [&](bool bIsActive, const FMetalFXResolutionDebugInfo& ResolutionDebugInfo)
	{
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
		if (bIsActive && ResolutionDebugInfo.bIsValid)
		{
			if (FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler())
			{
				// Queue the plan on the render thread so it is committed in the same
				// command stream as this frame's actual rect update. Writing it directly
				// on the game thread could pair a new plan with an older render-thread rect.
				ENQUEUE_RENDER_COMMAND(FMetalFXCommitResolutionDebugInfo)(
					[Upscaler, ResolutionDebugInfo](FRHICommandListImmediate&)
					{
						Upscaler->UpdateResolutionDebugInfo(ResolutionDebugInfo);
					});
			}
		}
#endif
	};

	if (!CanActivateForViewFamily(InViewFamily, UpscalerType))
	{
		UpdateDebugStatus(false);
		return;
	}

	if (UpscalerType == EMetalFXUpscalerType::Temporal)
	{
		if (!CanActivateTemporal(InViewFamily, MetalFXModule))
		{
			UpdateDebugStatus(false);
			return;
		}

		FMetalFXResolutionDebugInfo ResolutionDebugInfo;
		ApplyMetalFXScreenPercentageToViewFamily(
			InViewFamily,
			static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread()),
			&ResolutionDebugInfo);
		InstallTemporalUpscaler(InViewFamily, MetalFXModule);
		const bool bIsActive = InViewFamily.GetTemporalUpscalerInterface() != nullptr;
		UpdateDebugStatus(bIsActive);
		CommitResolutionDebugInfo(bIsActive, ResolutionDebugInfo);
		return;
	}

	if (!CanActivateSpatial(InViewFamily, MetalFXModule))
	{
		UpdateDebugStatus(false);
		return;
	}

	FMetalFXResolutionDebugInfo ResolutionDebugInfo;
	ApplyMetalFXScreenPercentageToViewFamily(
		InViewFamily,
		static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread()),
		&ResolutionDebugInfo);
	InstallSpatialUpscaler(InViewFamily, MetalFXModule);
	const bool bIsActive = InViewFamily.GetPrimarySpatialUpscalerInterface() != nullptr;
	UpdateDebugStatus(bIsActive);
	CommitResolutionDebugInfo(bIsActive, ResolutionDebugInfo);
}

bool FMetalFXViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!FSceneViewExtensionBase::IsActiveThisFrame_Internal(Context))
	{
		return false;
	}

	// Editor builds only allow MetalFX during PIE.
	if (GIsEditor)
	{
		const bool bEnableInEditor = CvarEnableMetalFXInEditor.GetValueOnGameThread();
		const UWorld* World = Context.GetWorld();

		if (!IsValid(World))
		{
			return false;
		}
		
		return bEnableInEditor && World->IsPlayInEditor();
	}

	return true;
}
