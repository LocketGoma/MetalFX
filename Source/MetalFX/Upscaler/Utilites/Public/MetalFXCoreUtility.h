#pragma once

#include "MetalFXHelper.h"

#if METALFX_PLUGIN_ENABLED
// MetalFX Upscaler utility functions.
// Returns one module-lifetime type: Temporal is preferred and Spatial is the fallback.
EMetalFXUpscalerType QuerySupportedMetalFXUpscalerType();
extern "C" int32 MetalFXQuerySupportReason(EMetalFXUpscalerType SupportedUpscalerType);

#if METALFX_NATIVE
// Native TemporalScaler의 Objective-C 구현은 .mm 파일에 유지하고, Core에서는 이 경계를 통해 호출한다.
extern "C" id<MTLFXTemporalScaler> MetalFXCreateTemporalUpscaler(id<MTLDevice> Device, const FMetalFXTemporalTextureFormatGroup& Formats, int32 InputWidth, int32 InputHeight, int32 OutputWidth, int32 OutputHeight);
extern "C" void MetalFXUpdateScalerResolution(id<MTLFXTemporalScaler> Scaler, int32 InputWidth, int32 InputHeight);
extern "C" void MetalFXEncode(id<MTLFXTemporalScaler> Scaler, id<MTLCommandBuffer> CmdBuffer, id<MTLTexture> Color, id<MTLTexture> Depth, id<MTLTexture> Motion, id<MTLTexture> Output);
extern "C" void MetalFXSetReset(id<MTLFXTemporalScaler> Scaler, bool bReset);
extern "C" void MetalFXSetDepthReversed(id<MTLFXTemporalScaler> Scaler, bool bDepthReversed);
extern "C" void MetalFXSetPreExposure(id<MTLFXTemporalScaler> Scaler, float PreExposure);
extern "C" void MetalFXSetJitterOffset(id<MTLFXTemporalScaler> Scaler, float OffsetX, float OffsetY);
extern "C" void MetalFXSetMotionVectorScale(id<MTLFXTemporalScaler> Scaler, float ScaleX, float ScaleY);
#endif
#endif
