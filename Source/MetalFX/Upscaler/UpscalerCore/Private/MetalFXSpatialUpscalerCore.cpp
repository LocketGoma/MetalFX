#include "MetalFXSpatialUpscalerCore.h"

#if METALFX_PLUGIN_ENABLED
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>
#if METALFX_NATIVE
#import <MetalFX/MetalFX.h>
#endif
#endif

struct FMetalFXSpatialCoreResources
{
#if METALFX_PLUGIN_ENABLED
#if METALFX_METALCPP
	NS::SharedPtr<MTLFX::SpatialScaler> CppScaler;
#elif METALFX_NATIVE
	id<MTLFXSpatialScaler> Scaler = nil;
#endif
#endif
};

FMetalFXSpatialUpscalerCore::FMetalFXSpatialUpscalerCore()
{
#if METALFX_PLUGIN_ENABLED
	Resources = std::make_unique<FMetalFXSpatialCoreResources>();
#endif
}

FMetalFXSpatialUpscalerCore::~FMetalFXSpatialUpscalerCore()
{
#if METALFX_PLUGIN_ENABLED
	if (Resources)
	{
#if METALFX_METALCPP
		Resources->CppScaler.reset();
#elif METALFX_NATIVE
		[Resources->Scaler release];
		Resources->Scaler = nil;
#endif
	}
#endif
}

#if METALFX_PLUGIN_ENABLED
bool FMetalFXSpatialUpscalerCore::SetTexturesToGroup(
	const FMetalFXSpatialPassParameters& Parameters,
	FMetalFXSpatialTextureGroup& OutTextureGroup)
{
	if (!Parameters.ColorTexture || !Parameters.OutputTexture)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX spatial texture setup failed because color or output is invalid."));
		return false;
	}

	OutTextureGroup.ColorTexture = CreateMetalFXTextureView(Parameters.ColorTexture);
	OutTextureGroup.OutputTexture = CreateMetalFXTextureView(Parameters.OutputTexture);

	if (!OutTextureGroup.ColorTexture.IsValid() || !OutTextureGroup.OutputTexture.IsValid())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX spatial texture view conversion failed."));
		return false;
	}

	// WIP: descriptor creation and SpatialScaler encode are intentionally not
	// implemented until the Unreal spatial integration path is validated.
	return true;
}
#endif
