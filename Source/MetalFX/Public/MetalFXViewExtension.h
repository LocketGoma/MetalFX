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

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	
	virtual void PreRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily) override {};
	virtual void PreRenderView_RenderThread(FRenderGraphType& GraphBuilder, FSceneView& InView) override {};
	virtual void PostRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily) override {};

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
