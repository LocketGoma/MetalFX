#include "MetalFXSpatialUpscaler.h"

/*
#if METALFX_PLUGIN_ENABLED
void FMetalFXSpatialUpscaler::CheckValidate() const
{
	checkf(m_FxUpscaler, TEXT("MetalFX Spatial Core is not ready."));
}

FMetalFXSpatialUpscaler::FMetalFXSpatialUpscaler(FMetalFXSpatialUpscalerCore* InUpscaler)
	: m_FxUpscaler(InUpscaler)
{
}

bool FMetalFXSpatialUpscaler::GetIsSupportedDevice() const
{
	return m_FxUpscaler
		&& m_FxUpscaler->GetIsSupportedDevice() == EMetalFXSupportReason::Supported;
}

ISpatialUpscaler* FMetalFXSpatialUpscaler::Fork_GameThread(const FSceneViewFamily& ViewFamily) const
{
	return new FMetalFXSpatialUpscaler(m_FxUpscaler);
}

FScreenPassTexture FMetalFXSpatialUpscaler::AddPasses(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FInputs& PassInputs) const
{
	CheckValidate();

	// WIP: registration remains disabled. If the adapter is exercised directly,
	// preserve a safe Unreal bilinear fallback instead of pretending to encode.
	return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, PassInputs, EUpscaleMethod::Bilinear);
}
#endif
*/
