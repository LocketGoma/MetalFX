#pragma once

#include "MetalFXSpatialUpscalerCore.h"



/**
 * WIP Unreal spatial adapter.
 *
 * It is compiled against the dedicated Spatial Core, but ViewExtension does
 * not register it until real MetalFX SpatialScaler encode is implemented.
 */

/*
#if METALFX_PLUGIN_ENABLED
class FMetalFXSpatialUpscaler final : public ISpatialUpscaler
{
public:
	explicit FMetalFXSpatialUpscaler(FMetalFXSpatialUpscalerCore* InUpscaler);

	bool GetIsSupportedDevice() const;
	virtual const TCHAR* GetDebugName() const override { return TEXT("MetalFXSpatialUpscaler"); }

	virtual ISpatialUpscaler* Fork_GameThread(const FSceneViewFamily& ViewFamily) const final override;
	virtual FScreenPassTexture AddPasses(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FInputs& PassInputs) const override;

private:
	void CheckValidate() const;

	// Non-owning. FMetalFXModule owns the Core for the module lifetime.
	FMetalFXSpatialUpscalerCore* m_FxUpscaler = nullptr;
};
#endif
*/
