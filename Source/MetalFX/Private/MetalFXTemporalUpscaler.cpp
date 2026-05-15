#include "MetalFXTemporalUpscaler.h"

#include <memory>
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "MetalFXSettings.h"

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
//IMPLEMENT_GLOBAL_SHADER(FMetalFXConvertVelocityCS, "/Plugin/MetalFX/Private/PostProcessFFX_FSR2ConvertVelocity.usf", "MainCS", SF_Compute);

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

static void LogScreenPassTextureInfo(
	const TCHAR* Label,
	const FScreenPassTexture& ScreenPassTexture)
{
	FRDGTextureRef Texture = ScreenPassTexture.Texture;

	if (!Texture)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("[MetalFX] %s: Texture=null"), Label);
		return;
	}

	const FRDGTextureDesc& Desc = Texture->Desc;
	const FIntRect& ViewRect = ScreenPassTexture.ViewRect;

	UE_LOG(LogMetalFX, Warning,
		TEXT("[MetalFX] %s:"
			 " TexturePtr=%p"
			 " DescExtent=%dx%d"
			 " ViewRect=(%d,%d)-(%d,%d)"
			 " ViewSize=%dx%d"
			 " Format=%d"),
		Label,
		Texture,
		Desc.Extent.X,
		Desc.Extent.Y,
		ViewRect.Min.X,
		ViewRect.Min.Y,
		ViewRect.Max.X,
		ViewRect.Max.Y,
		ViewRect.Width(),
		ViewRect.Height(),
		static_cast<int32>(Desc.Format));
}

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
	
	
	//4. Output Texture 생성
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		OutputExtents,
		PF_FloatRGBA,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV
	);
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("MetalFXOutput"));

	FRDGTextureRef GeneratedVelocityTexture = FMetalFXUpscalerCore::PrepareVelocityTexture
	(GraphBuilder, View, Inputs.SceneColor.Texture, 
	 Inputs.SceneDepth.Texture, Inputs.SceneVelocity.Texture, 
	 InputRect, Inputs.OutputViewRect, View.ViewMatrices.GetTemporalAAJitter());
	
	
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

#if METALFX_DEBUG
	bool Result = true;
	
	uint64 ColorTexWidth	= static_cast<uint64>(TextureGroupParams.ColorTexture->Desc.Extent.X);
	uint64 ColorTexHeight	= static_cast<uint64>(TextureGroupParams.ColorTexture->Desc.Extent.Y);
	uint64 VeloTexWidth	= static_cast<uint64>(TextureGroupParams.VelocityTexture->Desc.Extent.X);
	uint64 VeloTexHeight	= static_cast<uint64>(TextureGroupParams.VelocityTexture->Desc.Extent.Y);

	Result = ((ColorTexWidth == VeloTexWidth) && (ColorTexHeight == VeloTexHeight));
	
	if (!Result)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("[MetalFX] Test 5 - TextureSize Mismatch! - Color: %llux%llu Motion: %llux%llu"), ColorTexWidth, ColorTexHeight, VeloTexWidth, VeloTexHeight);
	}
	
	static uint64 DebugFrameIndex = 0;
	const uint64 LocalFrameIndex = ++DebugFrameIndex;

	auto LogRDG = [LocalFrameIndex](const TCHAR* Label, FRDGTextureRef Color, FRDGTextureRef Velocity)
	{
		UE_LOG(LogMetalFX, Warning,
			TEXT("[MetalFX][Frame=%llu] %s - ColorPtr=%p Color=%dx%d / VelocityPtr=%p Velocity=%dx%d"),
			LocalFrameIndex,
			Label,
			Color,
			Color ? Color->Desc.Extent.X : 0,
			Color ? Color->Desc.Extent.Y : 0,
			Velocity,
			Velocity ? Velocity->Desc.Extent.X : 0,
			Velocity ? Velocity->Desc.Extent.Y : 0);
	};
	
	LogRDG(TEXT("Before AddPass / Inputs"),
		Inputs.SceneColor.Texture,
		Inputs.SceneVelocity.Texture);

	LogRDG(TEXT("Before AddPass / PassParams"),
		PassParams->ColorTexture.GetTexture(),
		PassParams->VelocityTexture.GetTexture());

	LogRDG(TEXT("Before AddPass / CapturedParams"),
		TextureGroupParams.ColorTexture,
		TextureGroupParams.VelocityTexture);
#endif
	FMetalFXUpscalerCore* UpscalerCore = m_FxUpscaler;
	
	ERDGPassFlags Flags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::Copy | ERDGPassFlags::NeverCull;
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MetalFXTemporalUpscaler"), PassParams, Flags, 
		[UpscalerCore, PassParams, InputExtents, OutputExtents, DispatchParams](FRHICommandListImmediate& RHICmdList)
		{
			if (!UpscalerCore)
			{
				return;
			}
			//To do : Dispatch Params도 이용해야됨.
			
			UpscalerCore->SetTexturesToGroup(*PassParams);
			
			//실행 가능할때만 Enqueue로 보냄.
			if (UpscalerCore->CheckForExecuteMetalFX(InputExtents, OutputExtents))
			{
				RHICmdList.EnqueueLambda([UpscalerCore, InputExtents, OutputExtents](FRHICommandListImmediate& Cmd) mutable
				{	
					if (UpscalerCore != nullptr)
					{
						UpscalerCore->ExecuteMetalFX(Cmd);
						
					}
				});
			}
		});
	
	*OutputCustomHistory = InputCustomHistory;

	GraphBuilder.QueueTextureExtraction(OutputTexture, &ReactiveExtractedTexture);

	Outputs.FullRes.Texture = OutputTexture;
	Outputs.FullRes.ViewRect = Inputs.OutputViewRect;
	Outputs.NewHistory = *OutputCustomHistory;
	return Outputs;
}
#endif
