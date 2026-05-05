#pragma once
#include "SceneViewExtension.h"
#if WITH_METAL_PLATFORM
#include "MetalFXTemporalUpscaler.h"
#endif

typedef FRDGBuilder FRenderGraphType;

class FMetalFXViewExtension final : public FSceneViewExtensionBase
{
public:
	FMetalFXViewExtension(const FAutoRegister& AutoRegister);

	void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	
	void PreRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	void PreRenderView_RenderThread(FRenderGraphType& GraphBuilder, FSceneView& InView) override;
	void PostRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
private:

	const bool IsMetalFXEnable() const { return bMetalFXEnabled;}

private:
#if WITH_METAL_PLATFORM
	TUniquePtr<FMetalFXTemporalUpscaler> m_Upscaler;
#endif

	//애초에 Not Supported면 ViewExtension 생성 자체가 안됨
	bool bMetalFXEnabled = false;
};