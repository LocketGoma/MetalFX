/*
 * MetalFX version of DLSS-style AddVelocityCombinePass.
 *
 * Unreal's scene velocity input may be a 1x1 black fallback texture, so this
 * pass prepares a MetalFX-compatible velocity texture before upscaling.
 *
 * Do not copy proprietary DLSS implementation code into this file.
 */
#include "MetalFXTemporalUpscalerCore.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SystemTextures.h"

namespace
{
class FMetalFXVelocityCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMetalFXVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FMetalFXVelocityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FIntPoint, InputViewMin)
		SHADER_PARAMETER(FIntPoint, InputViewSize)
		SHADER_PARAMETER(uint32, bHasSceneVelocity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputVelocityTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMetalPlatform(Parameters.Platform);
	}

	static constexpr int32 ThreadGroupSize = 8;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMetalFXVelocityCS, "/Plugin/MetalFX/Private/MetalFXVelocity.usf", "MainCS", SF_Compute);

static bool HasUsableSceneVelocity(FRDGTextureRef VelocityTexture)
{
	return VelocityTexture && VelocityTexture->Desc.Extent != FIntPoint(1, 1);
}
} // namespace

FRDGTextureRef FMetalFXTemporalUpscalerCore::PrepareVelocityTexture(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneColorTexture, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntRect InputViewRect, bool bResetHistory)
{
	if (!InSceneColorTexture || InputViewRect.IsEmpty())
	{
		return nullptr;
	}

	if (bResetHistory)
	{
		return AddBlackVelocityTexturePass(GraphBuilder, InSceneColorTexture->Desc.Extent);
	}

	if (!InSceneDepthTexture)
	{
		return nullptr;
	}

	return GenerateVelocityTexturePass(GraphBuilder, View, InSceneDepthTexture, InVelocityTexture, InSceneColorTexture->Desc.Extent, InputViewRect);
}

FRDGTextureRef FMetalFXTemporalUpscalerCore::GenerateVelocityTexturePass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef InSceneDepthTexture, FRDGTextureRef InVelocityTexture, FIntPoint InputTextureExtent, FIntRect InputViewRect)
{
	const bool bHasSceneVelocity = HasUsableSceneVelocity(InVelocityTexture);
	FRDGTextureRef VelocityTexture = bHasSceneVelocity
		? InVelocityTexture
		: GSystemTextures.GetBlackDummy(GraphBuilder);

	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		InputTextureExtent,
		PF_G16R16F,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("MetalFX.Velocity"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputTexture), FVector4f::Zero());

	FMetalFXVelocityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMetalFXVelocityCS::FParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->InputViewMin = InputViewRect.Min;
	PassParameters->InputViewSize = InputViewRect.Size();
	PassParameters->bHasSceneVelocity = bHasSceneVelocity ? 1u : 0u;
	PassParameters->DepthTexture = InSceneDepthTexture;
	PassParameters->VelocityTexture = VelocityTexture;
	PassParameters->OutputVelocityTexture = GraphBuilder.CreateUAV(OutputTexture);

	TShaderMapRef<FMetalFXVelocityCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MetalFX Prepare Velocity"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(InputViewRect.Size(), FMetalFXVelocityCS::ThreadGroupSize));

	return OutputTexture;
}

FRDGTextureRef FMetalFXTemporalUpscalerCore::AddBlackVelocityTexturePass(FRDGBuilder& GraphBuilder, FIntPoint OutputExtent)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		OutputExtent,
		PF_G16R16F,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("MetalFX.ResetVelocity"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputTexture), FVector4f::Zero());
	return OutputTexture;
}
