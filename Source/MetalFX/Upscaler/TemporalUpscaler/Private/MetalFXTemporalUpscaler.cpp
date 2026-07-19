#include "MetalFXTemporalUpscaler.h"

#include "MetalFXSettings.h"

#if METALFX_PLUGIN_ENABLED
const void FMetalFXTemporalUpscaler::CheckValidate() const
{
	checkf(m_FxUpscaler, TEXT("MetalFX Upscaler is not ready. Check MetalFXTemporalUpscaler for more information."));
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

static float GetMetalFXScreenPercentageValue()
{
	IConsoleVariable* CVarScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	return CVarScreenPercentage ? CVarScreenPercentage->GetFloat() : 0.0f;
}

static FVector2D GetMetalFXJitterOffset(FVector2f TemporalJitterPixels)
{
	const FVector2D JitterOffset(TemporalJitterPixels);
	const int32 JitterMode = CVarMetalFXJitterMode.GetValueOnRenderThread();

	if (JitterMode == 0)
	{
		return FVector2D::ZeroVector;
	}

	return JitterMode < 0 ? -JitterOffset : JitterOffset;
}

static FVector2f GetMetalFXMotionVectorScale()
{
	return FVector2f(
		CVarMetalFXMotionVectorScaleX.GetValueOnRenderThread(),
		CVarMetalFXMotionVectorScaleY.GetValueOnRenderThread());
}

static FIntPoint GetMetalFXExperimentalInputExtent(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent)
{
	switch (CVarMetalFXExperimentalInputExtentMode.GetValueOnRenderThread())
	{
	case 1:
		return InputTextureExtent;
	case 2:
		return OutputExtent;
	default:
		return InputContentExtent;
	}
}

ITemporalUpscaler::FOutputs FMetalFXTemporalUpscaler::AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const ITemporalUpscaler::FInputs& Inputs) const
{
	CheckValidate();
	
	//1. Output 생성
	ITemporalUpscaler::FOutputs Outputs;

	//2. Rect 변수 할당	
	FIntPoint InputTextureExtent = Inputs.SceneColor.Texture->Desc.Extent;
	FIntPoint InputContentExtent = Inputs.SceneColor.ViewRect.Size();
	FIntPoint OutputExtents = Inputs.OutputViewRect.Size();
	FIntPoint DescriptorInputExtent = GetMetalFXExperimentalInputExtent(InputTextureExtent, InputContentExtent, OutputExtents);
	if (DescriptorInputExtent.X > InputContentExtent.X || DescriptorInputExtent.Y > InputContentExtent.Y)
	{
		// MetalFX validates the actual content scale against the descriptor scale.
		// Constrained ViewRects can be slightly smaller than the texture allocation, so keep both sizes aligned in that case.
		DescriptorInputExtent = InputContentExtent;
	}
	FIntRect InputContentRect = Inputs.SceneColor.ViewRect;
	const float ScreenPercentage = GetMetalFXScreenPercentageValue();
	const FVector2D JitterOffset = GetMetalFXJitterOffset(Inputs.TemporalJitterPixels);
	const FVector2f MotionVectorScale = GetMetalFXMotionVectorScale();

	//3. History 생성
	const TRefCountPtr<ITemporalUpscaler::IHistory> InputCustomHistory = Inputs.PrevHistory != nullptr ? Inputs.PrevHistory : new FMetalFXHistory();
	TRefCountPtr<ITemporalUpscaler::IHistory>* OutputCustomHistory = &Outputs.NewHistory;
		
	//4. Velocity Texture 보정 & 생성
	FRDGTextureRef GeneratedVelocityTexture = FMetalFXUpscalerCore::PrepareVelocityTexture
	(GraphBuilder, View, Inputs.SceneColor.Texture, 
	Inputs.SceneDepth.Texture, Inputs.SceneVelocity.Texture, 
	InputContentRect, Inputs.OutputViewRect, View.ViewMatrices.GetTemporalAAJitter());
	
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

	FMetalFXUpscalerCore* UpscalerCore = m_FxUpscaler;
	
	/* Note : MetalFX encodes directly into the active Metal command buffer rather than using an RDG shader dispatch.
	 * Keep this on the graphics/raster path, but skip RDG render-pass begin/end so MetalFX can encode outside an active
	 *  render pass. Removing Raster/SkipRenderPass has caused MetalRHI command buffer failures.
	 */
	ERDGPassFlags Flags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull;
	
	GraphBuilder.AddPass(RDG_EVENT_NAME("MetalFXTemporalUpscaler"), PassParams, Flags, 
	[UpscalerCore, PassParams, DescriptorInputExtent, InputContentExtent, InputContentRect, OutputExtents, OutputViewRect = Inputs.OutputViewRect, ScreenPercentage, JitterOffset, MotionVectorScale](FRHICommandListImmediate& RHICmdList)
	{
		if (!UpscalerCore)
		{
			return;
		}

		FMetalFXTextureGroup LocalTextureGroup;
		if (!UpscalerCore->SetTexturesToGroup(*PassParams, LocalTextureGroup))
		{
			UE_LOG(LogMetalFX, Error, TEXT("MetalFX upscaler texture setup failed."));
			return;
		}
		
		//실행 가능할때만 Enqueue로 보냄.
		if (UpscalerCore->CheckForExecuteMetalFX(DescriptorInputExtent, InputContentExtent, OutputExtents))
		{
			UpscalerCore->SetJitterOffset(JitterOffset);
			UpscalerCore->SetMotionVectorScale(MotionVectorScale);
			UpscalerCore->UpdateActiveDebugInfo(InputContentRect, OutputViewRect, ScreenPercentage);

			RHICmdList.EnqueueLambda([UpscalerCore, TextureGroup = MoveTemp(LocalTextureGroup)](FRHICommandListImmediate& Cmd) mutable
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
