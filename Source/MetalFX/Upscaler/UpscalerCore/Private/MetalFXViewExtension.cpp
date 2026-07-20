#include "MetalFXViewExtension.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFX.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscaler.h"
#include "MetalFXSpatialUpscaler.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "RenderGraphUtils.h"

#if !UE_BUILD_SHIPPING
static FString FormatMetalFXFullRect(const FIntRect& Rect)
{
	return FString::Printf(
		TEXT("Min=(%d,%d) Max=(%d,%d) Size=%dx%d"),
		Rect.Min.X,
		Rect.Min.Y,
		Rect.Max.X,
		Rect.Max.Y,
		Rect.Width(),
		Rect.Height());
}

static FString FormatMetalFXRect(const FIntRect& Rect)
{
	return FString::Printf(
		TEXT("Size=%dx%d"),
		Rect.Width(),
		Rect.Height());
}

static float GetMetalFXDebugScreenPercentageValue()
{
	IConsoleVariable* CVarScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	return CVarScreenPercentage ? CVarScreenPercentage->GetFloat() : 0.0f;
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

		if (ActiveDebugInfo && ActiveDebugInfo->bIsValid)
		{
			const FIntPoint ExpectedInputSize = GetMetalFXExpectedInputSize(ActiveDebugInfo->OutputRect, ActiveDebugInfo->ScreenPercentage);
			const FVector2D ActualScreenPercentage = GetMetalFXActualScreenPercentage(ActiveDebugInfo->InputRect, ActiveDebugInfo->OutputRect);

			GEngine->AddOnScreenDebugMessage(
				ActiveDetailsMessageKey,
				MessageDuration,
				bIsActive ? FColor::Emerald : FColor::Red,
				FString::Printf(
					TEXT("Apple MetalFX %s : InputRect[%s] OutputRect[%s] ExpectedInput[Size=%dx%d] ScreenPercentage=%.2f ActualSP=%.2f/%.2f"),
					bIsActive ? TEXT("Active") : TEXT("Deactive"),
					*FormatMetalFXRect(ActiveDebugInfo->InputRect),
					*FormatMetalFXRect(ActiveDebugInfo->OutputRect),
					ExpectedInputSize.X,
					ExpectedInputSize.Y,
					ActiveDebugInfo->ScreenPercentage,
					ActualScreenPercentage.X,
					ActualScreenPercentage.Y),
				true);
		}
	}
}
#endif

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

void FMetalFXViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	bool bIsCheckPassed = true;

	if (GIsEditor)
	{
		if (!(bMetalFXEnabled && bMetalFXSupported))
		{
			bIsCheckPassed = false;
		}
	}	
	
	if (InViewFamily.Scene == nullptr)
	{
		return;
	}

	// MetalFX can be selected from deferred desktop rendering and mobile/iOS rendering paths.
	const EShadingPath ShadingPath = InViewFamily.Scene->GetShadingPath();
	const bool bSupportedShadingPath = (ShadingPath == EShadingPath::Deferred || ShadingPath == EShadingPath::Mobile);

	if (InViewFamily.ViewMode != EViewModeIndex::VMI_Lit || !bSupportedShadingPath || !InViewFamily.bRealtimeUpdate)
	{
		bIsCheckPassed = false;
	}

	const EMetalFXUpscalerType UpscalerType = static_cast<EMetalFXUpscalerType>(CVarMetalFXUpscalerMode.GetValueOnGameThread());

	// MetalFX requires a valid view state and a matching primary upscale request.
	if (bIsCheckPassed)
	{
		for (const FSceneView* View : InViewFamily.Views)
		{
			if (View->State == nullptr)
			{
				bIsCheckPassed = false;	
				break;
			}
			
			if (View->bIsSceneCapture)
			{
				bIsCheckPassed = false;
				break;
			}
			
			switch (UpscalerType)
			{
			case EMetalFXUpscalerType::None:
				bIsCheckPassed = false;
				break;
			case EMetalFXUpscalerType::Spatial:
				//bIsCheckPassed = (View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::SpatialUpscale);
				// The Spatial adapter is intentionally disabled while its integration is still being implemented.
				bIsCheckPassed = false;
				break;
			case EMetalFXUpscalerType::Temporal:
				bIsCheckPassed = (View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale);
				break;
			case EMetalFXUpscalerType::MAX:
				bIsCheckPassed = false;
				break;
			}
		}
	}
	
	if (bIsCheckPassed)
	{
		if (bMetalFXSupported && bMetalFXEnabled && bIsCheckPassed)
		{
//Metal RHI 인 경우에 View Extension이 만들어지긴 하나, Plugin은 Disabled 일 수 있음.			
#if METALFX_PLUGIN_ENABLED
			FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
			if (!MetalFXModule.GetMetalFXUpscaler())
			{
				// Enabled only controls activation. Without the startup-created
				// Core, no upscaler logic is allowed to run.
				bIsCheckPassed = false;
			}

			if (bIsCheckPassed)
			{
				if (UpscalerType == EMetalFXUpscalerType::Spatial && !InViewFamily.GetPrimarySpatialUpscalerInterface())
				{
					if (FMetalFXSpatialUpscalerCore* SpatialCore = MetalFXModule.GetMetalFXSpatialUpscaler())
					{
						InViewFamily.SetPrimarySpatialUpscalerInterface(new FMetalFXSpatialUpscaler(SpatialCore));
					}
					else
					{
						bIsCheckPassed = false;
					}
				}
				
				if (UpscalerType == EMetalFXUpscalerType::Temporal && !InViewFamily.GetTemporalUpscalerInterface())
				{
					if (FMetalFXTemporalUpscalerCore* TemporalCore = MetalFXModule.GetMetalFXTemporalUpscaler())
					{
						InViewFamily.SetTemporalUpscalerInterface(new FMetalFXTemporalUpscaler(TemporalCore));
					}
					else
					{
						bIsCheckPassed = false;
					}
				}
			}
#endif //METALFX_PLUGIN_ENABLED
		}
	}
	
	//For Debug messages.
#if !UE_BUILD_SHIPPING
	// Show availability and active runtime state as separate debug lines.
	const bool bIsEnabledThisFrame = bIsCheckPassed && bMetalFXEnabled;
	const bool bEnableInEditor = CvarEnableMetalFXInEditor.GetValueOnGameThread();
	FMetalFXActiveDebugInfo ActiveDebugInfo;
#if METALFX_PLUGIN_ENABLED
	if (bMetalFXSupported)
	{
		FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
		FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler();
		if (Upscaler)
		{
			ActiveDebugInfo = Upscaler->GetActiveDebugInfo();
		}
	}
#endif //METALFX_PLUGIN_ENABLED
	AddMetalFXStatusDebugMessages(bMetalFXSupported, bEnableInEditor, bIsEnabledThisFrame, UpscalerType, &ActiveDebugInfo);
#endif
}

void FMetalFXViewExtension::PreRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// Explicitly call the base implementation for completeness. In the current 5.7.4 baseline this is a no-op.
	FSceneViewExtensionBase::PreRenderViewFamily_RenderThread(GraphBuilder, InViewFamily);

	//Debug Settings
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
	if (!CVarMetalFXDebugDisplay.GetValueOnRenderThread() || InViewFamily.Views.Num() == 0)
	{
		return;
	}

	const FSceneView* View = InViewFamily.Views[0];
	if (!View || View->UnscaledViewRect.IsEmpty())
	{
		return;
	}

	FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
	FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler();
	
	if (!Upscaler)
	{
		return;
	}

	const FIntRect OutputRect(FIntPoint::ZeroValue, View->UnscaledViewRect.Size());
	if (OutputRect.IsEmpty())
	{
		return;
	}

	//For debug status update.
	const float ScreenPercentage = GetMetalFXDebugScreenPercentageValue();
	const FIntRect ExpectedInputRect(FIntPoint::ZeroValue, GetMetalFXExpectedInputSize(OutputRect, ScreenPercentage));
	Upscaler->UpdateActiveDebugInfo(ExpectedInputRect, OutputRect, ScreenPercentage);
#endif
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
