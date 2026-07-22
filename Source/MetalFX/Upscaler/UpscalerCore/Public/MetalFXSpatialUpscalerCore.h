#pragma once

#include "MetalFXUpscalerCore.h"

#include <memory>

/**
 * Immutable per-pass values captured by the Unreal spatial adapter.
 * Texture objects are carried separately through RDG pass parameters.
 */
struct FMetalFXSpatialEncodeInputs
{
	FIntPoint InputTextureExtent = FIntPoint::ZeroValue;
	FIntPoint InputContentExtent = FIntPoint::ZeroValue;
	FIntPoint OutputExtent = FIntPoint::ZeroValue;
	FIntRect InputRect = FIntRect();
	FIntRect OutputRect = FIntRect();
	float ScreenPercentage = 100.0f;
};

/**
 * Core for MetalFX SpatialScaler. Spatial mode intentionally has no temporal
 * history, depth, motion-vector, exposure, jitter, or reset state.
 */
class FMetalFXSpatialUpscalerCore final : public FMetalFXUpscalerCore
{
public:
	UE_NONCOPYABLE(FMetalFXSpatialUpscalerCore)

	FMetalFXSpatialUpscalerCore();
	virtual ~FMetalFXSpatialUpscalerCore() override;

	virtual EMetalFXUpscalerType GetUpscalerType() const override
	{
		return EMetalFXUpscalerType::Spatial;
	}

#if METALFX_PLUGIN_ENABLED
	// RDG texture conversion and frame encode entry points.
	bool SetTexturesToGroup(
		const FMetalFXSpatialPassParameters& Parameters,
		FMetalFXSpatialTextureGroup& OutTextureGroup,
		FMetalFXSpatialTextureFormatGroup& OutFormats);
	bool PrepareToEncode(const FMetalFXSpatialEncodeInputs& Inputs, const FMetalFXSpatialTextureFormatGroup& Formats);
	void ExecuteMetalFX(FRHICommandList& CmdList, FMetalFXSpatialTextureGroup& TextureGroup);
#endif

private:
#if METALFX_PLUGIN_ENABLED
	// Scaler validation, configuration, creation, and native encoding.
	bool CheckValidate() const;
	bool EnsureUpscalerForConfiguration(
		const FMetalFXSpatialEncodeInputs& Inputs,
		const FMetalFXSpatialTextureFormatGroup& Formats);
	bool GenerateUpscaler();
	void UpdateInputContentSize(FIntPoint InputContentExtent);
	void Encode(FRHICommandList& CmdList, FMetalFXSpatialTextureGroup& TextureGroup);
#endif

	// Module-lifetime scaler state and its currently configured descriptor.
	std::unique_ptr<struct FMetalFXSpatialCoreResources> Resources;
	uint32 InputTextureWidth = 0;
	uint32 InputTextureHeight = 0;
	uint32 InputContentWidth = 0;
	uint32 InputContentHeight = 0;
	uint32 OutputWidth = 0;
	uint32 OutputHeight = 0;
};
