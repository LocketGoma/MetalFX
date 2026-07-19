#pragma once

#include "MetalFXUpscalerCore.h"

#include <memory>

struct FMetalFXSpatialEncodeInputs
{
	FRDGTextureRef Color = nullptr;
	FRDGTextureRef Output = nullptr;
	FIntRect InputRect = FIntRect();
	FIntRect OutputRect = FIntRect();
};

/**
 * WIP Core for MetalFX SpatialScaler.
 *
 * It intentionally has no temporal history, depth, motion-vector, exposure,
 * jitter, or reset state. Actual SpatialScaler encode is a follow-up task.
 */
class FMetalFXSpatialUpscalerCore final : public FMetalFXUpscalerCore
{
public:
	UE_NONCOPYABLE(FMetalFXSpatialUpscalerCore)

	FMetalFXSpatialUpscalerCore();
	virtual ~FMetalFXSpatialUpscalerCore() override;

	virtual EMetalFXUpscalerMode GetUpscalerMode() const override
	{
		return EMetalFXUpscalerMode::Spatial;
	}

	bool IsSpatialEncodeAvailable() const
	{
		return false;
	}

#if METALFX_PLUGIN_ENABLED
	bool SetTexturesToGroup(
		const FMetalFXSpatialPassParameters& Parameters,
		FMetalFXSpatialTextureGroup& OutTextureGroup);
#endif

private:
	std::unique_ptr<struct FMetalFXSpatialCoreResources> Resources;
};
