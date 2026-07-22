#pragma once

#include "MetalFXHelper.h"

#if METALFX_PLUGIN_ENABLED
// MetalFX Upscaler utility functions.
// Returns one module-lifetime type: Temporal is preferred and Spatial is the fallback.
EMetalFXUpscalerType QuerySupportedMetalFXUpscalerType();
extern "C" int32 MetalFXQuerySupportReason(EMetalFXUpscalerType SupportedUpscalerType);

#if METALFX_NATIVE
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

// Temporal MetalFX system utility functions. The Metal-cpp path does not expose Objective-C types through this header.
extern "C" id<MTLFXTemporalScaler> MetalFXCreateTemporalUpscaler(id<MTLDevice> Device, const FMetalFXTemporalTextureFormatGroup& Formats, int32 InputWidth, int32 InputHeight, int32 OutputWidth, int32 OutputHeight);
extern "C" bool MetalFXUpdateScalerResolution(id<MTLFXTemporalScaler> Scaler, int32 InputWidth, int32 InputHeight);
extern "C" void MetalFXEncode(id<MTLFXTemporalScaler> Scaler, id<MTLCommandBuffer> CmdBuffer, id<MTLTexture> Color, id<MTLTexture> Depth, id<MTLTexture> Motion, id<MTLTexture> Output);
extern "C" void MetalFXSetJitterOffset(id<MTLFXTemporalScaler> Scaler, float OffsetX, float OffsetY);
extern "C" void MetalFXSetMotionVectorScale(id<MTLFXTemporalScaler> Scaler, float ScaleX, float ScaleY);
#endif
#endif
