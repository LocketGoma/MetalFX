#pragma once
#include "SceneViewExtension.h"

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
	//The extension can be created even when MetalFX cannot activate.
	bool bMetalFXEnabled = false;
	
	//bIsSupporeted && (if GIsEditor)
	bool bMetalFXSupported = false;
};
