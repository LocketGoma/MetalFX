#include "MetalFXTemporalUpscalerCore.h"
#include "MetalFXCoreUtility.h"
#include "MetalFXSettings.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalCommandBuffer.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>
#endif

struct FMetalFXTemporalCoreResources
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
	NS::SharedPtr<MTLFX::TemporalScaler> CppScaler;
#elif METALFX_NATIVE
	id<MTLFXTemporalScaler> Scaler = nil;
#endif
#endif

	FMetalFXTemporalTextureFormatGroup Formats;
};

FMetalFXTemporalUpscalerCore::FMetalFXTemporalUpscalerCore()
{
#if METALFX_PLUGIN_ENABLED
	Resources = std::make_unique<FMetalFXTemporalCoreResources>();
#endif
}

FMetalFXTemporalUpscalerCore::~FMetalFXTemporalUpscalerCore()
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

float FMetalFXTemporalUpscalerCore::GetMinUpsampleResolutionFraction() const
{
	return GetMetalFXMinUpscaleResolutionFraction();
}

float FMetalFXTemporalUpscalerCore::GetMaxUpsampleResolutionFraction() const
{
	return GetMetalFXMaxUpscaleResolutionFraction();
}

#if METALFX_PLUGIN_ENABLED
static bool IsMetalFXContentScaleSupported(FIntPoint InputContentExtent, FIntPoint OutputExtent)
{
	constexpr int32 MetalFXMaxUpscaleFactor = 3;

	if (InputContentExtent.X <= 0 || InputContentExtent.Y <= 0 || OutputExtent.X <= 0 || OutputExtent.Y <= 0)
	{
		return false;
	}

	return OutputExtent.X <= InputContentExtent.X * MetalFXMaxUpscaleFactor
		&& OutputExtent.Y <= InputContentExtent.Y * MetalFXMaxUpscaleFactor;
}

bool FMetalFXTemporalUpscalerCore::GenerateUpscaler()
{
	if (!Resources)
	{
		return false;
	}

	if (!Resources->Formats.IsReady())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler generation skipped because texture formats are not ready."));
		return false;
	}

	bool bSuccess = false;

#if METALFX_METALCPP
	if (Resources->HasScaler())
	{
		Resources->CppScaler.reset();
	}

	MTL::Device* MetalDevice = static_cast<MTL::Device*>(GetMetalDevice());
	if (MetalDevice)
	{
		auto Desc = RetainPtr(MTLFX::TemporalScalerDescriptor::alloc()->init());
		if (Desc->supportsDevice(MetalDevice))
		{
			Desc->setInputWidth(m_InputTextureW);
			Desc->setInputHeight(m_InputTextureH);
			Desc->setOutputWidth(m_OutputW);
			Desc->setOutputHeight(m_OutputH);
			Desc->setColorTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Color));
			Desc->setDepthTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Depth));
			Desc->setMotionTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Motion));
			Desc->setOutputTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Output));
			Desc->setAutoExposureEnabled(true);

			Resources->CppScaler = NS::RetainPtr(Desc->newTemporalScaler(MetalDevice));
			bSuccess = Resources->HasScaler();
		}
	}
	else
	{
		NSLog(@"Metal device for MetalFX was not found.");
	}
#elif METALFX_NATIVE
	if (Resources->HasScaler())
	{
		[Resources->Scaler release];
		Resources->Scaler = nil;
	}

	id<MTLDevice> MetalDevice = (__bridge id<MTLDevice>)GetMetalDevice();
	if (MetalDevice != nil)
	{
		Resources->Scaler = MetalFXCreateTemporalUpscaler(MetalDevice, Resources->Formats, m_InputTextureW, m_InputTextureH, m_OutputW, m_OutputH);
		bSuccess = Resources->HasScaler();
	}
	else
	{
		NSLog(@"Metal device for MetalFX was not found.");
	}
#endif

	if (bSuccess)
	{
		UpdateInputContentSize(FIntPoint(m_InputContentW, m_InputContentH));
		Resources->Formats.ResetChangeState();
	}
	else
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX TemporalScaler API generation failed."));
	}

	return bSuccess;
}

void FMetalFXTemporalUpscalerCore::UpdateInputContentSize(FIntPoint InputContentExtent)
{
	m_InputContentW = InputContentExtent.X;
	m_InputContentH = InputContentExtent.Y;

#if METALFX_METALCPP
	Resources->CppScaler->setInputContentWidth(m_InputContentW);
	Resources->CppScaler->setInputContentHeight(m_InputContentH);
#elif METALFX_NATIVE
	MetalFXUpdateScalerResolution(Resources->Scaler, m_InputContentW, m_InputContentH);
#endif
}

bool FMetalFXTemporalUpscalerCore::SetTexturesToGroup(
	const FMetalFXTemporalPassParameters& Parameters,
	FMetalFXTemporalTextureGroup& OutTextureGroup)
{
	if (!Parameters.ColorTexture
		|| !Parameters.DepthTexture
		|| !Parameters.VelocityTexture
		|| !Parameters.OutputTexture)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX temporal texture setup failed because one or more textures are invalid."));
		return false;
	}

	if (!Resources)
	{
		return false;
	}

	FMetalFXTemporalTextureFormatGroup Formats;

	OutTextureGroup.ColorTexture = CreateMetalFXTextureView(Parameters.ColorTexture);
	OutTextureGroup.DepthTexture = CreateMetalFXTextureView(Parameters.DepthTexture);
	OutTextureGroup.VelocityTexture = CreateMetalFXTextureView(Parameters.VelocityTexture);
	OutTextureGroup.OutputTexture = CreateMetalFXTextureView(Parameters.OutputTexture);

	if (!OutTextureGroup.ColorTexture.IsValid()
		|| !OutTextureGroup.DepthTexture.IsValid()
		|| !OutTextureGroup.VelocityTexture.IsValid()
		|| !OutTextureGroup.OutputTexture.IsValid())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX temporal texture view conversion failed."));
		return false;
	}

#if METALFX_METALCPP
	Formats.Color = OutTextureGroup.ColorTexture.GetTexture()->pixelFormat();
	Formats.Depth = OutTextureGroup.DepthTexture.GetTexture()->pixelFormat();
	Formats.Motion = OutTextureGroup.VelocityTexture.GetTexture()->pixelFormat();
	Formats.Output = OutTextureGroup.OutputTexture.GetTexture()->pixelFormat();
#elif METALFX_NATIVE
	Formats.Color = static_cast<FMetalFXPixelFormat>([OutTextureGroup.ColorTexture.GetTexture() pixelFormat]);
	Formats.Depth = static_cast<FMetalFXPixelFormat>([OutTextureGroup.DepthTexture.GetTexture() pixelFormat]);
	Formats.Motion = static_cast<FMetalFXPixelFormat>([OutTextureGroup.VelocityTexture.GetTexture() pixelFormat]);
	Formats.Output = static_cast<FMetalFXPixelFormat>([OutTextureGroup.OutputTexture.GetTexture() pixelFormat]);
#endif

	if (Resources->Formats.UpdateChangeState(Formats))
	{
		Resources->Formats = Formats;
	}

	return true;
}

void FMetalFXTemporalUpscalerCore::SetJitterOffset(FVector2D Offset)
{
	if (!Resources || !CheckValidate())
	{
		return;
	}

#if METALFX_METALCPP
	Resources->CppScaler->setJitterOffsetX(static_cast<float>(Offset.X));
	Resources->CppScaler->setJitterOffsetY(static_cast<float>(Offset.Y));
#elif METALFX_NATIVE
	MetalFXSetJitterOffset(Resources->Scaler, Offset.X, Offset.Y);
#endif
}

void FMetalFXTemporalUpscalerCore::SetMotionVectorScale(FVector2f Scale)
{
	if (!Resources || !CheckValidate())
	{
		return;
	}

#if METALFX_METALCPP
	Resources->CppScaler->setMotionVectorScaleX(Scale.X);
	Resources->CppScaler->setMotionVectorScaleY(Scale.Y);
#elif METALFX_NATIVE
	MetalFXSetMotionVectorScale(Resources->Scaler, Scale.X, Scale.Y);
#endif
}

bool FMetalFXTemporalUpscalerCore::EnsureUpscalerForConfiguration(
	FIntPoint InputTextureExtent,
	FIntPoint InputContentExtent,
	FIntPoint OutputExtent,
	const FMetalFXTemporalTextureFormatGroup& Formats)
{
	if (!Resources)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal Core resources are not available."));
		return false;
	}

	if (!ValidateCommonExtents(InputTextureExtent, InputContentExtent, OutputExtent))
	{
		return false;
	}

	if (!Formats.IsReady())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX temporal texture formats are not ready."));
		return false;
	}

	if (!IsMetalFXContentScaleSupported(InputContentExtent, OutputExtent))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler configuration skipped because InputContent to Output scale exceeds 3x per dimension. InputTexture=%dx%d, InputContent=%dx%d, Output=%dx%d"), InputTextureExtent.X, InputTextureExtent.Y, InputContentExtent.X, InputContentExtent.Y, OutputExtent.X, OutputExtent.Y);
		return false;
	}

	const bool bHasScaler = Resources->HasScaler();
	const bool bInputTextureResolutionChanged = m_InputTextureW != InputTextureExtent.X || m_InputTextureH != InputTextureExtent.Y;
	const bool bInputContentResolutionChanged = m_InputContentW != InputContentExtent.X || m_InputContentH != InputContentExtent.Y;
	const bool bOutputResolutionChanged = m_OutputW != OutputExtent.X || m_OutputH != OutputExtent.Y;
	const bool bFormatChanged = Resources->Formats.GetIsChanged() || Resources->Formats.IsChanged(Formats);

	if (bHasScaler
		&& !bFormatChanged
		&& !bInputTextureResolutionChanged
		&& !bOutputResolutionChanged
		&& !bInputContentResolutionChanged)
	{
		return true;
	}

	FString RecreateReason;
	auto AddRecreateReason = [&RecreateReason](const TCHAR* Reason)
	{
		if (!RecreateReason.IsEmpty())
		{
			RecreateReason += TEXT(", ");
		}
		RecreateReason += Reason;
	};

	if (!bHasScaler)
	{
		AddRecreateReason(TEXT("InitialCreate"));
	}
	if (bFormatChanged)
	{
		AddRecreateReason(TEXT("FormatChanged"));
	}
	if (bInputTextureResolutionChanged)
	{
		AddRecreateReason(TEXT("InputTextureSizeChanged"));
	}
	if (bInputContentResolutionChanged)
	{
		AddRecreateReason(TEXT("InputContentSizeChanged"));
	}
	if (bOutputResolutionChanged)
	{
		AddRecreateReason(TEXT("OutputSizeChanged"));
	}

	UE_LOG(LogMetalFX, Log, TEXT("MetalFX TemporalScaler %s. Reason: %s. InputTexture=%dx%d, InputContent=%dx%d, Output=%dx%d"), bHasScaler ? TEXT("recreate requested") : TEXT("creation requested"), *RecreateReason, InputTextureExtent.X, InputTextureExtent.Y, InputContentExtent.X, InputContentExtent.Y, OutputExtent.X, OutputExtent.Y);

	m_InputTextureW = InputTextureExtent.X;
	m_InputTextureH = InputTextureExtent.Y;
	m_InputContentW = InputContentExtent.X;
	m_InputContentH = InputContentExtent.Y;
	m_OutputW = OutputExtent.X;
	m_OutputH = OutputExtent.Y;
	Resources->Formats = Formats;

	return GenerateUpscaler();
}

bool FMetalFXTemporalUpscalerCore::CheckForExecuteMetalFX(
	FIntPoint InputTextureExtent,
	FIntPoint InputContentExtent,
	FIntPoint OutputExtent)
{
	if (!Resources)
	{
		return false;
	}

	if (!EnsureUpscalerForConfiguration(InputTextureExtent, InputContentExtent, OutputExtent, Resources->Formats))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Temporal Upscaler is not ready. Skip upscaling this frame."));
		return false;
	}

	return true;
}

bool FMetalFXTemporalUpscalerCore::PrepareToEncode(const FMetalFXTemporalEncodeInputs& Inputs)
{
	if (!ValidateCommonRects(Inputs.InputRect, Inputs.OutputRect))
	{
		return false;
	}

	if (!CheckForExecuteMetalFX(Inputs.InputTextureExtent, Inputs.InputContentExtent, Inputs.OutputExtent))
	{
		return false;
	}

	SetJitterOffset(Inputs.JitterOffset);
	SetMotionVectorScale(Inputs.MotionVectorScale);
	UpdateActiveDebugInfo(Inputs.InputRect, Inputs.OutputRect, Inputs.ScreenPercentage);
	return true;
}

void FMetalFXTemporalUpscalerCore::ExecuteMetalFX(
	FRHICommandList& CmdList,
	FMetalFXTemporalTextureGroup& TextureGroup)
{
	if (!CheckValidate())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Temporal Upscaler is invalid. Skip upscaling this frame."));
		TextureGroup.ReleaseAllTexture();
		return;
	}

	Encode(CmdList, TextureGroup);
}

void FMetalFXTemporalUpscalerCore::Encode(
	FRHICommandList& CmdList,
	FMetalFXTemporalTextureGroup& TextureGroup)
{
	FMetalCommandBuffer* CurrentCommandBuffer = GetCurrentMetalCommandBuffer(CmdList);
	if (!CurrentCommandBuffer)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX could not find the active Metal command buffer."));
		return;
	}

#if METALFX_METALCPP
	Resources->CppScaler->setColorTexture(TextureGroup.ColorTexture.GetTexture());
	Resources->CppScaler->setDepthTexture(TextureGroup.DepthTexture.GetTexture());
	Resources->CppScaler->setMotionTexture(TextureGroup.VelocityTexture.GetTexture());
	Resources->CppScaler->setOutputTexture(TextureGroup.OutputTexture.GetTexture());

	MTL::CommandBuffer* MetalCommandBuffer = CurrentCommandBuffer->GetMTLCmdBuffer();
	if (MetalCommandBuffer)
	{
		@autoreleasepool
		{
			Resources->CppScaler->encodeToCommandBuffer(MetalCommandBuffer);
		}
		TextureGroup.ReleaseAllTextureDeferred(MetalCommandBuffer);
	}
	else
	{
		TextureGroup.ReleaseAllTexture();
	}
#elif METALFX_NATIVE
	id<MTLTexture> ColorTexture = TextureGroup.ColorTexture.GetTexture();
	id<MTLTexture> DepthTexture = TextureGroup.DepthTexture.GetTexture();
	id<MTLTexture> MotionTexture = TextureGroup.VelocityTexture.GetTexture();
	id<MTLTexture> OutputTexture = TextureGroup.OutputTexture.GetTexture();
	id<MTLCommandBuffer> MetalCommandBuffer = (__bridge id<MTLCommandBuffer>)CurrentCommandBuffer;

	MetalFXEncode(Resources->Scaler, MetalCommandBuffer, ColorTexture, DepthTexture, MotionTexture, OutputTexture);

	TextureGroup.ReleaseAllTextureDeferred(MetalCommandBuffer);
#endif
}

bool FMetalFXTemporalUpscalerCore::CheckValidate() const
{
	const bool bIsValid = IsInitialized() && Resources && Resources->HasScaler();
	if (!bIsValid)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal Core is not ready."));
	}
	return bIsValid;
}
#endif
