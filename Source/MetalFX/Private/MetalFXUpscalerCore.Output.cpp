#include "MetalFXUpscalerCore.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

//Do something : Make OutputTex

FRDGTextureRef FMetalFXUpscalerCore::CreateOutputTexture(FRDGBuilder& GraphBuilder, const FRDGTextureRef InColorTexture, FIntRect OutputViewRect)
{
	if (!InColorTexture)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("[MetalFX] Cannot create output texture: SceneColor is null."));
		return nullptr;
	}

	const FRDGTextureDesc& SceneColorDesc = InColorTexture->Desc;

	FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
		OutputViewRect.Size(),
		SceneColorDesc.Format,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("MetalFXOutput"));

	return OutputTexture;
}
