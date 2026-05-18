#include "MetalFXViewExtension.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFX.h"
#include "MetalFXSettings.h"
#include "MetalFXTemporalUpscaler.h"
#include "RenderGraphUtils.h"

class FMetalFXCopyExposureCS;

FMetalFXViewExtension::FMetalFXViewExtension(const FAutoRegister& AutoRegister)
: FSceneViewExtensionBase(AutoRegister)
{
	//어차피 MetalFX 못쓰면 생성도 안됨.
}

void FMetalFXViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	static TConsoleVariableData<bool>* CvarMetalFXEnable = IConsoleManager::Get().FindTConsoleVariableDataBool(TEXT("r.MetalFX.Enabled"));

	bMetalFXEnabled = CvarMetalFXEnable->GetValueOnGameThread();
}

//DLSS 로직 그대로 갖고옴 ㅎㅎ;
void FMetalFXViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (InViewFamily.ViewMode != EViewModeIndex::VMI_Lit ||
		InViewFamily.Scene == nullptr ||
		InViewFamily.Scene->GetShadingPath() != EShadingPath::Deferred ||
		!InViewFamily.bRealtimeUpdate)
	{
		return;
	}

	// Early returns if none of the view have a view state or if primary temporal upscaling isn't requested
	bool bFoundPrimaryTemporalUpscale = false;
	for (const FSceneView* View : InViewFamily.Views)
	{
		if (View->State == nullptr)
		{
			return;
		}

		if (View->bIsSceneCapture)
		{
			return;
		}

		if (View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale)
		{
			bFoundPrimaryTemporalUpscale = true;
		}
	}
	if (!bFoundPrimaryTemporalUpscale)
	{
		return;
	}
	
	if (bMetalFXEnabled)
	{
#if METALFX_PLUGIN_ENABLED
		FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
		FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler();
		if (!Upscaler)
		{
			return;
		}
		if (!InViewFamily.GetTemporalUpscalerInterface())
		{
			InViewFamily.SetTemporalUpscalerInterface(new FMetalFXTemporalUpscaler(Upscaler));
		}
#endif //METALFX_PLUGIN_ENABLED
	}
	
}


bool FMetalFXViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return FSceneViewExtensionBase::IsActiveThisFrame_Internal(Context);
}
