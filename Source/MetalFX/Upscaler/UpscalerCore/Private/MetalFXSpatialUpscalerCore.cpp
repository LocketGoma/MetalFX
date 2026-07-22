#include "MetalFXSpatialUpscalerCore.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalCommandBuffer.h"
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>
#if METALFX_NATIVE
#import <MetalFX/MetalFX.h>
#endif
#endif

//////////////////////////////////////////////////////////////////////////
// Native SpatialScaler resources
//////////////////////////////////////////////////////////////////////////

struct FMetalFXSpatialCoreResources
{
#if METALFX_PLUGIN_ENABLED
	bool HasScaler() const
	{
#if METALFX_METALCPP
		return CppScaler.get() != nullptr;
#elif METALFX_NATIVE
		return Scaler != nil;
#else
		return false;
#endif
	}

#if METALFX_METALCPP
	NS::SharedPtr<MTLFX::SpatialScaler> CppScaler;
#elif METALFX_NATIVE
	id<MTLFXSpatialScaler> Scaler = nil;
#endif
#endif
	FMetalFXSpatialTextureFormatGroup Formats;
};

//////////////////////////////////////////////////////////////////////////
// Core lifetime
//////////////////////////////////////////////////////////////////////////

FMetalFXSpatialUpscalerCore::FMetalFXSpatialUpscalerCore()
{
#if METALFX_PLUGIN_ENABLED
	Resources = std::make_unique<FMetalFXSpatialCoreResources>();
#endif
}

FMetalFXSpatialUpscalerCore::~FMetalFXSpatialUpscalerCore()
{
#if METALFX_PLUGIN_ENABLED
	ResetUpscaler();
#endif
}

#if METALFX_PLUGIN_ENABLED
void FMetalFXSpatialUpscalerCore::ResetUpscaler()
{
	if (!Resources)
	{
		return;
	}

#if METALFX_METALCPP
	Resources->CppScaler.reset();
#elif METALFX_NATIVE
	[Resources->Scaler release];
	Resources->Scaler = nil;
#endif
}

//////////////////////////////////////////////////////////////////////////
// RDG to Metal texture conversion
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::SetTexturesToGroup(const FMetalFXSpatialPassParameters& Parameters, FMetalFXSpatialTextureGroup& OutTextureGroup, FMetalFXSpatialTextureFormatGroup& OutFormats)
{
	if (!Parameters.ColorTexture || !Parameters.OutputTexture)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX spatial texture setup failed because color or output is invalid."));
		return false;
	}

	OutTextureGroup.ColorTexture = CreateMetalFXTextureView(Parameters.ColorTexture);
	OutTextureGroup.OutputTexture = CreateMetalFXTextureView(Parameters.OutputTexture);

	const bool bColorTextureViewValid = OutTextureGroup.ColorTexture.IsValid();
	const bool bOutputTextureViewValid = OutTextureGroup.OutputTexture.IsValid();
	if (!bColorTextureViewValid || !bOutputTextureViewValid)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX spatial texture view conversion failed."));
		return false;
	}

#if METALFX_METALCPP
	OutFormats.Color = OutTextureGroup.ColorTexture.GetTexture()->pixelFormat();
	OutFormats.Output = OutTextureGroup.OutputTexture.GetTexture()->pixelFormat();
#elif METALFX_NATIVE
	OutFormats.Color = static_cast<FMetalFXPixelFormat>([OutTextureGroup.ColorTexture.GetTexture() pixelFormat]);
	OutFormats.Output = static_cast<FMetalFXPixelFormat>([OutTextureGroup.OutputTexture.GetTexture() pixelFormat]);
#endif
	if (!OutFormats.IsReady())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX spatial texture formats are invalid."));
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
// SpatialScaler descriptor creation
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::GenerateUpscaler()
{
	if (!Resources || !Resources->Formats.IsReady())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX SpatialScaler creation failed because descriptor resources or texture formats are invalid."));
		return false;
	}
	ResetUpscaler();

#if METALFX_METALCPP
	MTL::Device* Device = static_cast<MTL::Device*>(GetMetalDevice());
	if (!Device)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX SpatialScaler creation failed because the Metal device is unavailable."));
		return false;
	}

	auto Descriptor = NS::TransferPtr(MTLFX::SpatialScalerDescriptor::alloc()->init());
	if (!Descriptor.get())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX SpatialScaler descriptor creation failed."));
		return false;
	}

	if (!MTLFX::SpatialScalerDescriptor::supportsDevice(Device))
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX SpatialScaler is not supported by the current Metal device."));
		return false;
	}

	Descriptor->setInputWidth(ConfiguredDescriptorInputExtent.X);
	Descriptor->setInputHeight(ConfiguredDescriptorInputExtent.Y);
	Descriptor->setOutputWidth(ConfiguredOutputExtent.X);
	Descriptor->setOutputHeight(ConfiguredOutputExtent.Y);
	Descriptor->setColorTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Color));
	Descriptor->setOutputTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Output));
	// TODO: Automatically select Perceptual, Linear, or HDR from Unreal's output color-space configuration. Until then, keep MetalFX's default Perceptual mode.
	Resources->CppScaler = NS::TransferPtr(Descriptor->newSpatialScaler(Device));
#elif METALFX_NATIVE
	id<MTLDevice> Device = (__bridge id<MTLDevice>)GetMetalDevice();
	const bool bDeviceValid = Device != nil;
	const bool bDeviceSupported = bDeviceValid && [MTLFXSpatialScalerDescriptor supportsDevice:Device];
	if (!bDeviceSupported)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX SpatialScaler is not supported by the current Metal device."));
		return false;
	}

	MTLFXSpatialScalerDescriptor* Descriptor = [MTLFXSpatialScalerDescriptor new];
	Descriptor.inputWidth = ConfiguredDescriptorInputExtent.X;
	Descriptor.inputHeight = ConfiguredDescriptorInputExtent.Y;
	Descriptor.outputWidth = ConfiguredOutputExtent.X;
	Descriptor.outputHeight = ConfiguredOutputExtent.Y;
	Descriptor.colorTextureFormat = static_cast<MTLPixelFormat>(Resources->Formats.Color);
	Descriptor.outputTextureFormat = static_cast<MTLPixelFormat>(Resources->Formats.Output);
	// TODO: Automatically select Perceptual, Linear, or HDR from Unreal's output color-space configuration. Until then, keep MetalFX's default Perceptual mode.
	Resources->Scaler = [Descriptor newSpatialScalerWithDevice:Device];
	[Descriptor release];
#endif

	if (!Resources->HasScaler())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX SpatialScaler creation failed."));
		return false;
	}

	UpdateInputContentSize(ConfiguredInputContentExtent);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Mutable input-content resolution
//////////////////////////////////////////////////////////////////////////

void FMetalFXSpatialUpscalerCore::UpdateInputContentSize(FIntPoint InputContentExtent)
{
	if (!Resources || !Resources->HasScaler())
	{
		return;
	}

	ConfiguredInputContentExtent = InputContentExtent;
#if METALFX_METALCPP
	Resources->CppScaler->setInputContentWidth(ConfiguredInputContentExtent.X);
	Resources->CppScaler->setInputContentHeight(ConfiguredInputContentExtent.Y);
#elif METALFX_NATIVE
	Resources->Scaler.inputContentWidth = ConfiguredInputContentExtent.X;
	Resources->Scaler.inputContentHeight = ConfiguredInputContentExtent.Y;
#endif
}

//////////////////////////////////////////////////////////////////////////
// Scaler configuration
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::EnsureUpscalerForConfiguration(const FMetalFXSpatialEncodeInputs& Inputs, const FMetalFXSpatialTextureFormatGroup& Formats)
{
	if (!Resources)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Spatial Core resources are not available."));
		return false;
	}

	if (!ValidateCommonExtents(Inputs.DescriptorInputExtent, Inputs.InputContentExtent, Inputs.OutputExtent))
	{
		return false;
	}

	if (!Formats.IsReady())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX spatial texture formats are not ready."));
		return false;
	}

	const bool bInputContentWidthFits = Inputs.InputContentExtent.X <= Inputs.DescriptorInputExtent.X;
	const bool bInputContentHeightFits = Inputs.InputContentExtent.Y <= Inputs.DescriptorInputExtent.Y;
	if (!bInputContentWidthFits || !bInputContentHeightFits)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial input content exceeds the descriptor input extent."));
		return false;
	}

	const bool bOutputWidthSupportsUpscaling = Inputs.OutputExtent.X >= Inputs.InputContentExtent.X;
	const bool bOutputHeightSupportsUpscaling = Inputs.OutputExtent.Y >= Inputs.InputContentExtent.Y;
	if (!bOutputWidthSupportsUpscaling || !bOutputHeightSupportsUpscaling)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial does not support downscaling."));
		return false;
	}

	const bool bDescriptorInputResolutionChanged = ConfiguredDescriptorInputExtent != Inputs.DescriptorInputExtent;
	const bool bInputContentResolutionChanged = ConfiguredInputContentExtent != Inputs.InputContentExtent;
	const bool bOutputResolutionChanged = ConfiguredOutputExtent != Inputs.OutputExtent;
	const bool bFormatChanged = Resources->Formats != Formats;
	const bool bHasScaler = Resources->HasScaler();
	const bool bDescriptorGeometryChanged = bDescriptorInputResolutionChanged || bOutputResolutionChanged;
	const bool bNeedsScalerRecreation = !bHasScaler || bDescriptorGeometryChanged || bFormatChanged;

	if (bNeedsScalerRecreation)
	{
		UE_LOG(LogMetalFX, Log, TEXT("MetalFX SpatialScaler %s. DescriptorInput=%dx%d, InputContent=%dx%d, Output=%dx%d"), bHasScaler ? TEXT("recreate requested") : TEXT("creation requested"), Inputs.DescriptorInputExtent.X, Inputs.DescriptorInputExtent.Y, Inputs.InputContentExtent.X, Inputs.InputContentExtent.Y, Inputs.OutputExtent.X, Inputs.OutputExtent.Y);

		ConfiguredDescriptorInputExtent = Inputs.DescriptorInputExtent;
		ConfiguredInputContentExtent = Inputs.InputContentExtent;
		ConfiguredOutputExtent = Inputs.OutputExtent;
		Resources->Formats = Formats;
		return GenerateUpscaler();
	}

	if (bInputContentResolutionChanged)
	{
		UpdateInputContentSize(Inputs.InputContentExtent);
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Per-frame encode preparation
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::PrepareToEncode(const FMetalFXSpatialEncodeInputs& Inputs, const FMetalFXSpatialTextureFormatGroup& Formats)
{
	const bool bCommonRectsValid = ValidateCommonRects(Inputs.InputRect, Inputs.OutputRect);
	const bool bUpscalerConfigured = bCommonRectsValid && EnsureUpscalerForConfiguration(Inputs, Formats);
	if (!bUpscalerConfigured)
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX Spatial Upscaler is not ready. Skip upscaling this frame."));
		return false;
	}

	UpdateActiveDebugInfo(Inputs.InputRect, Inputs.OutputRect);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Metal command-buffer encoding
//////////////////////////////////////////////////////////////////////////

void FMetalFXSpatialUpscalerCore::ExecuteMetalFX(FRHICommandList& CmdList, FMetalFXSpatialTextureGroup& TextureGroup)
{
	if (!CheckValidate())
	{
		TextureGroup.ReleaseAllTextures();
		return;
	}
	Encode(CmdList, TextureGroup);
}

void FMetalFXSpatialUpscalerCore::Encode(FRHICommandList& CmdList, FMetalFXSpatialTextureGroup& TextureGroup)
{
	FMetalCommandBuffer* CurrentCommandBuffer = GetCurrentMetalCommandBuffer(CmdList);
	if (!CurrentCommandBuffer)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial could not find the active Metal command buffer. Skip upscaling this frame."));
		TextureGroup.ReleaseAllTextures();
		return;
	}

#if METALFX_METALCPP
	Resources->CppScaler->setColorTexture(TextureGroup.ColorTexture.GetTexture());
	Resources->CppScaler->setOutputTexture(TextureGroup.OutputTexture.GetTexture());
	MTL::CommandBuffer* CommandBuffer = CurrentCommandBuffer->GetMTLCmdBuffer();
	if (CommandBuffer)
	{
		@autoreleasepool
		{
			Resources->CppScaler->encodeToCommandBuffer(CommandBuffer);
		}
		TextureGroup.ReleaseAllTexturesDeferred(CommandBuffer);
	}
	else
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial could not find the native Metal command buffer. Skip upscaling this frame."));
		TextureGroup.ReleaseAllTextures();
	}
#elif METALFX_NATIVE
	id<MTLCommandBuffer> CommandBuffer = (__bridge id<MTLCommandBuffer>)CurrentCommandBuffer->GetMTLCmdBuffer();
	if (CommandBuffer)
	{
		Resources->Scaler.colorTexture = TextureGroup.ColorTexture.GetTexture();
		Resources->Scaler.outputTexture = TextureGroup.OutputTexture.GetTexture();
		[Resources->Scaler encodeToCommandBuffer:CommandBuffer];
		TextureGroup.ReleaseAllTexturesDeferred(CommandBuffer);
	}
	else
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial could not find the native Metal command buffer. Skip upscaling this frame."));
		TextureGroup.ReleaseAllTextures();
	}
#endif
}

//////////////////////////////////////////////////////////////////////////
// Core validation
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::CheckValidate() const
{
	const bool bHasResources = Resources != nullptr;
	const bool bHasScaler = bHasResources && Resources->HasScaler();
	const bool bValid = IsInitialized() && bHasScaler;
	if (!bValid)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Spatial Core is not ready. Skip upscaling this frame."));
	}
	return bValid;
}
#endif
