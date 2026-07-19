#pragma once

/*
#include "MetalFXUpscalerCore.h"
#include "MetalFXHelper.h"

#if METALFX_PLUGIN_ENABLED
class ISpatialUpscaler;

class FMetalFXSpatialUpscaler final : public ISpatialUpscaler
{
public:
	FMetalFXSpatialUpscaler(FMetalFXUpscalerCore* InUpscaler);

	const bool GetIsSupportedDevice();
	const TCHAR* GetDebugName() const override { return TEXT("MetalFXSpatialUpscaler"); }

	virtual ISpatialUpscaler* Fork_GameThread(const class FSceneViewFamily& ViewFamily) const final override;
	virtual FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const override;
	const void CheckValidate() const;

private:
	FMetalFXUpscalerCore* m_FxUpscaler;
};
#endif //METALFX_PLUGIN_ENABLED
*/
