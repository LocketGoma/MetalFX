#include "MetalFXSpatialUpscaler.h"
#include "MetalFXSpatialUpscalerCore.h"
#include "MetalFXSettings.h"
#include "ScenePrivate.h"

#if METALFX_PLUGIN_ENABLED
void FMetalFXSpatialUpscaler::CheckValidate() const
{
	checkf(m_FxUpscaler, TEXT("MetalFX Spatial Core is not ready."));
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
	CheckValidate();

	if (!PassInputs.SceneColor.IsValid() || PassInputs.SceneColor.ViewRect.Min != FIntPoint::ZeroValue || View.UnscaledViewRect.Min != FIntPoint::ZeroValue)
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
		Output.Texture = FMetalFXUpscalerCore::CreateOutputTexture(
			GraphBuilder,
			PassInputs.SceneColor.Texture,
			View.UnscaledViewRect);
		Output.ViewRect = View.UnscaledViewRect;
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
	EncodeInputs.OutputExtent = Output.Texture->Desc.Extent;
	EncodeInputs.InputRect = PassInputs.SceneColor.ViewRect;
	EncodeInputs.OutputRect = Output.ViewRect;
	if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		EncodeInputs.ScreenPercentage = ScreenPercentage->GetFloat();
	}

	FMetalFXSpatialUpscalerCore* UpscalerCore = m_FxUpscaler;
	const ERDGPassFlags Flags =
		ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull;

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
			if (!UpscalerCore->SetTexturesToGroup(*PassParameters, TextureGroup))
			{
				return;
			}

			if (UpscalerCore->PrepareToEncode(EncodeInputs))
			{
				RHICmdList.EnqueueLambda(
					[UpscalerCore, TextureGroup = MoveTemp(TextureGroup)](FRHICommandListImmediate& Cmd) mutable
					{
						UpscalerCore->ExecuteMetalFX(Cmd, TextureGroup);
					});
			}
		});

	return Output;
}
#endif
