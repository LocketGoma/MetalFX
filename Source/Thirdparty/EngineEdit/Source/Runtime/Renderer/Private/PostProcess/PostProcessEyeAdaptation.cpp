//-----------Edit This--------------
FRDGTextureRef AddCopyEyeAdaptationDataToTexturePass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	const FRDGTextureDesc TextureDesc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(TextureDesc, TEXT("EyeAdaptationTexture"));

	//If MobileDevice - Use MobileEyeAdaptation.
	if (PLATFORM_ANDROID || PLATFORM_IOS)
	{
		AddCopyMobileEyeAdaptationDataToTexturePass(GraphBuilder, View.ShaderMap, GetEyeAdaptationBuffer(GraphBuilder, View), OutputTexture);		
	}
	else 
	{
		AddCopyEyeAdaptationDataToTexturePass(GraphBuilder, View.ShaderMap, GetEyeAdaptationBuffer(GraphBuilder, View), OutputTexture);
	}
	return OutputTexture;
}