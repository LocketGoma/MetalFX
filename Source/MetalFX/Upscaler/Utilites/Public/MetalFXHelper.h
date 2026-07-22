#pragma once
#include "CoreMinimal.h"
#include "MetalFXSettings.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "SceneView.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIContext.h"
#include "MetalCommandBuffer.h"
#include "MetalRHIUtility.h"
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogMetalFX, Log, All);

class FRDGTexture;

//= Metal이 지원되는 Apple 기기인지 아닌지
enum class EMetalSupportDevice : uint8
{
	Supported,
	NotSupported
};

//= MetalFX 가능한 환경인지 여부
enum class EMetalFXSupportReason : uint8
{
	Supported,
	NotSupported,
	NotSupportedOldDeviceType,
	NotSupportedOSVersionOutOfDate,
	NotSupportedMetalFXFrameworkMissing,
	NotSupportedMetalFXCreationFailed,
};

struct FMetalFXQualitySettings
{
	// Human-readable preset name used by logs and the on-screen debug display.
	const TCHAR* Name = TEXT("Balanced");

	// Primary input fraction relative to the MetalFX Secondary output target.
	float RelativeScale = 0.5f;

	// Legacy absolute Primary Screen Percentage used when engine-based scaling is disabled.
	float AbsoluteScreenPercentage = 50.0f;

	// Forces native-display input/output instead of composing with the engine base fraction.
	bool bForceNativeResolution = false;

	// Returns the Primary fraction consumed by Unreal's screen-percentage pipeline.
	// A returned value of 1.0 means 100% of the Secondary output target; it only
	// means 100% of the physical display when bForceNativeResolution also forces
	// that Secondary target to native resolution.
	float GetPrimaryResolutionFraction(bool bAutoScalingFromEngine) const
	{
		return (bForceNativeResolution || bAutoScalingFromEngine) ? RelativeScale : AbsoluteScreenPercentage / 100.0f;
	}
};

inline FMetalFXQualitySettings GetMetalFXQualitySettings(EMetalFXQualityMode QualityMode)
{
	if (QualityMode == EMetalFXQualityMode::Min)
	{
#if !UE_BUILD_SHIPPING
		return { TEXT("Min"), 0.01f, 1.0f, false };
#else
		return GetMetalFXQualitySettings(static_cast<EMetalFXQualityMode>(static_cast<int32>(EMetalFXQualityMode::Min) - 1));
#endif
	}

	switch (QualityMode)
	{
	case EMetalFXQualityMode::NativeAA:
		return { TEXT("NativeAA"), 1.0f, 100.0f, true };
	case EMetalFXQualityMode::UltraQuality:
		return { TEXT("UltraQuality"), 1.0f, 100.0f, false };
	case EMetalFXQualityMode::Quality:
		return { TEXT("Quality"), 0.667f, 66.7f, false };
	case EMetalFXQualityMode::Balanced:
		return { TEXT("Balanced"), 0.5f, 50.0f, false };
	case EMetalFXQualityMode::Performance:
		return { TEXT("Performance"), 0.42f, 42.0f, false };
	case EMetalFXQualityMode::UltraPerformance:
		// MetalFX TemporalScaler does not support upscaling greater than 3x per dimension.
		return { TEXT("UltraPerformance"), 0.34f, 34.0f, false };
	default:
		return { TEXT("Balanced"), 0.5f, 50.0f, false };
	}
}

inline float ConvertMetalFXQualityModeToScreenPercentage(EMetalFXQualityMode QualityMode)
{
	return GetMetalFXQualitySettings(QualityMode).AbsoluteScreenPercentage;
}

inline float ConvertMetalFXQualityModeToScreenPercentage(int32 QualityMode)
{
	return ConvertMetalFXQualityModeToScreenPercentage(static_cast<EMetalFXQualityMode>(QualityMode));
}

inline float ConvertMetalFXQualityModeToResolutionFraction(EMetalFXQualityMode QualityMode)
{
	return GetMetalFXQualitySettings(QualityMode).RelativeScale;
}

inline float ConvertMetalFXQualityModeToResolutionFraction(int32 QualityMode)
{
	return ConvertMetalFXQualityModeToResolutionFraction(static_cast<EMetalFXQualityMode>(QualityMode));
}

inline float GetMetalFXMinUpscaleResolutionFraction()
{
	return ConvertMetalFXQualityModeToResolutionFraction(EMetalFXQualityMode::Min);
}

inline float GetMetalFXMaxUpscaleResolutionFraction()
{
#if METALFX_DEBUG
	// Temporarily expose Unreal's full Primary resolution range so values above
	// 100% can be exercised while validating MetalFX supersampling behavior.
	return ISceneViewFamilyScreenPercentage::kMaxResolutionFraction;
#else
	return ConvertMetalFXQualityModeToResolutionFraction(EMetalFXQualityMode::NativeAA);
#endif
}

// Returns the primary input-to-output scale as a percentage.
inline float CalculateMetalFXScreenPercentage(const FIntRect& InputRect, const FIntRect& OutputRect)
{
	if (OutputRect.Width() <= 0 || OutputRect.Height() <= 0)
	{
		return 100.0f;
	}

	const float WidthFraction = static_cast<float>(InputRect.Width()) / static_cast<float>(OutputRect.Width());
	const float HeightFraction = static_cast<float>(InputRect.Height()) / static_cast<float>(OutputRect.Height());
	return FMath::Min(WidthFraction, HeightFraction) * 100.0f;
}

//텍스쳐 포멧 그룹
using FMetalFXPixelFormat = uint64_t;
struct FMetalFXTemporalTextureFormatGroup
{
	FMetalFXPixelFormat Color = 0;
	FMetalFXPixelFormat Depth = 0;
	FMetalFXPixelFormat Motion = 0;
	FMetalFXPixelFormat Output = 0;

	FMetalFXTemporalTextureFormatGroup() = default;
	
	FMetalFXTemporalTextureFormatGroup& operator =(const FMetalFXTemporalTextureFormatGroup& InFormats)
	{
		if (this != &InFormats)
		{
			bIsChanged = (Color != InFormats.Color || Depth != InFormats.Depth || Motion != InFormats.Motion || Output != InFormats.Output);
			
			Color = InFormats.Color;
			Depth = InFormats.Depth;
			Motion = InFormats.Motion;
			Output = InFormats.Output;
		}
		
		return *this;
	}
	
	FMetalFXTemporalTextureFormatGroup(const FMetalFXTemporalTextureFormatGroup& InFormats)
	{
		bIsChanged = (Color != InFormats.Color || Depth != InFormats.Depth || Motion != InFormats.Motion || Output != InFormats.Output);
		
		Color = InFormats.Color;
		Depth = InFormats.Depth;
		Motion = InFormats.Motion;
		Output = InFormats.Output;
	}
	
	bool UpdateChangeState(const FMetalFXTemporalTextureFormatGroup& InFormats)
	{
		bIsChanged = IsChanged(InFormats);
		return bIsChanged;
	}
	
	bool GetIsChanged() const
	{
		return bIsChanged;
	}

	bool IsChanged(const FMetalFXTemporalTextureFormatGroup& InFormats) const
	{
		return Color != InFormats.Color || Depth != InFormats.Depth || Motion != InFormats.Motion || Output != InFormats.Output;
	}

	bool IsReady() const
	{
		return Color != 0 && Depth != 0 && Motion != 0 && Output != 0;
	}
	
	void ResetChangeState()
	{
		bIsChanged = false;
	}
	
private:
	bool bIsChanged = false;
};

struct FMetalFXSpatialTextureFormatGroup
{
	FMetalFXPixelFormat Color = 0;
	FMetalFXPixelFormat Output = 0;

	bool IsReady() const
	{
		return Color != 0 && Output != 0;
	}

	bool operator==(const FMetalFXSpatialTextureFormatGroup& Other) const
	{
		return Color == Other.Color && Output == Other.Output;
	}

	bool operator!=(const FMetalFXSpatialTextureFormatGroup& Other) const
	{
		return !(*this == Other);
	}
};

BEGIN_SHADER_PARAMETER_STRUCT(FMetalFXTemporalPassParameters, )
	RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(DepthTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(VelocityTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::UAVMask)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FMetalFXSpatialPassParameters, )
	RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::UAVMask)
END_SHADER_PARAMETER_STRUCT()

//MetalFX 활성화때만 사용
#if METALFX_PLUGIN_ENABLED
namespace MTL
{
	class Texture;
	class CommandQueue;
	class CommandBuffer;
	class Device;
}

namespace MTLFX
{
	class SpatialScaler;
	class SpatialScalerDescriptor;
	class TemporalScaler;
	class TemporalScalerDescriptor;
}


//텍스쳐 그룹
#if METALFX_METALCPP
//2차 변환 처리를 위한 중간 구조체
struct FMetalFXCppTextureView
{	
	FMetalFXCppTextureView() = default;
	FMetalFXCppTextureView(const FMetalFXCppTextureView&) = delete;
	FMetalFXCppTextureView& operator=(const FMetalFXCppTextureView&) = delete;

	FMetalFXCppTextureView(FMetalFXCppTextureView&& Other) noexcept
	{
		MoveFrom(Other);
	}

	FMetalFXCppTextureView& operator=(FMetalFXCppTextureView&& Other) noexcept
	{
		if (this != &Other)
		{
			ReleaseTexture();
			MoveFrom(Other);
		}
		return *this;
	}

	~FMetalFXCppTextureView()
	{
		ReleaseTexture();
	}

public:
	void SetTexture(MTL::Texture* inTexture, bool isNeedRelease = false)
	{
		ReleaseTexture();
		
		Texture = inTexture;
		bNeedsRelease = isNeedRelease;
		bIsValid = inTexture != nullptr;
	}
	
	MTL::Texture* GetTexture()
	{
		return Texture;
	}
	
	//릴리즈 + 텍스쳐뷰 변수 초기화 (아래와는 다른 스텝임)
	void ReleaseTexture()
	{
		if (bNeedsRelease && Texture != nullptr)
		{
			Texture->release();
		}
		
		Texture = nullptr;
		bNeedsRelease = false;
		bIsValid = false;
	}
	
	void ReleaseTextureDeferred(MTL::CommandBuffer* CommandBuffer)
	{
		if (bNeedsRelease && Texture != nullptr)
		{
			MTL::Texture* TextureToRelease = Texture;
			
			Texture = nullptr;
			bNeedsRelease = false;
			bIsValid = false;
			
			if (CommandBuffer != nullptr)
			{
				//커맨드 버퍼가 유효하면, 커맨드 버퍼 완료 시 릴리즈 처리
				MTL::HandlerFunction Handler = [TextureToRelease](MTL::CommandBuffer* InBuffer)
				{
					TextureToRelease->release();
				};
				
				CommandBuffer->addCompletedHandler(Handler);
			}
			else
			{
				TextureToRelease->release();
			}
		}
	}
	
	bool IsValid() const
	{
		return bIsValid && Texture != nullptr;
	}
	
private:
	void MoveFrom(FMetalFXCppTextureView& Other)
	{
		Texture = Other.Texture;
		bNeedsRelease = Other.bNeedsRelease;
		bIsValid = Other.bIsValid;

		Other.Texture = nullptr;
		Other.bNeedsRelease = false;
		Other.bIsValid = false;
	}

	MTL::Texture* Texture = nullptr;
	bool bNeedsRelease = false;
	bool bIsValid = false;
};

#endif

#if METALFX_NATIVE
//2차 변환 처리를 위한 중간 구조체
struct FMetalFXObjCTextureView
{	
	FMetalFXObjCTextureView() = default;
	FMetalFXObjCTextureView(const FMetalFXObjCTextureView&) = delete;
	FMetalFXObjCTextureView& operator=(const FMetalFXObjCTextureView&) = delete;

	FMetalFXObjCTextureView(FMetalFXObjCTextureView&& Other) noexcept
	{
		MoveFrom(Other);
	}

	FMetalFXObjCTextureView& operator=(FMetalFXObjCTextureView&& Other) noexcept
	{
		if (this != &Other)
		{
			ReleaseTexture();
			MoveFrom(Other);
		}
		return *this;
	}

	~FMetalFXObjCTextureView()
	{
		ReleaseTexture();
	}
	
	void SetTexture(id<MTLTexture> inTexture, bool isNeedRelease = false)
	{
		ReleaseTexture();
		
		Texture = inTexture;
		bNeedsRelease = isNeedRelease;
		bIsValid = inTexture != nil;
	}
	
	id<MTLTexture> GetTexture()
	{
		return Texture;
	}
	
	void ReleaseTexture()
	{
		if (bNeedsRelease && Texture != nil)
		{
			[Texture release];
		}
		
		Texture = nil;
		bNeedsRelease = false;
		bIsValid = false;
	}
	
	void ReleaseTextureDeferred(id<MTLCommandBuffer> CommandBuffer)
	{
		if (bNeedsRelease && Texture != nil)
		{
			id<MTLTexture> TextureToRelease = Texture;
			
			Texture = nil;
			bNeedsRelease = false;
			bIsValid = false;
			
			if (CommandBuffer != nil)
			{
				[CommandBuffer addCompletedHandler:^(id<MTLCommandBuffer>)
				{
					[TextureToRelease release];
				}];
			}
			else
			{
				[TextureToRelease release];
			}
		}
	}
	
	bool IsValid() const
	{
		return bIsValid && Texture != nil;
	}
	
private:
	void MoveFrom(FMetalFXObjCTextureView& Other)
	{
		Texture = Other.Texture;
		bNeedsRelease = Other.bNeedsRelease;
		bIsValid = Other.bIsValid;

		Other.Texture = nil;
		Other.bNeedsRelease = false;
		Other.bIsValid = false;
	}

	id<MTLTexture> Texture = nil;
	bool bNeedsRelease = false;
	bool bIsValid = false;
};
#endif

#if METALFX_METALCPP
using FMetalFXTextureView = FMetalFXCppTextureView;
#elif METALFX_NATIVE
using FMetalFXTextureView = FMetalFXObjCTextureView;
#endif

// Temporal 전용 텍스처 그룹. Spatial Core에서 재사용하지 않는다.
struct FMetalFXTemporalTextureGroup
{
	FMetalFXTemporalTextureGroup() = default;
	
	FMetalFXTemporalTextureGroup(FMetalFXTemporalTextureGroup&& Other) noexcept
	{
		ColorTexture 	= MoveTemp(Other.ColorTexture);
		DepthTexture 	= MoveTemp(Other.DepthTexture);
		VelocityTexture = MoveTemp(Other.VelocityTexture);
		OutputTexture 	= MoveTemp(Other.OutputTexture);

		bReleased = Other.bReleased;
		Other.bReleased = true; // 포인터가 옮겨진것 뿐이라, 원본 그룹에서 소멸 처리하면 꼬임
	}
	
	~FMetalFXTemporalTextureGroup()
	{
		if (!bReleased)
		{
			ReleaseAllTexture();
		}
	}
	
public:
	void ReleaseAllTexture()
	{
		TextureRelease(ColorTexture);
		TextureRelease(DepthTexture);
		TextureRelease(VelocityTexture);
		TextureRelease(OutputTexture);
		
		bReleased = true;
	}
	
#if METALFX_METALCPP
	void TextureRelease(FMetalFXCppTextureView& TargetTexture)
	{
		TargetTexture.ReleaseTexture();
	}
	
	void ReleaseTextureDeferred(FMetalFXCppTextureView& TargetTexture, MTL::CommandBuffer* CommandBuffer)
	{
		TargetTexture.ReleaseTextureDeferred(CommandBuffer);
	}
	
	void ReleaseAllTextureDeferred(MTL::CommandBuffer* CommandBuffer)
	{
		ReleaseTextureDeferred(ColorTexture, CommandBuffer);
		ReleaseTextureDeferred(DepthTexture, CommandBuffer);
		ReleaseTextureDeferred(VelocityTexture, CommandBuffer);
		ReleaseTextureDeferred(OutputTexture, CommandBuffer);
		
		bReleased = true;
	}
	
	FMetalFXCppTextureView ColorTexture;
	FMetalFXCppTextureView DepthTexture;
	FMetalFXCppTextureView VelocityTexture;
	FMetalFXCppTextureView OutputTexture;
#endif
	
#if METALFX_NATIVE
	void TextureRelease(FMetalFXObjCTextureView& TargetTexture)
	{
		TargetTexture.ReleaseTexture();
	}
	
	void ReleaseTextureDeferred(FMetalFXObjCTextureView& TargetTexture, id<MTLCommandBuffer> CommandBuffer)
	{
		TargetTexture.ReleaseTextureDeferred(CommandBuffer);
	}
	
	void ReleaseAllTextureDeferred(id<MTLCommandBuffer> CommandBuffer)
	{
		ReleaseTextureDeferred(ColorTexture, CommandBuffer);
		ReleaseTextureDeferred(DepthTexture, CommandBuffer);
		ReleaseTextureDeferred(VelocityTexture, CommandBuffer);
		ReleaseTextureDeferred(OutputTexture, CommandBuffer);
		
		bReleased = true;
	}
	
	FMetalFXObjCTextureView ColorTexture;
	FMetalFXObjCTextureView DepthTexture;
	FMetalFXObjCTextureView VelocityTexture;
	FMetalFXObjCTextureView OutputTexture;
#endif
	
	bool bReleased = false;
};

// Spatial 전용 텍스처 그룹. Temporal history 입력을 의도적으로 포함하지 않는다.
struct FMetalFXSpatialTextureGroup
{
	FMetalFXSpatialTextureGroup() = default;

	FMetalFXSpatialTextureGroup(FMetalFXSpatialTextureGroup&& Other) noexcept
	{
		ColorTexture = MoveTemp(Other.ColorTexture);
		OutputTexture = MoveTemp(Other.OutputTexture);

		bReleased = Other.bReleased;
		Other.bReleased = true;
	}

	~FMetalFXSpatialTextureGroup()
	{
		if (!bReleased)
		{
			ReleaseAllTexture();
		}
	}

	void ReleaseAllTexture()
	{
		ColorTexture.ReleaseTexture();
		OutputTexture.ReleaseTexture();
		bReleased = true;
	}

#if METALFX_METALCPP
	void ReleaseAllTextureDeferred(MTL::CommandBuffer* CommandBuffer)
	{
		ColorTexture.ReleaseTextureDeferred(CommandBuffer);
		OutputTexture.ReleaseTextureDeferred(CommandBuffer);
		bReleased = true;
	}
#elif METALFX_NATIVE
	void ReleaseAllTextureDeferred(id<MTLCommandBuffer> CommandBuffer)
	{
		ColorTexture.ReleaseTextureDeferred(CommandBuffer);
		OutputTexture.ReleaseTextureDeferred(CommandBuffer);
		bReleased = true;
	}
#endif

	FMetalFXTextureView ColorTexture;
	FMetalFXTextureView OutputTexture;
	bool bReleased = false;
};

#endif
