#include "MetalFXViewExtension.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFX.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscaler.h"
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

static void AddMetalFXStatusDebugMessages(bool bCanActivate, bool bIsSupported, bool bIsActive, const FMetalFXActiveDebugInfo* ActiveDebugInfo = nullptr)
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
			bIsSupported ? FColor::Emerald : FColor::Yellow,
			FString::Printf(TEXT("Apple MetalFX Editor SupportState : %s"), bIsSupported ? TEXT("Enabled") : TEXT("Disabled")),
			true);
	}
	
	if (bCanActivate)
	{
		// Runtime state means MetalFX is actively selected for the current view family.
		GEngine->AddOnScreenDebugMessage(
			RuntimeMessageKey,
			MessageDuration,
			bIsActive ? FColor::Emerald : FColor::Yellow,
			FString::Printf(TEXT("Apple MetalFX : %s"), bIsActive ? TEXT("Enabled") : TEXT("Disabled")),
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
	
	//에디터에서는 EnableInEditor로 변경
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

	// MetalFX requires a valid view state and a primary temporal upscale request.
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
			
			//한번이라도 false 면 계속 false 처리 (안정성 이슈)
			//패치 스크립트를 작동했다면 모바일에서도 성공해야됨. (근데 애초에 패치 제대로 안했으면 빌드 자체가 안될거긴 해...)
			if (View->PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::TemporalUpscale)
			{
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
			FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler();
			
			if (!Upscaler)
			{
				bIsCheckPassed = false;
			}
			
			if (bIsCheckPassed && (!InViewFamily.GetTemporalUpscalerInterface()))
			{
				InViewFamily.SetTemporalUpscalerInterface(new FMetalFXTemporalUpscaler(Upscaler));
			}
#endif //METALFX_PLUGIN_ENABLED
		}
	}
	
	//For Debug messages.
#if !UE_BUILD_SHIPPING
	// Show availability and active runtime state as separate debug lines.
	const bool bIsEnabledThisFrame = bIsCheckPassed && bMetalFXEnabled;
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
	AddMetalFXStatusDebugMessages(bMetalFXSupported, bMetalFXEnabled, bIsEnabledThisFrame, &ActiveDebugInfo);
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

	//에디터에서는 PIE 환경에서만 켜지도록 처리
	if (GIsEditor)
	{
		const bool bEnableInEditor = CvarEnableMetalFXInEditor.GetValueOnGameThread();
		const UWorld* World = Context.GetWorld();

		if (!IsValid(World))
		{
			return false;
		}
		
		return bEnableInEditor && (World->WorldType == EWorldType::PIE);
	}

	return true;
}
