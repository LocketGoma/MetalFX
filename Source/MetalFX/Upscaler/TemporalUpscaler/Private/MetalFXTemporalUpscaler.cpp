#include "MetalFXTemporalUpscaler.h"

#include <memory>
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "MetalFXSettings.h"

//아직 작업할거 많이 남았음...
//MetalFX를 위한 ComputeShader
//Permutation Domains
//Bool = 0 / 1 토글
//Enum Class = Enum 값 개수만큼
class FMetalFX_Quality : SHADER_PERMUTATION_ENUM_CLASS("METALFX_QUALITY", EMetalFXQualityMode);
class FMetalFX_Permutation : SHADER_PERMUTATION_BOOL("METALFX_BASE_PERMUTATION");
class FMetalFX_Exposure : SHADER_PERMUTATION_BOOL("METALFX_EXPOSURE_COPY");
class FMetalFX_Denoiser : SHADER_PERMUTATION_BOOL("USE_METALFX_DENOISER");

//To do : 원본 대비 Rect 비율 = 스케일 옵션 = "퀄러티" 차이 이므로, 해당 값을 Enum 값에 따라 변경되도록 해야 됨.
//기준은 DLSS의 수치 변경 따라가기로.

class FMetalFXConvertVelocityCS : public FGlobalShader
{
public:
	static const int ThreadgroupSizeX = 8;
	static const int ThreadgroupSizeY = 8;
	static const int ThreadgroupSizeZ = 1;

	DECLARE_GLOBAL_SHADER(FMetalFXConvertVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FMetalFXConvertVelocityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_TEXTURE_ACCESS(DepthTexture, ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputDepth)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputVelocity)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform) && IsMetalPlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadgroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadgroupSizeY);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), ThreadgroupSizeZ);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("UNREAL_ENGINE_MAJOR_VERSION"), ENGINE_MAJOR_VERSION);
		OutEnvironment.SetDefine(TEXT("UNREAL_ENGINE_MINOR_VERSION"), ENGINE_MINOR_VERSION);
	}
};

#if METALFX_PLUGIN_ENABLED

const void FMetalFXTemporalUpscaler::CheckValidate() const
{
	checkf(m_FxUpscaler, TEXT("You Trying To Activate MetalFX. but MetalFX Upscaler Not Ready. You Must Check MetalFX Upscaler Logics. see MetalFXTemporalUpscaler Class For More Infomations."));
}

float FMetalFXTemporalUpscaler::GetMinUpsampleResolutionFraction() const
{
	return m_FxUpscaler->GetMinUpsampleResolutionFraction();
}

float FMetalFXTemporalUpscaler::GetMaxUpsampleResolutionFraction() const
{
	return m_FxUpscaler->GetMaxUpsampleResolutionFraction();
}

FMetalFXTemporalUpscaler::FMetalFXTemporalUpscaler(FMetalFXUpscalerCore* InUpscaler)
{
	m_FxUpscaler = InUpscaler;
}

const bool FMetalFXTemporalUpscaler::GetIsSupportedDevice()
{
	if (m_FxUpscaler)
	{
		return (m_FxUpscaler->GetIsSupportedDevice() == EMetalFXSupportReason::Supported);
	}
	return false;
}

ITemporalUpscaler* FMetalFXTemporalUpscaler::Fork_GameThread(const class FSceneViewFamily& ViewFamily) const
{
	return new FMetalFXTemporalUpscaler(m_FxUpscaler);
}

//-------
#if METALFX_DEBUG
static void LogRDGTextureDescForMetalFX(const TCHAR* Label, FRDGTextureRef Texture)
{
	if (!Texture)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("[MetalFX] %s: null"), Label);
		return;
	}

	UE_LOG(LogMetalFX, Warning,
		TEXT("[MetalFX] %s: Name=%s Extent=%dx%d Format=%d"),
		Label,
		Texture->Name,
		Texture->Desc.Extent.X,
		Texture->Desc.Extent.Y,
		static_cast<int32>(Texture->Desc.Format));
}

static void LogTemporalUpscalerInputsForMetalFX(
	const ITemporalUpscaler::FInputs& Inputs,
	FRDGTextureRef OutputTexture = nullptr)
{
	UE_LOG(LogMetalFX, Warning, TEXT("================ MetalFX Temporal Inputs ================"));

	LogRDGTextureDescForMetalFX(TEXT("SceneColor"), Inputs.SceneColor.Texture);
	LogRDGTextureDescForMetalFX(TEXT("SceneDepth"), Inputs.SceneDepth.Texture);
	LogRDGTextureDescForMetalFX(TEXT("SceneVelocity"), Inputs.SceneVelocity.Texture);

	if (OutputTexture)
	{
		LogRDGTextureDescForMetalFX(TEXT("OutputTexture"), OutputTexture);
	}
	else
	{
		UE_LOG(LogMetalFX, Warning, TEXT("[MetalFX] OutputTexture: null / not provided"));
	}

	UE_LOG(LogMetalFX, Warning,
		TEXT("[MetalFX] OutputViewRect: Min=(%d,%d) Max=(%d,%d) Size=%dx%d"),
		Inputs.OutputViewRect.Min.X,
		Inputs.OutputViewRect.Min.Y,
		Inputs.OutputViewRect.Max.X,
		Inputs.OutputViewRect.Max.Y,
		Inputs.OutputViewRect.Width(),
		Inputs.OutputViewRect.Height());

	if (Inputs.SceneColor.Texture)
	{
		const FIntPoint SceneColorExtent = Inputs.SceneColor.Texture->Desc.Extent;

		UE_LOG(LogMetalFX, Warning,
			TEXT("[MetalFX] SceneColorExtent vs OutputViewRectSize: SceneColor=%dx%d OutputViewRect=%dx%d Delta=%dx%d"),
			SceneColorExtent.X,
			SceneColorExtent.Y,
			Inputs.OutputViewRect.Width(),
			Inputs.OutputViewRect.Height(),
			SceneColorExtent.X - Inputs.OutputViewRect.Width(),
			SceneColorExtent.Y - Inputs.OutputViewRect.Height());
	}

	if (OutputTexture)
	{
		const FIntPoint OutputTextureExtent = OutputTexture->Desc.Extent;

		UE_LOG(LogMetalFX, Warning,
			TEXT("[MetalFX] OutputTextureExtent vs OutputViewRectSize: OutputTexture=%dx%d OutputViewRect=%dx%d Delta=%dx%d"),
			OutputTextureExtent.X,
			OutputTextureExtent.Y,
			Inputs.OutputViewRect.Width(),
			Inputs.OutputViewRect.Height(),
			OutputTextureExtent.X - Inputs.OutputViewRect.Width(),
			OutputTextureExtent.Y - Inputs.OutputViewRect.Height());
	}

	UE_LOG(LogMetalFX, Warning, TEXT("========================================================="));
}
#endif

ITemporalUpscaler::FOutputs FMetalFXTemporalUpscaler::AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const ITemporalUpscaler::FInputs& Inputs) const
{
	CheckValidate();
	
	//1. Output 생성
	ITemporalUpscaler::FOutputs Outputs;

	//2. Rect 변수 할당	
	FIntPoint InputExtents = Inputs.SceneColor.Texture->Desc.Extent;
	FIntPoint OutputExtents = Inputs.OutputViewRect.Size();
	FIntRect InputRect(FIntPoint::ZeroValue, InputExtents);
	
	FMetalFXDispatchParameters DispatchParams;
	DispatchParams.JitterOffset = View.ViewMatrices.GetTemporalAAJitter();

	//3. Histroy 생성
	const TRefCountPtr<ITemporalUpscaler::IHistory> InputCustomHistory = Inputs.PrevHistory != nullptr ? Inputs.PrevHistory : new FMetalFXHistory();
	TRefCountPtr<ITemporalUpscaler::IHistory>* OutputCustomHistory = &Outputs.NewHistory;
		
	//4. Velocity Texture 보정 & 생성
	FRDGTextureRef GeneratedVelocityTexture = FMetalFXUpscalerCore::PrepareVelocityTexture
	(GraphBuilder, View, Inputs.SceneColor.Texture, 
	Inputs.SceneDepth.Texture, Inputs.SceneVelocity.Texture, 
	InputRect, Inputs.OutputViewRect, View.ViewMatrices.GetTemporalAAJitter());
	
	//5. Output Texture 생성
	FRDGTextureRef OutputTexture = FMetalFXUpscalerCore::CreateOutputTexture
	(GraphBuilder, Inputs.SceneColor.Texture, Inputs.OutputViewRect);
		
#if METALFX_DEBUG
	LogTemporalUpscalerInputsForMetalFX(Inputs, OutputTexture);
#endif
	
	auto* PassParams = GraphBuilder.AllocParameters<FMetalFXParameters>();
	PassParams->ColorTexture = Inputs.SceneColor.Texture;
	PassParams->DepthTexture = Inputs.SceneDepth.Texture;
	PassParams->VelocityTexture = GeneratedVelocityTexture;
	PassParams->OutputTexture = OutputTexture;
		
	FMetalFXTextureParameterGroup TextureGroupParams;
	TextureGroupParams.ColorTexture = Inputs.SceneColor.Texture;
	TextureGroupParams.DepthTexture = Inputs.SceneDepth.Texture;
	TextureGroupParams.VelocityTexture = GeneratedVelocityTexture;
	TextureGroupParams.OutputTexture = OutputTexture;

	FMetalFXUpscalerCore* UpscalerCore = m_FxUpscaler;
	
	//실험 필요
	ERDGPassFlags Flags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::Copy | ERDGPassFlags::NeverCull;
	
	GraphBuilder.AddPass(RDG_EVENT_NAME("MetalFXTemporalUpscaler"), PassParams, Flags, 
	[UpscalerCore, PassParams, InputExtents, OutputExtents, DispatchParams](FRHICommandListImmediate& RHICmdList)
	{
		if (!UpscalerCore)
		{
			return;
		}
		//To do : Use Dispatch Params (Jitter Offset, Motion Vector Scale, ETC..), Use Valid History
		FMetalFXTextureGroup LocalTextureGroup;
		UpscalerCore->SetTexturesToGroup(*PassParams, LocalTextureGroup);
		
		//실행 가능할때만 Enqueue로 보냄.
		if (UpscalerCore->CheckForExecuteMetalFX(InputExtents, OutputExtents))
		{
			RHICmdList.EnqueueLambda([UpscalerCore, TextureGroup = MoveTemp(LocalTextureGroup), InputExtents, OutputExtents](FRHICommandListImmediate& Cmd) mutable
			{	
				UpscalerCore->ExecuteMetalFX(Cmd, TextureGroup);
			});
		}
		else
		{
			LocalTextureGroup.ReleaseAllTexture();
		}
	});
	
	//To do : 정상적인 Custom History 사용 필요
	*OutputCustomHistory = InputCustomHistory;

	GraphBuilder.QueueTextureExtraction(OutputTexture, &ReactiveExtractedTexture);

	Outputs.FullRes.Texture = OutputTexture;
	Outputs.FullRes.ViewRect = Inputs.OutputViewRect;
	Outputs.NewHistory = *OutputCustomHistory;
	return Outputs;
}
#endif
