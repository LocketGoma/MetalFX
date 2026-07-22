#include "MetalFXSpatialUpscaler.h"
#include "MetalFXSpatialUpscalerCore.h"
#include "MetalFXHelper.h"
#include "MetalFXSettings.h"
#include "ScenePrivate.h"

#if METALFX_PLUGIN_ENABLED
DECLARE_GPU_STAT(MetalFXSpatialUpscaler);

bool FMetalFXSpatialUpscaler::CheckValidate() const
{
	if (!m_FxUpscaler)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Spatial Core is not ready."));
		return false;
	}
	return true;
}

FMetalFXSpatialUpscaler::FMetalFXSpatialUpscaler(FMetalFXSpatialUpscalerCore* InUpscaler)
	: m_FxUpscaler(InUpscaler)
{
}

ISpatialUpscaler* FMetalFXSpatialUpscaler::Fork_GameThread(const FSceneViewFamily& ViewFamily) const
{
	return new FMetalFXSpatialUpscaler(m_FxUpscaler);
}

FScreenPassTexture FMetalFXSpatialUpscaler::AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const
{
	RDG_EVENT_SCOPE_STAT(GraphBuilder, MetalFXSpatialUpscaler, "MetalFXSpatialUpscaler");
	RDG_GPU_STAT_SCOPE(GraphBuilder, MetalFXSpatialUpscaler);

	if (!PassInputs.SceneColor.IsValid())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Spatial received an invalid SceneColor."));
		return FScreenPassTexture();
	}

	if (!CheckValidate())
	{
		return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, PassInputs, EUpscaleMethod::Bilinear);
	}

	if (PassInputs.Stage != EUpscaleStage::PrimaryToSecondary && PassInputs.Stage != EUpscaleStage::PrimaryToOutput)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial received an unsupported upscale stage; using bilinear fallback."));
		return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, PassInputs, EUpscaleMethod::Bilinear);
	}

	FIntRect OutputRect;
	if (PassInputs.OverrideOutput.IsValid())
	{
		OutputRect = PassInputs.OverrideOutput.ViewRect;
	}
	else if (PassInputs.Stage == EUpscaleStage::PrimaryToSecondary)
	{
		OutputRect = FIntRect(FIntPoint::ZeroValue, View.GetSecondaryViewRectSize());
	}
	else
	{
		OutputRect = View.UnscaledViewRect;
	}

	if (PassInputs.SceneColor.ViewRect.Min != FIntPoint::ZeroValue || OutputRect.Min != FIntPoint::ZeroValue)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial requires valid origin-aligned input and output rects; using bilinear fallback."));
		return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, PassInputs, EUpscaleMethod::Bilinear);
	}

	FScreenPassTexture Output;
	if (PassInputs.OverrideOutput.IsValid())
	{
		Output = PassInputs.OverrideOutput;
	}
	else
	{
		Output.Texture = FMetalFXUpscalerCore::CreateOutputTexture(GraphBuilder, PassInputs.SceneColor.Texture, OutputRect);
		Output.ViewRect = OutputRect;
	}

	if (!Output.IsValid())
	{
		return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, PassInputs, EUpscaleMethod::Bilinear);
	}

	if (Output.Texture->Desc.Extent != Output.ViewRect.Size())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial cannot encode into an oversized override output; using bilinear fallback."));
		return ISpatialUpscaler::AddDefaultUpscalePass(GraphBuilder, View, PassInputs, EUpscaleMethod::Bilinear);
	}

	auto* PassParameters = GraphBuilder.AllocParameters<FMetalFXSpatialPassParameters>();
	PassParameters->ColorTexture = PassInputs.SceneColor.Texture;
	PassParameters->OutputTexture = Output.Texture;

	FMetalFXSpatialEncodeInputs EncodeInputs;
	EncodeInputs.InputTextureExtent = PassInputs.SceneColor.Texture->Desc.Extent;
	EncodeInputs.InputContentExtent = PassInputs.SceneColor.ViewRect.Size();
	EncodeInputs.OutputExtent = Output.ViewRect.Size();
	EncodeInputs.InputRect = PassInputs.SceneColor.ViewRect;
	EncodeInputs.OutputRect = Output.ViewRect;
	EncodeInputs.ScreenPercentage = CalculateMetalFXScreenPercentage(EncodeInputs.InputRect, EncodeInputs.OutputRect);

	FMetalFXSpatialUpscalerCore* UpscalerCore = m_FxUpscaler;
	const ERDGPassFlags Flags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MetalFXSpatialUpscaler %dx%d -> %dx%d",
			EncodeInputs.InputContentExtent.X,
			EncodeInputs.InputContentExtent.Y,
			EncodeInputs.OutputRect.Width(),
			EncodeInputs.OutputRect.Height()),
		PassParameters,
		Flags,
		[UpscalerCore, PassParameters, EncodeInputs](FRHICommandListImmediate& RHICmdList)
		{
			FMetalFXSpatialTextureGroup TextureGroup;
			FMetalFXSpatialTextureFormatGroup Formats;
			if (!UpscalerCore->SetTexturesToGroup(*PassParameters, TextureGroup, Formats))
			{
				return;
			}

			RHICmdList.EnqueueLambda([UpscalerCore, EncodeInputs, Formats, TextureGroup = MoveTemp(TextureGroup)](FRHICommandListImmediate& Cmd) mutable
			{
				if (UpscalerCore->PrepareToEncode(EncodeInputs, Formats))
				{
					UpscalerCore->ExecuteMetalFX(Cmd, TextureGroup);
				}
			});
		});

	return Output;
}
#endif
