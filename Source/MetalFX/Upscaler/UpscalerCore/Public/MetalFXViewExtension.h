#pragma once
#include "SceneViewExtension.h"

typedef FRDGBuilder FRenderGraphType;

class FMetalFXModule;
enum class EMetalFXUpscalerType : uint8;

class FMetalFXViewExtension final : public FSceneViewExtensionBase
{
public:
	FMetalFXViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRenderGraphType& GraphBuilder, FSceneView& InView) override {};
	virtual void PostRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily) override {};

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
private:
	bool CanActivateForViewFamily(const FSceneViewFamily& ViewFamily, EMetalFXUpscalerType UpscalerType) const;
	bool CanActivateTemporal(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule) const;
	bool CanActivateSpatial(const FSceneViewFamily& ViewFamily, const FMetalFXModule& MetalFXModule) const;
	void InstallTemporalUpscaler(FSceneViewFamily& ViewFamily, FMetalFXModule& MetalFXModule) const;
	void InstallSpatialUpscaler(FSceneViewFamily& ViewFamily, FMetalFXModule& MetalFXModule) const;

	const bool IsMetalFXEnable() const { return bMetalFXEnabled; }

private:
	//The extension can be created even when MetalFX cannot activate.
	bool bMetalFXEnabled = false;
	
	//bIsSupporeted && (if GIsEditor)
	bool bMetalFXSupported = false;
};
