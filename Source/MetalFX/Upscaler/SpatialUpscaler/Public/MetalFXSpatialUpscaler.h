#pragma once

#include "PostProcess/PostProcessUpscale.h"

class FMetalFXSpatialUpscalerCore;

#if METALFX_PLUGIN_ENABLED
class FMetalFXSpatialUpscaler final : public ISpatialUpscaler
{
public:
	explicit FMetalFXSpatialUpscaler(FMetalFXSpatialUpscalerCore* InUpscaler);

	// ISpatialUpscaler interface
	const TCHAR* GetDebugName() const override { return TEXT("MetalFXSpatialUpscaler"); }
	ISpatialUpscaler* Fork_GameThread(const FSceneViewFamily& ViewFamily) const override;
	FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const override;

private:
	bool CheckValidate() const;

	// Non-owning. FMetalFXModule owns the Core for the module lifetime.
	FMetalFXSpatialUpscalerCore* m_FxUpscaler = nullptr;
};
#endif
