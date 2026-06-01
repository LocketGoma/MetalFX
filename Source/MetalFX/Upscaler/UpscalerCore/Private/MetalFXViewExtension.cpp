#include "MetalFXViewExtension.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFX.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscaler.h"
#include "Engine/Engine.h"
#include "RenderGraphUtils.h"

class FMetalFXCopyExposureCS;

#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
static void AddMetalFXStatusDebugMessages(bool bCanActivate, bool bIsEnabled)
{
	if (!GEngine)
	{
		return;
	}

	constexpr int32 ChannelCode = 'M'+'E'+'T'+'A'+'L'+'F'+'X';
	constexpr int32 ActivationMessageKey = ChannelCode + 'A';			//Can Activate
	constexpr int32 RuntimeMessageKey = ChannelCode + 'R';				//Enabled
	constexpr float MessageDuration = 0.1f;

	// Availability means the current RHI and MetalFX device support checks passed.
	GEngine->AddOnScreenDebugMessage(
		ActivationMessageKey,
		MessageDuration,
		bCanActivate ? FColor::Emerald : FColor::Red,
		FString::Printf(TEXT("Apple MetalFX : %s Activate"), bCanActivate ? TEXT("Can") : TEXT("Can NOT")),
		true);

	if (bCanActivate)
	{	
	// Runtime state means MetalFX is actively selected for the current view family.
	GEngine->AddOnScreenDebugMessage(
		RuntimeMessageKey,
		MessageDuration,
		bIsEnabled ? FColor::Emerald : FColor::Yellow,
		FString::Printf(TEXT("Apple MetalFX : %s"), bIsEnabled ? TEXT("Enabled") : TEXT("Disabled")),
		true);
	}
}
#endif

FMetalFXViewExtension::FMetalFXViewExtension(const FAutoRegister& AutoRegister)
: FSceneViewExtensionBase(AutoRegister)
{
	// The extension can exist on Metal RHI even when MetalFX itself is not supported.
}

void FMetalFXViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	static TConsoleVariableData<bool>* CvarMetalFXEnable = IConsoleManager::Get().FindTConsoleVariableDataBool(TEXT("r.MetalFX.Enabled"));

	//If the CVar is not registered, MetalFX must stay disabled.
	bMetalFXEnabled = CvarMetalFXEnable ? CvarMetalFXEnable->GetValueOnGameThread() : false;
}

void FMetalFXViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
	const bool bCanActivateThisFrame = MetalFXModule.GetIsSupportedByRHI();
	bool bIsEnabledThisFrame = false;

	if (InViewFamily.ViewMode != EViewModeIndex::VMI_Lit ||
		InViewFamily.Scene == nullptr ||
		InViewFamily.Scene->GetShadingPath() != EShadingPath::Deferred ||
		!InViewFamily.bRealtimeUpdate)
	{
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
		// This view family is not realtime lit deferred scene rendering.
		AddMetalFXStatusDebugMessages(bCanActivateThisFrame, false);
#endif
		return;
	}

	// MetalFX requires a valid view state and a primary temporal upscale request.
	bool bFoundPrimaryTemporalUpscale = false;
	for (const FSceneView* View : InViewFamily.Views)
	{
		if (View->State == nullptr)
		{
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
			// Views without persistent state cannot provide temporal history.
			AddMetalFXStatusDebugMessages(bCanActivateThisFrame, false);
#endif
			return;
		}

		if (View->bIsSceneCapture)
		{
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
			// Scene capture views are excluded from MetalFX temporal upscaling.
			AddMetalFXStatusDebugMessages(bCanActivateThisFrame, false);
#endif
			return;
		}

		if (View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale)
		{
			bFoundPrimaryTemporalUpscale = true;
		}
	}
	if (!bFoundPrimaryTemporalUpscale)
	{
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
		// This view family is not requesting temporal upscaling.
		AddMetalFXStatusDebugMessages(bCanActivateThisFrame, false);
#endif
		return;
	}
	
	if (bCanActivateThisFrame && bMetalFXEnabled)
	{
#if METALFX_PLUGIN_ENABLED
		FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler();
		if (!Upscaler)
		{
#if !UE_BUILD_SHIPPING
			// The device passed support checks, but the upscaler core is not ready.
			AddMetalFXStatusDebugMessages(bCanActivateThisFrame, false);
#endif
			return;
		}
		if (!InViewFamily.GetTemporalUpscalerInterface())
		{
			InViewFamily.SetTemporalUpscalerInterface(new FMetalFXTemporalUpscaler(Upscaler));
		}
		bIsEnabledThisFrame = true;
#endif //METALFX_PLUGIN_ENABLED
	}
	
#if !UE_BUILD_SHIPPING && METALFX_PLUGIN_ENABLED
	// Show availability and active runtime state as separate debug lines.
	AddMetalFXStatusDebugMessages(bCanActivateThisFrame, bIsEnabledThisFrame);
#endif
}


bool FMetalFXViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return FSceneViewExtensionBase::IsActiveThisFrame_Internal(Context);
}
