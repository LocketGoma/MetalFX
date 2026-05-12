/*
* MetalFX version of DLSS-style AddVelocityCombinePass.
 *
 * Unreal's scene velocity input may be a 1x1 black fallback texture, so this
 * pass prepares a MetalFX-compatible velocity texture before upscaling.
 *
 * Do not copy proprietary DLSS implementation code into this file.
 */

static bool IsFallbackVelocityTexture(FRDGTextureRef VelocityTexture)
{
	return !VelocityTexture || VelocityTexture->Desc.Extent == FIntPoint(1, 1);
}

//Color 와의 비교 기준이 "Input" 인지 "output" 인지 반드시 확인 필요!
FRDGTextureRef FMetalFXUpscalerCore::PrepareVelocityTexture(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneColorTexture, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntRect InputViewRect, FIntRect OutputViewRect, FVector2f TemporalJitterPixels)
{
	if (!InSceneColorTexture)
	{
		return InVelocityTexture;
	}
	
	if (IsFallbackVelocityTexture(InVelocityTexture))
	{
		return AddBlackVelocityTexturePass(GraphBuilder, OutputViewRect.Size());
	}
	
	
	//To do
	return GenerateVelocityTexturePass(GraphBuilder, View, InSceneDepthTexture, InVelocityTexture, InputViewRect, OutputViewRect, TemporalJitterPixels);
}


FRDGTextureRef FMetalFXUpscalerCore::GenerateVelocityTexturePass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntRect InputViewRect, FIntRect OutputViewRect, FVector2f TemporalJitterPixels)
{
	return InVelocityTexture;	
}

//임시 조치용 검은색 VelocityTexture
FRDGTextureRef FMetalFXUpscalerCore::AddBlackVelocityTexturePass(FRDGBuilder& GraphBuilder, FIntPoint OutputExtent)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
	OutputExtent,
	PF_G16R16F,
	FClearValueBinding::Black,
	TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef OutputTexture =
		GraphBuilder.CreateTexture(Desc, TEXT("MetalFX.BlackVelocity"));

	AddClearUAVPass(
		GraphBuilder,
		GraphBuilder.CreateUAV(OutputTexture),
		FVector4f(0, 0, 0, 0));

	return OutputTexture;
}