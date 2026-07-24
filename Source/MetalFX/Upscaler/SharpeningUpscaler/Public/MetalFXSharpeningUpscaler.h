#pragma once

#include "PostProcess/PostProcessUpscale.h"

/**
 * Applies RCAS after MetalFX has produced its output.
 *
 * RCAS does not resize an image by itself. When a secondary resolution step is
 * still required, this adapter first uses Unreal Engine's normal secondary
 * scaling method and then sharpens the final-size image.
 */
class FMetalFXSharpeningUpscaler final : public ISpatialUpscaler
{
public:
	virtual const TCHAR* GetDebugName() const override;
	virtual ISpatialUpscaler* Fork_GameThread(const FSceneViewFamily& ViewFamily) const override;
	virtual FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const override;
};
