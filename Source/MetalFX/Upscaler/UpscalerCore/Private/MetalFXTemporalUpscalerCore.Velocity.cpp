/*
 * MetalFX version of DLSS-style AddVelocityCombinePass.
 *
 * Unreal's scene velocity input may be a 1x1 black fallback texture, so this
 * pass prepares a MetalFX-compatible velocity texture before upscaling.
 *
 * Do not copy proprietary DLSS implementation code into this file.
 */
#include "MetalFXTemporalUpscalerCore.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

static bool IsFallbackVelocityTexture(FRDGTextureRef VelocityTexture)
{
	const bool bVelocityTextureMissing = VelocityTexture == nullptr;
	const bool bVelocityTextureIsFallback = !bVelocityTextureMissing && VelocityTexture->Desc.Extent == FIntPoint(1, 1);
	return bVelocityTextureMissing || bVelocityTextureIsFallback;
}

// Color 와의 비교 기준이 "Input" 인지 "Output" 인지 반드시 확인 필요!
FRDGTextureRef FMetalFXTemporalUpscalerCore::PrepareVelocityTexture(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneColorTexture, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntRect InputViewRect, FIntRect OutputViewRect, FVector2D TemporalJitterPixels)
{
	if (!InSceneColorTexture)
	{
		return InVelocityTexture;
	}

	if (IsFallbackVelocityTexture(InVelocityTexture))
	{
		return AddBlackVelocityTexturePass(GraphBuilder, InputViewRect.Size());
	}

	//To do
	// The conversion pass is currently a passthrough, but forward both rects so
	// its eventual implementation receives the correct input/output geometry.
	return GenerateVelocityTexturePass(GraphBuilder, View, InSceneDepthTexture, InVelocityTexture, InputViewRect, OutputViewRect, TemporalJitterPixels);
}

FRDGTextureRef FMetalFXTemporalUpscalerCore::GenerateVelocityTexturePass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntRect InputViewRect, FIntRect OutputViewRect, FVector2D TemporalJitterPixels)
{
	return InVelocityTexture;
}

// Temporary zero-velocity fallback until the conversion pass is implemented.
FRDGTextureRef FMetalFXTemporalUpscalerCore::AddBlackVelocityTexturePass(FRDGBuilder& GraphBuilder, FIntPoint OutputExtent)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		OutputExtent,
		PF_G16R16F,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("MetalFX.BlackVelocity"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputTexture), FVector4f(0, 0, 0, 0));

	return OutputTexture;
}
