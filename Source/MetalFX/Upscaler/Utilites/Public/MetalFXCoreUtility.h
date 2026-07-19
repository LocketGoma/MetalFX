#pragma once

#include "MetalFXHelper.h"

#if METALFX_PLUGIN_ENABLED
//Obj-C 쓰면 import - .h
//C++ Wrapper면 include - .hpp
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

//MetalFX Upscaler 외의 유틸함수
EMetalFXSupportedType GetMetalFXSupportedType();
EMetalFXSupportReason GetMetalFXSupportReason();
bool IsMetalFXUpscalerModeSupported(
	EMetalFXSupportedType SupportedTypes,
	EMetalFXUpscalerMode UpscalerMode);

//Temporal MetalFX System Utility Functions
extern "C" id<MTLFXTemporalScaler> MetalFXCreateTemporalUpscaler(
	id<MTLDevice> Device,
	const FMetalFXTemporalTextureFormatGroup Formats,
	int InputWidth,
	int InputHeight,
	int OutputWidth,
	int OutputHeight);

extern "C" bool MetalFXUpdateScalerResolution(
	id<MTLFXTemporalScaler> Scaler,
	int InputWidth,
	int InputHeight);

extern "C" void MetalFXEncode(
	id<MTLFXTemporalScaler> Scaler,
	id<MTLCommandBuffer> CmdBuffer,
	id<MTLTexture> Color,
	id<MTLTexture> Depth,
	id<MTLTexture> Motion,
	id<MTLTexture> Output);

extern "C" void MetalFXSetJitterOffset(
	id<MTLFXTemporalScaler> Scaler,
	float OffsetX,
	float OffsetY);

extern "C" void MetalFXSetMotionVectorScale(
	id<MTLFXTemporalScaler> Scaler,
	float ScaleX,
	float ScaleY);
#endif
