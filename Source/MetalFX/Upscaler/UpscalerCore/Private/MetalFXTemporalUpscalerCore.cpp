#include "MetalFXTemporalUpscalerCore.h"
#include "MetalFXCoreUtility.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalCommandBuffer.h"
#endif

struct FMetalFXTemporalCoreResources
{
#if METALFX_PLUGIN_ENABLED
	bool HasScaler() const
	{
#if METALFX_METALCPP
		return CppScaler.get() != nullptr;
#endif
#if METALFX_NATIVE
		return Scaler != nil;
#endif
#if !METALFX_METALCPP && !METALFX_NATIVE
		return false;
#endif
	}

#if METALFX_METALCPP
	NS::SharedPtr<MTLFX::TemporalScaler> CppScaler;
#endif
#if METALFX_NATIVE
	id<MTLFXTemporalScaler> Scaler = nil;
#endif
#endif

	FMetalFXTemporalTextureFormatGroup Formats;
};

struct FMetalFXContentScaleRange
{
	float Min = 1.0f;
	float Max = 1.0f;
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
	ResetUpscaler();
#endif
}

#if METALFX_PLUGIN_ENABLED
void FMetalFXTemporalUpscalerCore::ResetUpscaler()
{
	if (!Resources)
	{
		return;
	}

#if METALFX_METALCPP
	Resources->CppScaler.reset();
#endif
#if METALFX_NATIVE
	[Resources->Scaler release];
	Resources->Scaler = nil;
#endif
}

static bool IsMetalFXContentScaleSupported(FIntPoint InputContentExtent, FIntPoint OutputExtent)
{
	constexpr int32 MetalFXMaxUpscaleFactor = 3;

	const bool bInputWidthValid = InputContentExtent.X > 0;
	const bool bInputHeightValid = InputContentExtent.Y > 0;
	const bool bOutputWidthValid = OutputExtent.X > 0;
	const bool bOutputHeightValid = OutputExtent.Y > 0;
	const bool bInputExtentValid = bInputWidthValid && bInputHeightValid;
	const bool bOutputExtentValid = bOutputWidthValid && bOutputHeightValid;
	if (!bInputExtentValid || !bOutputExtentValid)
	{
		return false;
	}

	const bool bWidthScaleSupported = OutputExtent.X <= InputContentExtent.X * MetalFXMaxUpscaleFactor;
	const bool bHeightScaleSupported = OutputExtent.Y <= InputContentExtent.Y * MetalFXMaxUpscaleFactor;
	return bWidthScaleSupported && bHeightScaleSupported;
}

static bool GetMetalFXContentScaleRange(FIntPoint InputContentExtent, FIntPoint OutputExtent, FMetalFXContentScaleRange& OutScaleRange)
{
	const bool bInputValid = InputContentExtent.X > 0 && InputContentExtent.Y > 0;
	const bool bOutputValid = OutputExtent.X > 0 && OutputExtent.Y > 0;
	if (!bInputValid || !bOutputValid)
	{
		return false;
	}

	const float WidthScale = static_cast<float>(OutputExtent.X) / static_cast<float>(InputContentExtent.X);
	const float HeightScale = static_cast<float>(OutputExtent.Y) / static_cast<float>(InputContentExtent.Y);
	const bool bScalesFinite = FMath::IsFinite(WidthScale) && FMath::IsFinite(HeightScale);
	if (!bScalesFinite)
	{
		return false;
	}

	constexpr float MaximumScaleDifference = 0.01f;
	const bool bAspectRatioCompatible = FMath::Abs(WidthScale - HeightScale) <= MaximumScaleDifference;
	if (!bAspectRatioCompatible)
	{
		return false;
	}

	OutScaleRange.Min = FMath::Min(WidthScale, HeightScale);
	OutScaleRange.Max = FMath::Max(WidthScale, HeightScale);
	return true;
}

#if METALFX_METALCPP
static bool GetMetalFXDeviceContentScaleRange(MTL::Device* MetalDevice, FMetalFXContentScaleRange& OutScaleRange)
{
	if (!MetalDevice)
	{
		return false;
	}

	const float SupportedMinScale = MTLFX::TemporalScalerDescriptor::supportedInputContentMinScale(MetalDevice);
	const float SupportedMaxScale = MTLFX::TemporalScalerDescriptor::supportedInputContentMaxScale(MetalDevice);
	const bool bSupportedRangeValid = FMath::IsFinite(SupportedMinScale) && FMath::IsFinite(SupportedMaxScale) && SupportedMinScale > 0.0f && SupportedMaxScale >= SupportedMinScale;
	if (!bSupportedRangeValid)
	{
		return false;
	}

	OutScaleRange.Min = SupportedMinScale;
	OutScaleRange.Max = SupportedMaxScale;
	return true;
}

static bool DoesMetalFXTextureMatchExtent(MTL::Texture* Texture, FIntPoint Extent)
{
	if (!Texture)
	{
		return false;
	}

	const bool bWidthMatches = Texture->width() == static_cast<NS::UInteger>(Extent.X);
	const bool bHeightMatches = Texture->height() == static_cast<NS::UInteger>(Extent.Y);
	return bWidthMatches && bHeightMatches;
}
#endif

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
	ResetUpscaler();

#if METALFX_METALCPP
	MTL::Device* MetalDevice = static_cast<MTL::Device*>(GetMetalDevice());
	if (MetalDevice)
	{
		auto Descriptor = NS::TransferPtr(MTLFX::TemporalScalerDescriptor::alloc()->init());
		const bool bDescriptorValid = Descriptor.get() != nullptr;
		const bool bDeviceSupported = bDescriptorValid && MTLFX::TemporalScalerDescriptor::supportsDevice(MetalDevice);
		if (bDeviceSupported)
		{
			FMetalFXContentScaleRange RequestedScaleRange;
			const bool bRequestedScaleValid = GetMetalFXContentScaleRange(ConfiguredInputContentExtent, ConfiguredOutputExtent, RequestedScaleRange);
			if (bConfiguredDynamicInputEnabled && !bRequestedScaleValid)
			{
				UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler dynamic input rejected because input and output aspect ratios are incompatible. InputContent=%dx%d, Output=%dx%d"), ConfiguredInputContentExtent.X, ConfiguredInputContentExtent.Y, ConfiguredOutputExtent.X, ConfiguredOutputExtent.Y);
				return false;
			}

			FMetalFXContentScaleRange DeviceScaleRange;
			const bool bDeviceScaleRangeAvailable = GetMetalFXDeviceContentScaleRange(MetalDevice, DeviceScaleRange);
			const bool bRequestedScaleSupported = !bDeviceScaleRangeAvailable || RequestedScaleRange.Min >= DeviceScaleRange.Min || FMath::IsNearlyEqual(RequestedScaleRange.Min, DeviceScaleRange.Min);
			const bool bRequestedMaxScaleSupported = !bDeviceScaleRangeAvailable || RequestedScaleRange.Max <= DeviceScaleRange.Max || FMath::IsNearlyEqual(RequestedScaleRange.Max, DeviceScaleRange.Max);
			if (bConfiguredDynamicInputEnabled && (!bRequestedScaleSupported || !bRequestedMaxScaleSupported))
			{
				UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler dynamic input scale is outside the device range. Requested=%.3f-%.3f, Supported=%.3f-%.3f"), RequestedScaleRange.Min, RequestedScaleRange.Max, DeviceScaleRange.Min, DeviceScaleRange.Max);
				return false;
			}
			if (bConfiguredDynamicInputEnabled && bDeviceScaleRangeAvailable)
			{
				UE_LOG(LogMetalFX, Log, TEXT("MetalFX TemporalScaler dynamic input scale configured. Requested=%.3f-%.3f, Supported=%.3f-%.3f"), RequestedScaleRange.Min, RequestedScaleRange.Max, DeviceScaleRange.Min, DeviceScaleRange.Max);
			}

			Descriptor->setInputWidth(ConfiguredDescriptorInputExtent.X);
			Descriptor->setInputHeight(ConfiguredDescriptorInputExtent.Y);
			Descriptor->setOutputWidth(ConfiguredOutputExtent.X);
			Descriptor->setOutputHeight(ConfiguredOutputExtent.Y);
			Descriptor->setColorTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Color));
			Descriptor->setDepthTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Depth));
			Descriptor->setMotionTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Motion));
			Descriptor->setOutputTextureFormat(static_cast<MTL::PixelFormat>(Resources->Formats.Output));
			Descriptor->setAutoExposureEnabled(true);

			if (bConfiguredDynamicInputEnabled)
			{
				Descriptor->setInputContentPropertiesEnabled(true);
				Descriptor->setInputContentMinScale(RequestedScaleRange.Min);
				Descriptor->setInputContentMaxScale(RequestedScaleRange.Max);
			}

			Resources->CppScaler = NS::TransferPtr(Descriptor->newTemporalScaler(MetalDevice));
			bSuccess = Resources->HasScaler();
		}
	}
#endif
#if METALFX_NATIVE
	id<MTLDevice> MetalDevice = (__bridge id<MTLDevice>)GetMetalDevice();
	if (MetalDevice != nil)
	{
		Resources->Scaler = MetalFXCreateTemporalUpscaler(MetalDevice, Resources->Formats, ConfiguredDescriptorInputExtent.X, ConfiguredDescriptorInputExtent.Y, ConfiguredOutputExtent.X, ConfiguredOutputExtent.Y);
		bSuccess = Resources->HasScaler();
	}
#endif

	if (bSuccess)
	{
		UpdateInputContentSize(ConfiguredInputContentExtent);
	}

	if (!bSuccess)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX TemporalScaler API generation failed."));
	}

	return bSuccess;
}

void FMetalFXTemporalUpscalerCore::UpdateInputContentSize(FIntPoint InputContentExtent)
{
	// Descriptor와 출력 크기는 재생성이 필요하지만, 입력 컨텐츠 크기는 생성된 Scaler에 직접 갱신할 수 있다.
#if METALFX_METALCPP
	Resources->CppScaler->setInputContentWidth(InputContentExtent.X);
	Resources->CppScaler->setInputContentHeight(InputContentExtent.Y);
#endif
#if METALFX_NATIVE
	MetalFXUpdateScalerResolution(Resources->Scaler, InputContentExtent.X, InputContentExtent.Y);
#endif

	ConfiguredInputContentExtent = InputContentExtent;
}

bool FMetalFXTemporalUpscalerCore::SetTexturesToGroup(const FMetalFXTemporalPassParameters& Parameters, FMetalFXTemporalTextureGroup& OutTextureGroup, FMetalFXTemporalTextureFormatGroup& OutFormats)
{
	const bool bColorTextureValid = Parameters.ColorTexture != nullptr;
	const bool bDepthTextureValid = Parameters.DepthTexture != nullptr;
	const bool bVelocityTextureValid = Parameters.VelocityTexture != nullptr;
	const bool bOutputTextureValid = Parameters.OutputTexture != nullptr;
	const bool bInputTexturesValid = bColorTextureValid && bDepthTextureValid && bVelocityTextureValid;
	if (!bInputTexturesValid || !bOutputTextureValid)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX temporal texture setup failed because one or more textures are invalid."));
		return false;
	}

	OutTextureGroup.ColorTexture = CreateMetalFXTextureView(Parameters.ColorTexture);
	OutTextureGroup.DepthTexture = CreateMetalFXTextureView(Parameters.DepthTexture);
	OutTextureGroup.VelocityTexture = CreateMetalFXTextureView(Parameters.VelocityTexture);
	OutTextureGroup.OutputTexture = CreateMetalFXTextureView(Parameters.OutputTexture);

	const bool bColorTextureViewValid = OutTextureGroup.ColorTexture.IsValid();
	const bool bDepthTextureViewValid = OutTextureGroup.DepthTexture.IsValid();
	const bool bVelocityTextureViewValid = OutTextureGroup.VelocityTexture.IsValid();
	const bool bOutputTextureViewValid = OutTextureGroup.OutputTexture.IsValid();
	const bool bInputTextureViewsValid = bColorTextureViewValid && bDepthTextureViewValid && bVelocityTextureViewValid;
	if (!bInputTextureViewsValid || !bOutputTextureViewValid)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX temporal texture view conversion failed."));
		return false;
	}

#if METALFX_METALCPP
	OutFormats.Color = OutTextureGroup.ColorTexture.GetTexture()->pixelFormat();
	OutFormats.Depth = OutTextureGroup.DepthTexture.GetTexture()->pixelFormat();
	OutFormats.Motion = OutTextureGroup.VelocityTexture.GetTexture()->pixelFormat();
	OutFormats.Output = OutTextureGroup.OutputTexture.GetTexture()->pixelFormat();
#endif
#if METALFX_NATIVE
	OutFormats.Color = static_cast<FMetalFXPixelFormat>([OutTextureGroup.ColorTexture.GetTexture() pixelFormat]);
	OutFormats.Depth = static_cast<FMetalFXPixelFormat>([OutTextureGroup.DepthTexture.GetTexture() pixelFormat]);
	OutFormats.Motion = static_cast<FMetalFXPixelFormat>([OutTextureGroup.VelocityTexture.GetTexture() pixelFormat]);
	OutFormats.Output = static_cast<FMetalFXPixelFormat>([OutTextureGroup.OutputTexture.GetTexture() pixelFormat]);
#endif

	if (!OutFormats.IsReady())
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX temporal texture formats are invalid."));
		return false;
	}

	return true;
}

void FMetalFXTemporalUpscalerCore::SetHistoryReset(bool bReset)
{
#if METALFX_METALCPP
	Resources->CppScaler->setReset(bReset);
#endif
#if METALFX_NATIVE
	MetalFXSetReset(Resources->Scaler, bReset);
#endif
}

void FMetalFXTemporalUpscalerCore::SetDepthReversed(bool bDepthReversed)
{
#if METALFX_METALCPP
	Resources->CppScaler->setDepthReversed(bDepthReversed);
#endif
#if METALFX_NATIVE
	MetalFXSetDepthReversed(Resources->Scaler, bDepthReversed);
#endif
}

void FMetalFXTemporalUpscalerCore::SetPreExposure(float PreExposure)
{
#if METALFX_METALCPP
	Resources->CppScaler->setPreExposure(PreExposure);
#endif
#if METALFX_NATIVE
	MetalFXSetPreExposure(Resources->Scaler, PreExposure);
#endif
}

void FMetalFXTemporalUpscalerCore::SetJitterOffset(FVector2D Offset)
{
#if METALFX_METALCPP
	Resources->CppScaler->setJitterOffsetX(static_cast<float>(Offset.X));
	Resources->CppScaler->setJitterOffsetY(static_cast<float>(Offset.Y));
#endif
#if METALFX_NATIVE
	MetalFXSetJitterOffset(Resources->Scaler, static_cast<float>(Offset.X), static_cast<float>(Offset.Y));
#endif
}

void FMetalFXTemporalUpscalerCore::SetMotionVectorScale(FVector2f Scale)
{
#if METALFX_METALCPP
	Resources->CppScaler->setMotionVectorScaleX(Scale.X);
	Resources->CppScaler->setMotionVectorScaleY(Scale.Y);
#endif
#if METALFX_NATIVE
	MetalFXSetMotionVectorScale(Resources->Scaler, Scale.X, Scale.Y);
#endif
}

bool FMetalFXTemporalUpscalerCore::EnsureUpscalerForConfiguration(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent, bool bDynamicInputEnabled, const FMetalFXTemporalTextureFormatGroup& Formats)
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
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler configuration skipped because InputContent to Output scale exceeds 3x per dimension. DescriptorInput=%dx%d, InputContent=%dx%d, Output=%dx%d"), InputTextureExtent.X, InputTextureExtent.Y, InputContentExtent.X, InputContentExtent.Y, OutputExtent.X, OutputExtent.Y);
		return false;
	}

	const bool bInputContentWidthFits = InputContentExtent.X <= InputTextureExtent.X;
	const bool bInputContentHeightFits = InputContentExtent.Y <= InputTextureExtent.Y;
	if (bDynamicInputEnabled && (!bInputContentWidthFits || !bInputContentHeightFits))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler dynamic input content does not fit the descriptor input. DescriptorInput=%dx%d, InputContent=%dx%d"), InputTextureExtent.X, InputTextureExtent.Y, InputContentExtent.X, InputContentExtent.Y);
		return false;
	}

	FMetalFXContentScaleRange RequestedScaleRange;
	if (bDynamicInputEnabled && !GetMetalFXContentScaleRange(InputContentExtent, OutputExtent, RequestedScaleRange))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler dynamic input requires matching input and output aspect ratios. InputContent=%dx%d, Output=%dx%d"), InputContentExtent.X, InputContentExtent.Y, OutputExtent.X, OutputExtent.Y);
		return false;
	}

	const bool bHasScaler = Resources->HasScaler();
	const bool bDescriptorInputResolutionChanged = ConfiguredDescriptorInputExtent != InputTextureExtent;
	const bool bInputContentResolutionChanged = ConfiguredInputContentExtent != InputContentExtent;
	const bool bOutputResolutionChanged = ConfiguredOutputExtent != OutputExtent;
	const bool bDynamicInputChanged = bConfiguredDynamicInputEnabled != bDynamicInputEnabled;
	const bool bFormatChanged = Resources->Formats != Formats;
	const bool bDynamicInputScaleChanged = bDynamicInputEnabled && bInputContentResolutionChanged;
	const bool bDescriptorGeometryChanged = bDescriptorInputResolutionChanged || bOutputResolutionChanged || bDynamicInputChanged || bDynamicInputScaleChanged;
	const bool bNeedsScalerRecreation = !bHasScaler || bFormatChanged || bDescriptorGeometryChanged;

	if (!bNeedsScalerRecreation)
	{
		if (bInputContentResolutionChanged)
		{
			UpdateInputContentSize(InputContentExtent);
		}
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
	if (bDescriptorInputResolutionChanged)
	{
		AddRecreateReason(TEXT("DescriptorInputSizeChanged"));
	}
	if (bInputContentResolutionChanged)
	{
		AddRecreateReason(TEXT("InputContentSizeChanged"));
	}
	if (bOutputResolutionChanged)
	{
		AddRecreateReason(TEXT("OutputSizeChanged"));
	}
	if (bDynamicInputChanged)
	{
		AddRecreateReason(TEXT("DynamicInputChanged"));
	}

	UE_LOG(LogMetalFX, Log, TEXT("MetalFX TemporalScaler %s. Reason: %s. DescriptorInput=%dx%d, InputContent=%dx%d, Output=%dx%d, DynamicInput=%s, ContentScale=%.3f-%.3f"), bHasScaler ? TEXT("recreate requested") : TEXT("creation requested"), *RecreateReason, InputTextureExtent.X, InputTextureExtent.Y, InputContentExtent.X, InputContentExtent.Y, OutputExtent.X, OutputExtent.Y, bDynamicInputEnabled ? TEXT("true") : TEXT("false"), RequestedScaleRange.Min, RequestedScaleRange.Max);

	ConfiguredDescriptorInputExtent = InputTextureExtent;
	ConfiguredInputContentExtent = InputContentExtent;
	ConfiguredOutputExtent = OutputExtent;
	bConfiguredDynamicInputEnabled = bDynamicInputEnabled;
	Resources->Formats = Formats;

	return GenerateUpscaler();
}

bool FMetalFXTemporalUpscalerCore::PrepareToEncode(const FMetalFXTemporalEncodeInputs& Inputs, const FMetalFXTemporalTextureFormatGroup& Formats, const FMetalFXTemporalTextureGroup& TextureGroup)
{
	if (!ValidateCommonRects(Inputs.InputRect, Inputs.OutputRect))
	{
		return false;
	}

#if METALFX_METALCPP
	if (Inputs.bDynamicInputEnabled)
	{
		const bool bColorExtentMatches = DoesMetalFXTextureMatchExtent(TextureGroup.ColorTexture.GetTexture(), Inputs.DescriptorInputExtent);
		const bool bDepthExtentMatches = DoesMetalFXTextureMatchExtent(TextureGroup.DepthTexture.GetTexture(), Inputs.DescriptorInputExtent);
		const bool bMotionExtentMatches = DoesMetalFXTextureMatchExtent(TextureGroup.VelocityTexture.GetTexture(), Inputs.DescriptorInputExtent);
		const bool bOutputExtentMatches = DoesMetalFXTextureMatchExtent(TextureGroup.OutputTexture.GetTexture(), Inputs.OutputExtent);
		const bool bInputExtentsMatch = bColorExtentMatches && bDepthExtentMatches && bMotionExtentMatches;
		if (!bInputExtentsMatch || !bOutputExtentMatches)
		{
			UE_LOG(LogMetalFX, Warning, TEXT("MetalFX TemporalScaler dynamic input texture dimensions do not match the descriptor. Color=%s, Depth=%s, Motion=%s, Output=%s"),
				bColorExtentMatches ? TEXT("match") : TEXT("mismatch"),
				bDepthExtentMatches ? TEXT("match") : TEXT("mismatch"),
				bMotionExtentMatches ? TEXT("match") : TEXT("mismatch"),
				bOutputExtentMatches ? TEXT("match") : TEXT("mismatch"));
			return false;
		}
	}
#endif

	if (!EnsureUpscalerForConfiguration(Inputs.DescriptorInputExtent, Inputs.InputContentExtent, Inputs.OutputExtent, Inputs.bDynamicInputEnabled, Formats))
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX Temporal Upscaler is not ready. Skip upscaling this frame."));
		return false;
	}

	SetHistoryReset(Inputs.bResetHistory);
	SetDepthReversed(Inputs.bDepthReversed);
	SetPreExposure(Inputs.PreExposure);
	SetJitterOffset(Inputs.JitterOffset);
	SetMotionVectorScale(Inputs.MotionVectorScale);
	UpdateActiveDebugInfo(Inputs.InputRect, Inputs.OutputRect);
	return true;
}

void FMetalFXTemporalUpscalerCore::ExecuteMetalFX(FRHICommandList& CmdList, FMetalFXTemporalTextureGroup& TextureGroup)
{
	if (!CheckValidate())
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX Temporal Upscaler is invalid. Skip upscaling this frame."));
		TextureGroup.ReleaseAllTextures();
		return;
	}

	Encode(CmdList, TextureGroup);
}

void FMetalFXTemporalUpscalerCore::Encode(FRHICommandList& CmdList, FMetalFXTemporalTextureGroup& TextureGroup)
{
	FMetalCommandBuffer* CurrentCommandBuffer = GetCurrentMetalCommandBuffer(CmdList);
	if (!CurrentCommandBuffer)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX could not find the active Metal command buffer."));
		TextureGroup.ReleaseAllTextures();
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
		TextureGroup.ReleaseAllTexturesDeferred(MetalCommandBuffer);
	}
	else
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX could not find the native Metal command buffer."));
		TextureGroup.ReleaseAllTextures();
	}
#endif
#if METALFX_NATIVE
	id<MTLTexture> ColorTexture = TextureGroup.ColorTexture.GetTexture();
	id<MTLTexture> DepthTexture = TextureGroup.DepthTexture.GetTexture();
	id<MTLTexture> MotionTexture = TextureGroup.VelocityTexture.GetTexture();
	id<MTLTexture> OutputTexture = TextureGroup.OutputTexture.GetTexture();
	id<MTLCommandBuffer> MetalCommandBuffer = (__bridge id<MTLCommandBuffer>)CurrentCommandBuffer->GetMTLCmdBuffer();
	if (MetalCommandBuffer != nil)
	{
		MetalFXEncode(Resources->Scaler, MetalCommandBuffer, ColorTexture, DepthTexture, MotionTexture, OutputTexture);
		TextureGroup.ReleaseAllTexturesDeferred(MetalCommandBuffer);
	}
	else
	{
		TextureGroup.ReleaseAllTextures();
	}
#endif
}

bool FMetalFXTemporalUpscalerCore::CheckValidate() const
{
	const bool bHasResources = Resources != nullptr;
	const bool bHasScaler = bHasResources && Resources->HasScaler();
	const bool bIsValid = IsInitialized() && bHasScaler;
	if (!bIsValid)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Temporal Core is not ready."));
	}
	return bIsValid;
}
#endif
