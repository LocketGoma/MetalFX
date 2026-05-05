//////////////////////////////////////////////////////////////////////////
//! CopyMobileEyeAdaptationToTextureCS
//////////////////////////////////////////////////////////////////////////

class FCopyMobileEyeAdaptationToTextureCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FCopyMobileEyeAdaptationToTextureCS);
	SHADER_USE_PARAMETER_STRUCT(FCopyMobileEyeAdaptationToTextureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, MobileEyeAdaptationBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWMobileEyeAdaptationTexture)
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform) || IsMetalMobilePlatform(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCopyMobileEyeAdaptationToTextureCS, "/Engine/Private/PostProcessMobile.usf", "CopyMobileEyeAdaptationToTextureCS", SF_Compute);

void AddCopyMobileEyeAdaptationDataToTexturePass(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* ShaderMap, FRDGBufferRef EyeAdaptationBuffer, FRDGTextureRef OutputTexture)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FCopyMobileEyeAdaptationToTextureCS::FParameters>();
	PassParameters->MobileEyeAdaptationBuffer = GraphBuilder.CreateSRV(EyeAdaptationBuffer);
	PassParameters->RWMobileEyeAdaptationTexture = GraphBuilder.CreateUAV(OutputTexture);
	
	TShaderMapRef<FCopyMobileEyeAdaptationToTextureCS> ComputeShader(ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("CopyMobileEyeAdaptationToTexture (CS)"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1));
}