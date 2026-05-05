#pragma once
#include "SceneViewExtension.h"
#if METALFX_PLUGIN_ENABLED
#include "MetalFXTemporalUpscaler.h"
#endif //METALFX_PLUGIN_ENABLED

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
#if METALFX_PLUGIN_ENABLED
	TUniquePtr<FMetalFXTemporalUpscaler> m_Upscaler;
#endif //METALFX_PLUGIN_ENABLED

	//애초에 Not Supported면 ViewExtension 생성 자체가 안됨
	bool bMetalFXEnabled = false;
};