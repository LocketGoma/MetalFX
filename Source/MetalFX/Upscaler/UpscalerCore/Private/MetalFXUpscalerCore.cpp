#include "MetalFXUpscalerCore.h"
#include "MetalFXCoreUtility.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIContext.h"
#include "MetalCommandBuffer.h"
#include "MetalRHIUtility.h"
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

bool FMetalFXUpscalerCore::ValidateCommonExtents(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent) const
{
	if (!bIsInitialized)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Upscaler Core is not initialized."));
		return false;
	}

	const bool bInputTextureWidthValid = InputTextureExtent.X > 0;
	const bool bInputTextureHeightValid = InputTextureExtent.Y > 0;
	const bool bInputContentWidthValid = InputContentExtent.X > 0;
	const bool bInputContentHeightValid = InputContentExtent.Y > 0;
	const bool bOutputWidthValid = OutputExtent.X > 0;
	const bool bOutputHeightValid = OutputExtent.Y > 0;
	const bool bHasValidInputTextureExtent = bInputTextureWidthValid && bInputTextureHeightValid;
	const bool bHasValidInputContentExtent = bInputContentWidthValid && bInputContentHeightValid;
	const bool bHasValidOutputExtent = bOutputWidthValid && bOutputHeightValid;
	const bool bHasValidExtents = bHasValidInputTextureExtent && bHasValidInputContentExtent && bHasValidOutputExtent;

	if (!bHasValidExtents)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX received invalid texture extents. DescriptorInput=%dx%d, InputContent=%dx%d, Output=%dx%d"), InputTextureExtent.X, InputTextureExtent.Y, InputContentExtent.X, InputContentExtent.Y, OutputExtent.X, OutputExtent.Y);
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

void FMetalFXUpscalerCore::UpdateActiveDebugInfo(FIntRect InputRect, FIntRect OutputRect)
{
	FScopeLock Lock(&ActiveDebugInfoCS);

	ActiveDebugInfo.InputRect = InputRect;
	ActiveDebugInfo.OutputRect = OutputRect;
	ActiveDebugInfo.bIsValid = true;
}

void FMetalFXUpscalerCore::UpdateResolutionDebugInfo(const FMetalFXResolutionDebugInfo& ResolutionDebugInfo)
{
	FScopeLock Lock(&ActiveDebugInfoCS);
	ActiveDebugInfo.Resolution = ResolutionDebugInfo;
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
#endif
#if METALFX_NATIVE
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

static EMetalFXSupportReason QueryMetalFXSupportReasonWithoutLogging(EMetalFXUpscalerType SupportedUpscalerType)
{
#if METALFX_PLUGIN_ENABLED
	return static_cast<EMetalFXSupportReason>(MetalFXQuerySupportReason(SupportedUpscalerType));
#else
	return EMetalFXSupportReason::NotSupported;
#endif
}

EMetalFXSupportReason FMetalFXUpscalerCore::QuerySupportReason(EMetalFXUpscalerType SupportedUpscalerType)
{
	const bool bTemporalSupported = SupportedUpscalerType == EMetalFXUpscalerType::Temporal;
	const bool bSpatialSupported = SupportedUpscalerType == EMetalFXUpscalerType::Spatial;
	const bool bHasSupportedUpscaler = bTemporalSupported || bSpatialSupported;
	const EMetalFXSupportReason SupportReason = bHasSupportedUpscaler ? EMetalFXSupportReason::Supported : QueryMetalFXSupportReasonWithoutLogging(SupportedUpscalerType);

	switch (SupportReason)
	{
	case EMetalFXSupportReason::Supported:
		UE_LOG(LogMetalFX, Log, TEXT("MetalFX is supported on this device."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupported:
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is not supported in this environment."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedOldDeviceType:
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is not supported on this device. The device is too old."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedOSVersionOutOfDate:
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is not supported because the OS version is too old."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedMetalFXFrameworkMissing:
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is not supported because the framework is missing."));
		return SupportReason;
	case EMetalFXSupportReason::NotSupportedMetalFXCreationFailed:
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is not supported because the scaler support check failed."));
		return SupportReason;
	}

	UE_LOG(LogMetalFX, Warning, TEXT("MetalFX is not supported in this environment."));
	return EMetalFXSupportReason::NotSupported;
}

EMetalFXUpscalerType FMetalFXUpscalerCore::QuerySupportedUpscalerType()
{
#if METALFX_PLUGIN_ENABLED
	return QuerySupportedMetalFXUpscalerType();
#else
	return EMetalFXUpscalerType::None;
#endif
}
