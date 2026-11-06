#include "MetalFXTemporalUpscalerCore.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "SystemTextures.h"

namespace
{
class FMetalFXExposureCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMetalFXExposureCS);
	SHADER_USE_PARAMETER_STRUCT(FMetalFXExposureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
		SHADER_PARAMETER(uint32, bHasEyeAdaptation)
		SHADER_PARAMETER(float, FallbackExposure)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputExposureTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMetalPlatform(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMetalFXExposureCS, "/Plugin/MetalFX/Private/MetalFXExposure.usf", "MainCS", SF_Compute);
} // namespace

FRDGTextureRef FMetalFXTemporalUpscalerCore::PrepareExposureTexture(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef EyeAdaptationTexture, float PreExposure)
{
	// UE exposes RGBA32F eye-adaptation data; MetalFX requires a 1x1 R16Float texture.
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R16F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef ExposureTexture = GraphBuilder.CreateTexture(Desc, TEXT("MetalFX.Exposure"));

	auto* PassParameters = GraphBuilder.AllocParameters<FMetalFXExposureCS::FParameters>();
	PassParameters->EyeAdaptationTexture = EyeAdaptationTexture ? EyeAdaptationTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
	PassParameters->bHasEyeAdaptation = EyeAdaptationTexture ? 1u : 0u;
	// With no valid eye adaptation, use a neutral multiplier on pre-exposed input.
	PassParameters->FallbackExposure = FMath::IsFinite(PreExposure) && PreExposure > 0.0f ? PreExposure : 1.0f;
	PassParameters->OutputExposureTexture = GraphBuilder.CreateUAV(ExposureTexture);

	TShaderMapRef<FMetalFXExposureCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("MetalFX Prepare Exposure"), ComputeShader, PassParameters, FIntVector(1, 1, 1));
	return ExposureTexture;
}
