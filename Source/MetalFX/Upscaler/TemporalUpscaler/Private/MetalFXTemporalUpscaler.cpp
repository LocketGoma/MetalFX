#include "MetalFXTemporalUpscaler.h"
#include "MetalFXHelper.h"
#include "MetalFXSettings.h"

#if METALFX_PLUGIN_ENABLED
bool FMetalFXTemporalUpscaler::CheckValidate() const
{
	if (!UpscalerCore)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal Core is not ready."));
		return false;
	}
	return true;
}

float FMetalFXTemporalUpscaler::GetMinUpsampleResolutionFraction() const
{
	return GetMetalFXMinUpscaleResolutionFraction();
}

float FMetalFXTemporalUpscaler::GetMaxUpsampleResolutionFraction() const
{
	return GetMetalFXMaxUpscaleResolutionFraction();
}

FMetalFXTemporalUpscaler::FMetalFXTemporalUpscaler(FMetalFXTemporalUpscalerCore* InUpscaler)
	: UpscalerCore(InUpscaler)
{
}

ITemporalUpscaler* FMetalFXTemporalUpscaler::Fork_GameThread(const FSceneViewFamily& ViewFamily) const
{
	return new FMetalFXTemporalUpscaler(UpscalerCore);
}

//-------
#if METALFX_DEBUG
static void LogRDGTextureDescForMetalFX(const TCHAR* Label, FRDGTextureRef Texture)
{
	if (!Texture)
	{
		UE_LOG(LogMetalFX, VeryVerbose, TEXT("[MetalFX] %s: null"), Label);
		return;
	}

	UE_LOG(LogMetalFX, VeryVerbose, TEXT("[MetalFX] %s: Name=%s Extent=%dx%d Format=%d"), Label, Texture->Name, Texture->Desc.Extent.X, Texture->Desc.Extent.Y, static_cast<int32>(Texture->Desc.Format));
}

static void LogTemporalUpscalerInputsForMetalFX(const ITemporalUpscaler::FInputs& Inputs, FRDGTextureRef OutputTexture = nullptr)
{
	UE_LOG(LogMetalFX, VeryVerbose, TEXT("================ MetalFX Temporal Inputs ================"));

	LogRDGTextureDescForMetalFX(TEXT("SceneColor"), Inputs.SceneColor.Texture);
	LogRDGTextureDescForMetalFX(TEXT("SceneDepth"), Inputs.SceneDepth.Texture);
	LogRDGTextureDescForMetalFX(TEXT("SceneVelocity"), Inputs.SceneVelocity.Texture);

	if (OutputTexture)
	{
		LogRDGTextureDescForMetalFX(TEXT("OutputTexture"), OutputTexture);
	}
	else
	{
		UE_LOG(LogMetalFX, VeryVerbose, TEXT("[MetalFX] OutputTexture: null / not provided"));
	}

	UE_LOG(LogMetalFX, VeryVerbose, TEXT("[MetalFX] OutputViewRect: Min=(%d,%d) Max=(%d,%d) Size=%dx%d"), Inputs.OutputViewRect.Min.X, Inputs.OutputViewRect.Min.Y, Inputs.OutputViewRect.Max.X, Inputs.OutputViewRect.Max.Y, Inputs.OutputViewRect.Width(), Inputs.OutputViewRect.Height());

	if (Inputs.SceneColor.Texture)
	{
		const FIntPoint SceneColorExtent = Inputs.SceneColor.Texture->Desc.Extent;

		UE_LOG(LogMetalFX, VeryVerbose, TEXT("[MetalFX] SceneColorExtent vs OutputViewRectSize: SceneColor=%dx%d OutputViewRect=%dx%d Delta=%dx%d"), SceneColorExtent.X, SceneColorExtent.Y, Inputs.OutputViewRect.Width(), Inputs.OutputViewRect.Height(), SceneColorExtent.X - Inputs.OutputViewRect.Width(), SceneColorExtent.Y - Inputs.OutputViewRect.Height());
	}

	if (OutputTexture)
	{
		const FIntPoint OutputTextureExtent = OutputTexture->Desc.Extent;

		UE_LOG(LogMetalFX, VeryVerbose, TEXT("[MetalFX] OutputTextureExtent vs OutputViewRectSize: OutputTexture=%dx%d OutputViewRect=%dx%d Delta=%dx%d"), OutputTextureExtent.X, OutputTextureExtent.Y, Inputs.OutputViewRect.Width(), Inputs.OutputViewRect.Height(), OutputTextureExtent.X - Inputs.OutputViewRect.Width(), OutputTextureExtent.Y - Inputs.OutputViewRect.Height());
	}

	UE_LOG(LogMetalFX, VeryVerbose, TEXT("========================================================="));
}
#endif

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
	return FVector2f(CVarMetalFXMotionVectorScaleX.GetValueOnRenderThread(), CVarMetalFXMotionVectorScaleY.GetValueOnRenderThread());
}

static FIntPoint ResolveMetalFXDescriptorInputExtent(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent)
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
	ITemporalUpscaler::FOutputs Outputs;
	//1. Output 생성
	if (!CheckValidate())
	{
		return Outputs;
	}

	const bool bSceneColorValid = Inputs.SceneColor.Texture != nullptr;
	const bool bSceneDepthValid = Inputs.SceneDepth.Texture != nullptr;
	const bool bInputRectValid = !Inputs.SceneColor.ViewRect.IsEmpty();
	const bool bOutputRectValid = !Inputs.OutputViewRect.IsEmpty();
	const bool bSceneTexturesValid = bSceneColorValid && bSceneDepthValid;
	const bool bSceneRectsValid = bInputRectValid && bOutputRectValid;
	if (!bSceneTexturesValid || !bSceneRectsValid)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal received invalid color, depth, or output geometry."));
		return Outputs;
	}

	//2. Rect 변수 할당
	const FIntPoint InputTextureExtent = Inputs.SceneColor.Texture->Desc.Extent;
	const FIntPoint InputContentExtent = Inputs.SceneColor.ViewRect.Size();
	const FIntPoint OutputExtent = Inputs.OutputViewRect.Size();
	FIntPoint DescriptorInputExtent = ResolveMetalFXDescriptorInputExtent(InputTextureExtent, InputContentExtent, OutputExtent);
	const bool bDescriptorWidthFitsContent = DescriptorInputExtent.X <= InputContentExtent.X;
	const bool bDescriptorHeightFitsContent = DescriptorInputExtent.Y <= InputContentExtent.Y;
	if (!bDescriptorWidthFitsContent || !bDescriptorHeightFitsContent)
	{
		// MetalFX validates the actual content scale against the descriptor scale.
		// Constrained ViewRects can be slightly smaller than the texture allocation, so keep both sizes aligned in that case.
		DescriptorInputExtent = InputContentExtent;
	}
	const FIntRect InputContentRect = Inputs.SceneColor.ViewRect;
	const FVector2D JitterOffset = GetMetalFXJitterOffset(Inputs.TemporalJitterPixels);
	const FVector2f MotionVectorScale = GetMetalFXMotionVectorScale();

	//3. History 생성
	// MetalFX owns the actual temporal history internally. Unreal's custom
	// history object is retained as the continuity token for this view.
	const bool bHasPreviousHistory = Inputs.PrevHistory != nullptr;
	const bool bResetHistory = View.bCameraCut || !bHasPreviousHistory;
	const TRefCountPtr<ITemporalUpscaler::IHistory> InputCustomHistory = bHasPreviousHistory ? Inputs.PrevHistory : new FMetalFXHistory();

	//4. Velocity Texture 보정 & 생성
	// Prepare velocity and output resources.
	FRDGTextureRef GeneratedVelocityTexture = FMetalFXTemporalUpscalerCore::PrepareVelocityTexture(GraphBuilder, View, Inputs.SceneColor.Texture, Inputs.SceneDepth.Texture, Inputs.SceneVelocity.Texture, InputContentRect, Inputs.OutputViewRect, View.ViewMatrices.GetTemporalAAJitter());
	if (!GeneratedVelocityTexture)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal could not prepare a velocity texture."));
		return Outputs;
	}

	//5. Output Texture 생성
	FRDGTextureRef OutputTexture = FMetalFXUpscalerCore::CreateOutputTexture(GraphBuilder, Inputs.SceneColor.Texture, Inputs.OutputViewRect);
	if (!OutputTexture)
	{
		return Outputs;
	}

#if METALFX_DEBUG
	LogTemporalUpscalerInputsForMetalFX(Inputs, OutputTexture);
#endif

	auto* PassParams = GraphBuilder.AllocParameters<FMetalFXTemporalPassParameters>();
	PassParams->ColorTexture = Inputs.SceneColor.Texture;
	PassParams->DepthTexture = Inputs.SceneDepth.Texture;
	PassParams->VelocityTexture = GeneratedVelocityTexture;
	PassParams->OutputTexture = OutputTexture;

	FMetalFXTemporalUpscalerCore* const PassUpscalerCore = UpscalerCore;

	FMetalFXTemporalEncodeInputs EncodeInputs;
	EncodeInputs.DescriptorInputExtent = DescriptorInputExtent;
	EncodeInputs.InputContentExtent = InputContentExtent;
	EncodeInputs.OutputExtent = OutputExtent;
	EncodeInputs.InputRect = InputContentRect;
	EncodeInputs.OutputRect = Inputs.OutputViewRect;
	EncodeInputs.bResetHistory = bResetHistory;
	EncodeInputs.JitterOffset = JitterOffset;
	EncodeInputs.MotionVectorScale = MotionVectorScale;

	/* Note : MetalFX encodes directly into the active Metal command buffer rather than using an RDG shader dispatch.
	 * Keep this on the graphics/raster path, but skip RDG render-pass begin/end so MetalFX can encode outside an active
	 * render pass. Removing Raster/SkipRenderPass has caused MetalRHI command buffer failures.
	 */
	const ERDGPassFlags Flags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MetalFXTemporalUpscaler"), PassParams, Flags, [PassUpscalerCore, PassParams, EncodeInputs](FRHICommandListImmediate& RHICmdList)
		{
			if (!PassUpscalerCore)
			{
				return;
			}

			FMetalFXTemporalTextureGroup LocalTextureGroup;
			FMetalFXTemporalTextureFormatGroup Formats;
			if (!PassUpscalerCore->SetTexturesToGroup(*PassParams, LocalTextureGroup, Formats))
			{
				UE_LOG(LogMetalFX, Error, TEXT("MetalFX upscaler texture setup failed."));
				return;
			}

			//실행 가능할때만 Enqueue로 보냄.
			// Serialize mutable scaler configuration with the encode that consumes it.
			RHICmdList.EnqueueLambda([PassUpscalerCore, EncodeInputs, Formats, TextureGroup = MoveTemp(LocalTextureGroup)](FRHICommandListImmediate& Cmd) mutable
				{
					if (PassUpscalerCore->PrepareToEncode(EncodeInputs, Formats))
					{
						PassUpscalerCore->ExecuteMetalFX(Cmd, TextureGroup);
					}
				});
		});

	Outputs.FullRes.Texture = OutputTexture;
	Outputs.FullRes.ViewRect = Inputs.OutputViewRect;
	//To do : 정상적인 Custom History 사용 필요
	Outputs.NewHistory = InputCustomHistory;
	return Outputs;
}
#endif
