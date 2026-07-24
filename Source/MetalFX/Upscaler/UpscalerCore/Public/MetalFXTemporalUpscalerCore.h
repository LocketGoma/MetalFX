#pragma once

#include "MetalFXUpscalerCore.h"

#include <memory>

struct FMetalFXTemporalEncodeInputs : FMetalFXEncodeGeometry
{
	bool bResetHistory = false;
	FVector2D JitterOffset = FVector2D::ZeroVector;
	FVector2f MotionVectorScale = FVector2f::ZeroVector;
};

/**
 * Core for MetalFX TemporalScaler.
 *
 * This owns all temporal scaler state. It is owned once by FMetalFXModule and
 * referenced non-owningly by FMetalFXTemporalUpscaler and its forks.
 */
class FMetalFXTemporalUpscalerCore final : public FMetalFXUpscalerCore
{
public:
	UE_NONCOPYABLE(FMetalFXTemporalUpscalerCore)

	FMetalFXTemporalUpscalerCore();
	virtual ~FMetalFXTemporalUpscalerCore() override;

	virtual EMetalFXUpscalerType GetUpscalerType() const override
	{
		return EMetalFXUpscalerType::Temporal;
	}

	static FRDGTextureRef PrepareVelocityTexture(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneColorTexture, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntRect InputViewRect, bool bResetHistory);

	static FRDGTextureRef GenerateVelocityTexturePass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntPoint InputTextureExtent, FIntRect InputViewRect);

#if METALFX_PLUGIN_ENABLED
	bool SetTexturesToGroup(const FMetalFXTemporalPassParameters& Parameters, FMetalFXTemporalTextureGroup& OutTextureGroup, FMetalFXTemporalTextureFormatGroup& OutFormats);

	bool PrepareToEncode(const FMetalFXTemporalEncodeInputs& Inputs, const FMetalFXTemporalTextureFormatGroup& Formats);
	void ExecuteMetalFX(FRHICommandList& CmdList, FMetalFXTemporalTextureGroup& TextureGroup);
#endif

private:
	static FRDGTextureRef AddBlackVelocityTexturePass(FRDGBuilder& GraphBuilder, FIntPoint OutputExtent);

#if METALFX_PLUGIN_ENABLED
	bool CheckValidate() const;
	bool EnsureUpscalerForConfiguration(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent, const FMetalFXTemporalTextureFormatGroup& Formats);
	bool GenerateUpscaler();
	void ResetUpscaler();

	void UpdateInputContentSize(FIntPoint InputContentExtent);
	void SetHistoryReset(bool bReset);
	void SetJitterOffset(FVector2D Offset);
	void SetMotionVectorScale(FVector2f Scale);
	void Encode(FRHICommandList& CmdList, FMetalFXTemporalTextureGroup& TextureGroup);
#endif

private:
	std::unique_ptr<struct FMetalFXTemporalCoreResources> Resources;
	FIntPoint ConfiguredDescriptorInputExtent = FIntPoint::ZeroValue;
	FIntPoint ConfiguredInputContentExtent = FIntPoint::ZeroValue;
	FIntPoint ConfiguredOutputExtent = FIntPoint::ZeroValue;
};
