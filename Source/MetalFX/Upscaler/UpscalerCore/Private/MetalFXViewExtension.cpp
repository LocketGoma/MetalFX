#include "MetalFXViewExtension.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFX.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscaler.h"
#include "Engine/Engine.h"
#include "RenderGraphUtils.h"

class FMetalFXCopyExposureCS;

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
			GEngine->AddOnScreenDebugMessage(
				ActiveDetailsMessageKey,
				MessageDuration,
				bIsActive ? FColor::Emerald : FColor::Red,
				FString::Printf(
					TEXT("Apple MetalFX %s : InputRect[%s] OutputRect[%s] ScreenPercentage=%.2f"),
					bIsActive ? TEXT("Active") : TEXT("Deactive"),
					*FormatMetalFXRect(ActiveDebugInfo->InputRect),
					*FormatMetalFXRect(ActiveDebugInfo->OutputRect),
					ActiveDebugInfo->ScreenPercentage),
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
		bMetalFXSupported = CvarMetalFXEditorSupported ? CvarMetalFXEditorSupported->GetValueOnGameThread() : bMetalFXSupported;
	}
	
	//If the CVar is not registered, MetalFX must stay disabled.
	bMetalFXEnabled = CvarMetalFXEnable ? CvarMetalFXEnable->GetValueOnGameThread() : false;
	
}

void FMetalFXViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	bool bIsEnabledThisFrame = false;
	bool bIsCheckPassed = true;

	if (GIsEditor)
	{
		if (!(bMetalFXEnabled && bMetalFXSupported))
		{
			bIsCheckPassed = false;
		}
	}	
	
	if (InViewFamily.ViewMode != EViewModeIndex::VMI_Lit ||	InViewFamily.Scene == nullptr || InViewFamily.Scene->GetShadingPath() != EShadingPath::Deferred || !InViewFamily.bRealtimeUpdate)
	{
		bIsCheckPassed = false;
	}

	// MetalFX requires a valid view state and a primary temporal upscale request.
	bool bFoundPrimaryTemporalUpscale = true;
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
			if (View->PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::TemporalUpscale)
			{
				bFoundPrimaryTemporalUpscale = false;
			}
		}
	}
	
	if (bIsCheckPassed)
	{
		if (!bFoundPrimaryTemporalUpscale)
		{
			bIsCheckPassed = false;
		}
		
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
	
	bIsEnabledThisFrame = bIsCheckPassed && bMetalFXEnabled;
	
#if !UE_BUILD_SHIPPING
	// Show availability and active runtime state as separate debug lines.
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


bool FMetalFXViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return FSceneViewExtensionBase::IsActiveThisFrame_Internal(Context);
}
