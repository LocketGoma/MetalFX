#pragma once

#if METALFX_PLUGIN_ENABLED
//Obj-C 쓰면 import - .h
//C++ Wrapper면 include - .hpp
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#ifdef __cplusplus
extern "C"
#endif
id<MTLFXTemporalScaler> MetalFXCreateTemporalUpscaler(id<MTLDevice> Device, int InputWidth, int InputHeight, int OutputWidth, int OutputHeight);

#ifdef __cplusplus
extern "C"
#endif
bool MetalFXUpdateScalerResolution(id<MTLFXTemporalScaler> Scaler, int InputWidth, int InputHeight);

#ifdef __cplusplus
extern "C"
#endif
void MetalFXEncode(id<MTLFXTemporalScaler> Scaler, id<MTLCommandBuffer> CmdBuffer, id<MTLTexture> Color, id<MTLTexture> Depth, id<MTLTexture> Motion, id<MTLTexture> Output, bool bReset);

#ifdef __cplusplus
extern "C"
#endif
void MetalFXSetJitterOffset(id<MTLFXTemporalScaler> Scaler, int OffsetX, int OffsetY);

#ifdef __cplusplus
extern "C"
#endif
void MetalFXSetMotionVectorScale(id<MTLFXTemporalScaler> Scaler, int OffsetX, int OffsetY);

//MetalFX Upscaler 외의 유틸함수
#ifdef __cplusplus
extern "C"
#endif
int32 MetalFXQuerySupportReason();

static BOOL IsSystemVersionAtLeast(NSString* MinVersion);

#endif //METALFX_PLUGIN_ENABLED