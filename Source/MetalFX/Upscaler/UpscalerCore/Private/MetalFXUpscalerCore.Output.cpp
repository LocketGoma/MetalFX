#include "MetalFXUpscalerCore.h"
#include "RenderGraphBuilder.h"

FRDGTextureRef FMetalFXUpscalerCore::CreateOutputTexture(FRDGBuilder& GraphBuilder, FRDGTextureRef InColorTexture, FIntRect OutputViewRect)
{
	if (!InColorTexture || OutputViewRect.IsEmpty())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX cannot create an output texture. SceneColorValid=%s OutputSize=%dx%d"), InColorTexture ? TEXT("true") : TEXT("false"), OutputViewRect.Width(), OutputViewRect.Height());
		return nullptr;
	}

	const FRDGTextureDesc& SceneColorDesc = InColorTexture->Desc;

	FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
		OutputViewRect.Size(),
		SceneColorDesc.Format,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	return GraphBuilder.CreateTexture(OutputDesc, TEXT("MetalFXOutput"));
}
