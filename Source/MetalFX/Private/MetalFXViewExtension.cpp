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
void FMetalFXViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{	
//Apple Upscaler (MetalFX) 설정 변경은 Mac/모바일(iOS) 환경에서만 가능하도록 처리

	// 최종 렌더 사이즈
	FIntPoint OutputRect = InViewFamily.RenderTarget->GetSizeXY();
	FIntPoint InputRect = OutputRect;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(InViewFamily.GetFeatureLevel());
	
	//현재 뷰 패밀리에서 ShaderMap 안잡히면 바로 Out
	if (!ShaderMap)
	{
		bMetalFXEnabled = false;
		return;
	}
	
#if WITH_METAL_PLATFORM
	FMetalFXModule& MetalFXModule = FModuleManager::GetModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
	FMetalFXUpscalerCore* Upscaler = MetalFXModule.GetMetalFXUpscaler();
	bool IsTemporalUpscalingRequested = false;

	if (!Upscaler)
	{
		return;
	}
	
	Upscaler->UpdateResolution(InputRect, OutputRect);
	
	for (auto* InView : InViewFamily.Views)
	{
		IsTemporalUpscalingRequested |= (InView->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale);
	}

	if (bMetalFXEnabled && InViewFamily.GetTemporalUpscalerInterface() == nullptr)
	{
		InViewFamily.SetTemporalUpscalerInterface(new FMetalFXTemporalUpscaler(Upscaler));
	}
#endif
}

void FMetalFXViewExtension::PreRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	FSceneViewExtensionBase::PreRenderViewFamily_RenderThread(GraphBuilder, InViewFamily);
}

void FMetalFXViewExtension::PreRenderView_RenderThread(FRenderGraphType& GraphBuilder, FSceneView& InView)
{
	FSceneViewExtensionBase::PreRenderView_RenderThread(GraphBuilder, InView);
}

void FMetalFXViewExtension::PostRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	FSceneViewExtensionBase::PostRenderViewFamily_RenderThread(GraphBuilder, InViewFamily);
	
	if (IsMetalFXEnable())
	{
		GraphBuilder.RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
}

void FMetalFXViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	FSceneViewExtensionBase::PrePostProcessPass_RenderThread(GraphBuilder, View, Inputs);
}

bool FMetalFXViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return FSceneViewExtensionBase::IsActiveThisFrame_Internal(Context);
}
