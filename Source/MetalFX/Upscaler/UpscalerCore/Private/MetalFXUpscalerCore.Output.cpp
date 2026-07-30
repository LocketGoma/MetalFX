#include "MetalFXUpscalerCore.h"
#include "RenderGraphBuilder.h"

FRDGTextureRef FMetalFXUpscalerCore::CreateOutputTexture(FRDGBuilder& GraphBuilder, FRDGTextureRef InSceneColorTexture, FIntRect OutputViewRect)
{
	if (!InSceneColorTexture || OutputViewRect.IsEmpty())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX cannot create an output texture. SceneColorValid=%s OutputSize=%dx%d"), InSceneColorTexture ? TEXT("true") : TEXT("false"), OutputViewRect.Width(), OutputViewRect.Height());
		return nullptr;
	}

	const FRDGTextureDesc& SceneColorDesc = InSceneColorTexture->Desc;

	FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
		OutputViewRect.Size(),
		SceneColorDesc.Format,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	return GraphBuilder.CreateTexture(OutputDesc, TEXT("MetalFXOutput"));
}
