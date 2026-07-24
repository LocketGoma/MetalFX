#include "MetalFXSharpeningUpscaler.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "MetalFXSettings.h"
#include "PixelShaderUtils.h"
#include "ScenePrivate.h"

DECLARE_GPU_STAT(MetalFXRCAS);

namespace MetalFXSharpening
{
class FRCASPixelShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRCASPixelShader);
	SHADER_USE_PARAMETER_STRUCT(FRCASPixelShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER(FIntPoint, InputViewMin)
		SHADER_PARAMETER(FIntPoint, InputViewMax)
		SHADER_PARAMETER(FIntPoint, OutputViewMin)
		SHADER_PARAMETER(float, Sharpness)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMetalPlatform(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRCASPixelShader, "/Plugin/MetalFX/Private/MetalFXRCAS.usf", "MainPS", SF_Pixel);

static EUpscaleMethod GetSecondaryUpscaleMethod(const FViewInfo& View)
{
	if (View.Family && View.Family->SecondaryScreenPercentageMethod == ESecondaryScreenPercentageMethod::LowerPixelDensitySimulation)
	{
		return EUpscaleMethod::SmoothStep;
	}

	return EUpscaleMethod::Nearest;
}

static FScreenPassTexture ScaleToFinalOutputIfNeeded(FRDGBuilder& GraphBuilder, const FViewInfo& View, const ISpatialUpscaler::FInputs& PassInputs)
{
	const FIntPoint FinalOutputSize = PassInputs.OverrideOutput.IsValid() ? PassInputs.OverrideOutput.ViewRect.Size() : View.UnscaledViewRect.Size();
	// Match Unreal Engine's default Secondary path, including overscan and
	// asymmetric crop handling.
	const FScreenPassTexture SecondaryInput(PassInputs.SceneColor.Texture, View.GetSecondaryViewCropRect());

	if (SecondaryInput.ViewRect.Size() == FinalOutputSize)
	{
		return SecondaryInput;
	}

	ISpatialUpscaler::FInputs ScaleInputs = PassInputs;
	ScaleInputs.OverrideOutput = FScreenPassRenderTarget();

	return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, ScaleInputs, GetSecondaryUpscaleMethod(View));
}

static FScreenPassTexture AddRCASPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassTexture& Input, const FScreenPassRenderTarget& OverrideOutput)
{
	FScreenPassRenderTarget Output = OverrideOutput;
	if (!Output.IsValid())
	{
		FRDGTextureDesc OutputDesc = Input.Texture->Desc;
		OutputDesc.Reset();
		OutputDesc.Flags |= TexCreate_ShaderResource;

		Output.Texture = GraphBuilder.CreateTexture(OutputDesc, TEXT("MetalFX.RCAS.Output"));
		Output.ViewRect = Input.ViewRect;
		Output.LoadAction = ERenderTargetLoadAction::EClear;
	}

	const FScreenPassTextureViewport InputViewport(Input);
	const FScreenPassTextureViewport OutputViewport(Output);
	const float Sharpness = FMath::Clamp(CVarMetalFXSharpness.GetValueOnRenderThread(), 0.0f, 1.0f);

	FRCASPixelShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FRCASPixelShader::FParameters>();
	PassParameters->InputTexture = Input.Texture;
	PassParameters->InputViewMin = Input.ViewRect.Min;
	PassParameters->InputViewMax = Input.ViewRect.Max;
	PassParameters->OutputViewMin = Output.ViewRect.Min;
	PassParameters->Sharpness = Sharpness;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, Output.LoadAction);

	TShaderMapRef<FRCASPixelShader> PixelShader(View.ShaderMap);
	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("MetalFX RCAS Sharpness=%.2f", Sharpness), View, OutputViewport, InputViewport, PixelShader, PassParameters);

	return FScreenPassTexture(Output);
}
} // namespace MetalFXSharpening

const TCHAR* FMetalFXSharpeningUpscaler::GetDebugName() const
{
	return TEXT("MetalFX RCAS");
}

ISpatialUpscaler* FMetalFXSharpeningUpscaler::Fork_GameThread(const FSceneViewFamily&) const
{
	return new FMetalFXSharpeningUpscaler();
}

FScreenPassTexture FMetalFXSharpeningUpscaler::AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const
{
	RDG_EVENT_SCOPE_STAT(GraphBuilder, MetalFXRCAS, "MetalFX RCAS");
	RDG_GPU_STAT_SCOPE(GraphBuilder, MetalFXRCAS);

	check(PassInputs.SceneColor.IsValid());
	check(PassInputs.Stage == EUpscaleStage::SecondaryToOutput);

	const FScreenPassTexture SharpeningInput = MetalFXSharpening::ScaleToFinalOutputIfNeeded(GraphBuilder, View, PassInputs);

	return MetalFXSharpening::AddRCASPass(GraphBuilder, View, SharpeningInput, PassInputs.OverrideOutput);
}
