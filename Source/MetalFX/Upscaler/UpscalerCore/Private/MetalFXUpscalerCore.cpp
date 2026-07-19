#include "MetalFXUpscalerCore.h"
#include "MetalFXCoreUtility.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIContext.h"
#include "MetalCommandBuffer.h"
#include "MetalRHIUtility.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>

extern "C" int32 MetalFXQuerySupportReason();
#endif

FMetalFXUpscalerCore::FMetalFXUpscalerCore() = default;

FMetalFXUpscalerCore::~FMetalFXUpscalerCore() = default;

void FMetalFXUpscalerCore::Initialize()
{
	if (!bIsInitialized)
	{
		bIsInitialized = true;
	}
}

bool FMetalFXUpscalerCore::ValidateCommonExtents(
	FIntPoint InputTextureExtent,
	FIntPoint InputContentExtent,
	FIntPoint OutputExtent) const
{
	if (!bIsInitialized)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Upscaler Core is not initialized."));
		return false;
	}

	const bool bHasValidExtents =
		InputTextureExtent.X > 0
		&& InputTextureExtent.Y > 0
		&& InputContentExtent.X > 0
		&& InputContentExtent.Y > 0
		&& OutputExtent.X > 0
		&& OutputExtent.Y > 0;

	if (!bHasValidExtents)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX received invalid texture extents. InputTexture=%dx%d, InputContent=%dx%d, Output=%dx%d"), InputTextureExtent.X, InputTextureExtent.Y, InputContentExtent.X, InputContentExtent.Y, OutputExtent.X, OutputExtent.Y);
	}

	return bHasValidExtents;
}

bool FMetalFXUpscalerCore::ValidateCommonRects(FIntRect InputRect, FIntRect OutputRect) const
{
	const bool bHasValidRects = !InputRect.IsEmpty() && !OutputRect.IsEmpty();
	if (!bHasValidRects)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX received an empty content rect. Input=%dx%d, Output=%dx%d"), InputRect.Width(), InputRect.Height(), OutputRect.Width(), OutputRect.Height());
	}

	return bHasValidRects;
}

void FMetalFXUpscalerCore::UpdateActiveDebugInfo(FIntRect InputRect, FIntRect OutputRect, float ScreenPercentage)
{
	FScopeLock Lock(&ActiveDebugInfoCS);

	ActiveDebugInfo.InputRect = InputRect;
	ActiveDebugInfo.OutputRect = OutputRect;
	ActiveDebugInfo.ScreenPercentage = ScreenPercentage;
	ActiveDebugInfo.bIsValid = true;
}

FMetalFXActiveDebugInfo FMetalFXUpscalerCore::GetActiveDebugInfo() const
{
	FScopeLock Lock(&ActiveDebugInfoCS);
	return ActiveDebugInfo;
}

#if METALFX_PLUGIN_ENABLED
FMetalFXTextureView FMetalFXUpscalerCore::CreateMetalFXTextureView(FRDGTextureRef Texture)
{
	FMetalFXTextureView Result;
	if (!Texture)
	{
		return Result;
	}

	FRHITexture* RHITexture = Texture->GetRHI();
	if (!RHITexture)
	{
		return Result;
	}

#if METALFX_METALCPP
	MTL::Texture* SourceTexture = static_cast<MTL::Texture*>(RHITexture->GetNativeResource());
	if (!SourceTexture)
	{
		return Result;
	}

	if (SourceTexture->textureType() == MTL::TextureType2D)
	{
		Result.SetTexture(SourceTexture, false);
		return Result;
	}

	if (SourceTexture->textureType() == MTL::TextureType2DArray)
	{
		MTL::Texture* TextureView = SourceTexture->newTextureView(SourceTexture->pixelFormat(), MTL::TextureType2D, NS::Range::Make(0, 1), NS::Range::Make(0, 1));

		Result.SetTexture(TextureView, TextureView != nullptr);
	}
#elif METALFX_NATIVE
	id<MTLTexture> SourceTexture = (__bridge id<MTLTexture>)RHITexture->GetNativeResource();
	if (SourceTexture == nil)
	{
		return Result;
	}

	if ([SourceTexture textureType] == MTLTextureType2D)
	{
		Result.SetTexture(SourceTexture, false);
		return Result;
	}

	if ([SourceTexture textureType] == MTLTextureType2DArray)
	{
		id<MTLTexture> TextureView = [SourceTexture newTextureViewWithPixelFormat:[SourceTexture pixelFormat] textureType:MTLTextureType2D levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)];

		Result.SetTexture(TextureView, TextureView != nil);
	}
#endif

	return Result;
}

FMetalCommandBuffer* FMetalFXUpscalerCore::GetCurrentMetalCommandBuffer(FRHICommandList& CmdList)
{
	return FMetalRHIUtility::GetCurrentCommandBufferFromCmdList(CmdList);
}

void* FMetalFXUpscalerCore::GetMetalDevice()
{
	return GDynamicRHI ? GDynamicRHI->RHIGetNativeDevice() : nullptr;
}
#endif

static EMetalFXSupportReason QueryMetalFXSupportReasonWithoutLogging()
{
#if METALFX_PLUGIN_ENABLED
	return static_cast<EMetalFXSupportReason>(MetalFXQuerySupportReason());
#else
	return EMetalFXSupportReason::NotSupported;
#endif
}

EMetalFXSupportReason FMetalFXUpscalerCore::GetIsSupportedDevice()
{
	const EMetalFXSupportReason SupportReason = IsMetalFXSupported()
		? EMetalFXSupportReason::Supported
		: QueryMetalFXSupportReasonWithoutLogging();

	switch (SupportReason)
	{
	case EMetalFXSupportReason::Supported:
		UE_LOG(LogRHI, Log, TEXT("MetalFX Supported Device."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupported:
		UE_LOG(LogRHI, Warning, TEXT("MetalFX is not supported in this environment."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedOldDeviceType:
		UE_LOG(LogRHI, Warning, TEXT("MetalFX is not supported on this device. The device is too old."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedOSVersionOutOfDate:
		UE_LOG(LogRHI, Warning, TEXT("MetalFX is not supported because the OS version is too old."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedMetalFXFrameworkMissing:
		UE_LOG(LogRHI, Warning, TEXT("MetalFX is not supported because the framework is missing."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedMetalFXCreationFailed:
		UE_LOG(LogRHI, Warning, TEXT("MetalFX is not supported because the scaler support check failed."));
		return SupportReason;
	}

	UE_LOG(LogRHI, Warning, TEXT("MetalFX is not supported in this environment."));
	return EMetalFXSupportReason::NotSupported;
}

bool FMetalFXUpscalerCore::IsMetalFXSupported()
{
	return GetMetalFXSupportedType() != EMetalFXSupportedType::None;
}

EMetalFXSupportedType FMetalFXUpscalerCore::GetMetalFXSupportedType()
{
#if METALFX_PLUGIN_ENABLED
	return ::GetMetalFXSupportedType();
#else
	return EMetalFXSupportedType::None;
#endif
}

EMetalFXSupportReason FMetalFXUpscalerCore::GetMetalFXSupportReason()
{
	return GetIsSupportedDevice();
}

bool FMetalFXUpscalerCore::IsUpscalerModeSupported(
	EMetalFXSupportedType SupportedTypes,
	EMetalFXUpscalerMode UpscalerMode)
{
	switch (UpscalerMode)
	{
	case EMetalFXUpscalerMode::Spatial:
		return EnumHasAnyFlags(SupportedTypes, EMetalFXSupportedType::Spatial);
	case EMetalFXUpscalerMode::Temporal:
		return EnumHasAnyFlags(SupportedTypes, EMetalFXSupportedType::Temporal);
	default:
		return false;
	}
}
