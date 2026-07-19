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
//////////////////////////////////////////////////////////////////////////
// RDG to Metal texture conversion
//////////////////////////////////////////////////////////////////////////

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

	FMetalFXSpatialTextureFormatGroup Formats;
#if METALFX_METALCPP
	Formats.Color = OutTextureGroup.ColorTexture.GetTexture()->pixelFormat();
	Formats.Output = OutTextureGroup.OutputTexture.GetTexture()->pixelFormat();
#elif METALFX_NATIVE
	Formats.Color = static_cast<FMetalFXPixelFormat>([OutTextureGroup.ColorTexture.GetTexture() pixelFormat]);
	Formats.Output = static_cast<FMetalFXPixelFormat>([OutTextureGroup.OutputTexture.GetTexture() pixelFormat]);
#endif
	Resources->Formats = Formats;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// SpatialScaler descriptor creation
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::GenerateUpscaler()
{
	if (!Resources || !Resources->Formats.IsReady())
	{
		return false;
	}

#if METALFX_METALCPP
	Resources->CppScaler.reset();
	MTL::Device* Device = static_cast<MTL::Device*>(GetMetalDevice());
	if (!Device)
	{
		return false;
	}

	auto Descriptor = RetainPtr(MTLFX::SpatialScalerDescriptor::alloc()->init());
	if (!Descriptor->supportsDevice(Device))
	{
		return false;
	}

	Descriptor->setInputWidth(InputTextureWidth);
	Descriptor->setInputHeight(InputTextureHeight);
	Descriptor->setOutputWidth(OutputWidth);
	Descriptor->setOutputHeight(OutputHeight);
	Descriptor->setColorTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Color));
	Descriptor->setOutputTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Output));
	Resources->CppScaler = NS::RetainPtr(Descriptor->newSpatialScaler(Device));
#elif METALFX_NATIVE
	[Resources->Scaler release];
	Resources->Scaler = nil;

	id<MTLDevice> Device = (__bridge id<MTLDevice>)GetMetalDevice();
	if (Device == nil || ![MTLFXSpatialScalerDescriptor supportsDevice:Device])
	{
		return false;
	}

	MTLFXSpatialScalerDescriptor* Descriptor = [MTLFXSpatialScalerDescriptor new];
	Descriptor.inputWidth = InputTextureWidth;
	Descriptor.inputHeight = InputTextureHeight;
	Descriptor.outputWidth = OutputWidth;
	Descriptor.outputHeight = OutputHeight;
	Descriptor.colorTextureFormat = static_cast<MTLPixelFormat>(Resources->Formats.Color);
	Descriptor.outputTextureFormat = static_cast<MTLPixelFormat>(Resources->Formats.Output);
	Resources->Scaler = [Descriptor newSpatialScalerWithDevice:Device];
	[Descriptor release];
#endif

	if (!Resources->HasScaler())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX SpatialScaler creation failed."));
		return false;
	}

	ConfiguredFormats = Resources->Formats;
	UpdateInputContentSize(FIntPoint(InputContentWidth, InputContentHeight));
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Mutable input-content resolution
//////////////////////////////////////////////////////////////////////////

void FMetalFXSpatialUpscalerCore::UpdateInputContentSize(FIntPoint InputContentExtent)
{
	InputContentWidth = InputContentExtent.X;
	InputContentHeight = InputContentExtent.Y;
#if METALFX_METALCPP
	Resources->CppScaler->setInputContentWidth(InputContentWidth);
	Resources->CppScaler->setInputContentHeight(InputContentHeight);
#elif METALFX_NATIVE
	Resources->Scaler.inputContentWidth = InputContentWidth;
	Resources->Scaler.inputContentHeight = InputContentHeight;
#endif
}

//////////////////////////////////////////////////////////////////////////
// Scaler configuration
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::EnsureUpscalerForConfiguration(
	const FMetalFXSpatialEncodeInputs& Inputs,
	const FMetalFXSpatialTextureFormatGroup& Formats)
{
	if (!Resources
		|| !ValidateCommonExtents(Inputs.InputTextureExtent, Inputs.InputContentExtent, Inputs.OutputExtent)
		|| !Formats.IsReady())
	{
		return false;
	}

	const FIntPoint ConfiguredInputTextureExtent(InputTextureWidth, InputTextureHeight);
	const FIntPoint ConfiguredInputContentExtent(InputContentWidth, InputContentHeight);
	const FIntPoint ConfiguredOutputExtent(OutputWidth, OutputHeight);

	const bool bInputTextureResolutionChanged = ConfiguredInputTextureExtent != Inputs.InputTextureExtent;
	const bool bInputContentResolutionChanged = ConfiguredInputContentExtent != Inputs.InputContentExtent;
	const bool bOutputResolutionChanged = ConfiguredOutputExtent != Inputs.OutputExtent;
	const bool bFormatChanged = ConfiguredFormats != Formats;
	const bool bNeedsScalerRecreation =
		!Resources->HasScaler()
		|| bInputTextureResolutionChanged
		|| bOutputResolutionChanged
		|| bFormatChanged;

	if (bNeedsScalerRecreation)
	{
		InputTextureWidth = Inputs.InputTextureExtent.X;
		InputTextureHeight = Inputs.InputTextureExtent.Y;
		InputContentWidth = Inputs.InputContentExtent.X;
		InputContentHeight = Inputs.InputContentExtent.Y;
		OutputWidth = Inputs.OutputExtent.X;
		OutputHeight = Inputs.OutputExtent.Y;
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

bool FMetalFXSpatialUpscalerCore::PrepareToEncode(const FMetalFXSpatialEncodeInputs& Inputs)
{
	if (!ValidateCommonRects(Inputs.InputRect, Inputs.OutputRect)
		|| !EnsureUpscalerForConfiguration(Inputs, Resources->Formats))
	{
		return false;
	}

	UpdateActiveDebugInfo(Inputs.InputRect, Inputs.OutputRect, Inputs.ScreenPercentage);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Metal command-buffer encoding
//////////////////////////////////////////////////////////////////////////

void FMetalFXSpatialUpscalerCore::ExecuteMetalFX(
	FRHICommandList& CmdList,
	FMetalFXSpatialTextureGroup& TextureGroup)
{
	if (!CheckValidate())
	{
		TextureGroup.ReleaseAllTexture();
		return;
	}
	Encode(CmdList, TextureGroup);
}

void FMetalFXSpatialUpscalerCore::Encode(
	FRHICommandList& CmdList,
	FMetalFXSpatialTextureGroup& TextureGroup)
{
	FMetalCommandBuffer* CurrentCommandBuffer = GetCurrentMetalCommandBuffer(CmdList);
	if (!CurrentCommandBuffer)
	{
		TextureGroup.ReleaseAllTexture();
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
		TextureGroup.ReleaseAllTextureDeferred(CommandBuffer);
	}
	else
	{
		TextureGroup.ReleaseAllTexture();
	}
#elif METALFX_NATIVE
	id<MTLCommandBuffer> CommandBuffer = (__bridge id<MTLCommandBuffer>)CurrentCommandBuffer;
	Resources->Scaler.colorTexture = TextureGroup.ColorTexture.GetTexture();
	Resources->Scaler.outputTexture = TextureGroup.OutputTexture.GetTexture();
	[Resources->Scaler encodeToCommandBuffer:CommandBuffer];
	TextureGroup.ReleaseAllTextureDeferred(CommandBuffer);
#endif
}

//////////////////////////////////////////////////////////////////////////
// Core validation
//////////////////////////////////////////////////////////////////////////

bool FMetalFXSpatialUpscalerCore::CheckValidate() const
{
	const bool bValid = IsInitialized() && Resources && Resources->HasScaler();
	if (!bValid)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Spatial Core is not ready."));
	}
	return bValid;
}
#endif
