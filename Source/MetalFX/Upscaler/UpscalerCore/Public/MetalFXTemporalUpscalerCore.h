#pragma once

#include "MetalFXUpscalerCore.h"

#include <memory>

struct FMetalFXTemporalEncodeInputs
{
	FIntPoint InputTextureExtent = FIntPoint::ZeroValue;
	FIntPoint InputContentExtent = FIntPoint::ZeroValue;
	FIntPoint OutputExtent = FIntPoint::ZeroValue;
	FIntRect InputRect = FIntRect();
	FIntRect OutputRect = FIntRect();
	FVector2D JitterOffset = FVector2D::ZeroVector;
	FVector2f MotionVectorScale = FVector2f::ZeroVector;
	float ScreenPercentage = 100.0f;
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

	float GetMinUpsampleResolutionFraction() const;
	float GetMaxUpsampleResolutionFraction() const;

	static FRDGTextureRef PrepareVelocityTexture(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef InSceneColorTexture,
		FRDGTextureRef InSceneDepthTexture,
		FRDGTextureRef InVelocityTexture,
		FIntRect InputViewRect,
		FIntRect OutputViewRect,
		FVector2D TemporalJitterPixels);

	static FRDGTextureRef GenerateVelocityTexturePass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef InSceneDepthTexture,
		FRDGTextureRef InVelocityTexture,
		FIntRect InputViewRect,
		FIntRect OutputViewRect,
		FVector2D TemporalJitterPixels);

#if METALFX_PLUGIN_ENABLED
	bool SetTexturesToGroup(
		const FMetalFXTemporalPassParameters& Parameters,
		FMetalFXTemporalTextureGroup& OutTextureGroup);

	bool PrepareToEncode(const FMetalFXTemporalEncodeInputs& Inputs);
	void ExecuteMetalFX(FRHICommandList& CmdList, FMetalFXTemporalTextureGroup& TextureGroup);
#endif

private:
	static FRDGTextureRef AddBlackVelocityTexturePass(
		FRDGBuilder& GraphBuilder,
		FIntPoint OutputExtent);

#if METALFX_PLUGIN_ENABLED
	bool CheckValidate() const;
	bool CheckForExecuteMetalFX(
		FIntPoint InputTextureExtent,
		FIntPoint InputContentExtent,
		FIntPoint OutputExtent);
	bool EnsureUpscalerForConfiguration(
		FIntPoint InputTextureExtent,
		FIntPoint InputContentExtent,
		FIntPoint OutputExtent,
		const FMetalFXTemporalTextureFormatGroup& Formats);
	bool GenerateUpscaler();

	void UpdateInputContentSize(FIntPoint InputContentExtent);
	void SetJitterOffset(FVector2D Offset);
	void SetMotionVectorScale(FVector2f Scale);
	void Encode(FRHICommandList& CmdList, FMetalFXTemporalTextureGroup& TextureGroup);
#endif

private:
	std::unique_ptr<struct FMetalFXTemporalCoreResources> Resources;

	uint32 m_InputTextureW = 2560;
	uint32 m_InputTextureH = 1440;
	uint32 m_InputContentW = 2560;
	uint32 m_InputContentH = 1440;
	uint32 m_OutputW = 2560;
	uint32 m_OutputH = 1440;
};
