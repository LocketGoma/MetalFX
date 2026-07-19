/*
#include "MetalFXSpatialUpscaler.h"

#if METALFX_PLUGIN_ENABLED
const void FMetalFXSpatialUpscaler::CheckValidate() const
{
	checkf(m_FxUpscaler, TEXT("MetalFX Upscaler is not ready. Check MetalFXSpatialUpscaler for more information."));
}

FMetalFXSpatialUpscaler::FMetalFXSpatialUpscaler(FMetalFXUpscalerCore* InUpscaler)
{
	m_FxUpscaler = InUpscaler;
}

const bool FMetalFXSpatialUpscaler::GetIsSupportedDevice()
{
	if (m_FxUpscaler)
	{
		return (m_FxUpscaler->GetIsSupportedDevice() == EMetalFXSupportReason::Supported);
	}
	return false;
}

ISpatialUpscaler* FMetalFXSpatialUpscaler::Fork_GameThread(const class FSceneViewFamily& ViewFamily) const
{
	return new FMetalFXSpatialUpscaler(m_FxUpscaler);
}

FScreenPassTexture FMetalFXSpatialUpscaler::AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const
{
	CheckValidate();

	// WIP: keep the spatial mode path alive until MetalFX SpatialScaler is wired.
	return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, PassInputs, EUpscaleMethod::Bilinear);
}
#endif //METALFX_PLUGIN_ENABLED
*/
