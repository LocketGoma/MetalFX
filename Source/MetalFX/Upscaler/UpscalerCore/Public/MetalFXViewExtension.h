#pragma once
#include "SceneViewExtension.h"

class FMetalFXModule;
enum class EMetalFXUpscalerType : uint8;

class FMetalFXViewExtension final : public FSceneViewExtensionBase
{
public:
	FMetalFXViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {}
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override {}

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
private:
	bool CanActivateForViewFamily(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule, bool bLogFailure = true) const;
	bool CanActivateTemporal(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule) const;
	bool CanActivateSpatial(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule) const;
	void InstallTemporalUpscaler(FSceneViewFamily& ViewFamily, FMetalFXModule& MetalFXModule) const;
	void InstallSpatialUpscaler(FSceneViewFamily& ViewFamily, FMetalFXModule& MetalFXModule) const;
};
