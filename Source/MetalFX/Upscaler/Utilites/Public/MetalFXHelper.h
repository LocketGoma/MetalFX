#pragma once
#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "MetalFXSettings.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"

#if METALFX_PLUGIN_ENABLED && METALFX_METALCPP
#include <Metal/Metal.hpp>
#elif METALFX_PLUGIN_ENABLED && METALFX_NATIVE
#import <Metal/Metal.h>
#endif

class FRDGTexture;
class FSceneViewFamily;

DECLARE_LOG_CATEGORY_EXTERN(LogMetalFX, Log, All);

// QualityMode에 대응하는 이름, Primary 입력 비율, Native 해상도 강제 여부를 반환한다.
// Min은 빌드 설정에 따라 테스트용 1% 또는 가장 낮은 정식 프리셋으로 변환한다. 범위 초과 시 입력 기준 100%인 "UltraQuality" 로 진행한다.
inline FMetalFXQualitySettings GetMetalFXQualitySettings(EMetalFXQualityMode QualityMode)
{
	if (QualityMode == EMetalFXQualityMode::Min)
	{
#if !UE_BUILD_SHIPPING && METALFX_DEBUG
		return { TEXT("Min"), 0.01f, false };
#else
		return GetMetalFXQualitySettings(static_cast<EMetalFXQualityMode>(static_cast<int32>(EMetalFXQualityMode::Min) - 1));
#endif
	}

	switch (QualityMode)
	{
	case EMetalFXQualityMode::NativeAA:
		return { TEXT("NativeAA"), 1.0f, true };
	case EMetalFXQualityMode::UltraQuality:
		return { TEXT("UltraQuality"), 1.0f, false };
	case EMetalFXQualityMode::Quality:
		return { TEXT("Quality"), 0.667f, false };
	case EMetalFXQualityMode::Balanced:
		return { TEXT("Balanced"), 0.5f, false };
	case EMetalFXQualityMode::Performance:
		return { TEXT("Performance"), 0.42f, false };
	case EMetalFXQualityMode::UltraPerformance:
		// MetalFX TemporalScaler는 각 축 (X,Y축)당 3배를 초과하는 업스케일링을 지원하지 않음.
		return { TEXT("UltraPerformance"), 0.34f, false };
	default:
		return { TEXT("UltraQuality"), 1.0f, false };
	}
}

// QualityMode의 Primary 입력 비율을 r.ScreenPercentage 형식인 0~100 단위 값으로 변환한다.
// 예: Quality의 0.667 비율은 66.7을 반환한다.
inline float ConvertMetalFXQualityModeToScreenPercentage(EMetalFXQualityMode QualityMode)
{
	return GetMetalFXQualitySettings(QualityMode).GetScreenPercentage();
}

// 콘솔 변수에서 읽은 정수형 QualityMode를 Screen Percentage 값으로 변환한다.
inline float ConvertMetalFXQualityModeToScreenPercentage(int32 QualityMode)
{
	return ConvertMetalFXQualityModeToScreenPercentage(static_cast<EMetalFXQualityMode>(QualityMode));
}

// QualityMode의 Primary 입력 비율을 Unreal 해상도 비율 형식인 0~1 단위 값으로 반환한다.
// 이 값은 MetalFX 출력 목표를 기준으로 한 입력 비율이며, 항상 실제 디스플레이 해상도를 기준으로 하지는 않는다.
inline float ConvertMetalFXQualityModeToResolutionFraction(EMetalFXQualityMode QualityMode)
{
	return GetMetalFXQualitySettings(QualityMode).GetPrimaryResolutionFraction();
}

// QualityMode를 해상도 비율 수치로 변환.
inline float ConvertMetalFXQualityModeToResolutionFraction(int32 QualityMode)
{
	return ConvertMetalFXQualityModeToResolutionFraction(static_cast<EMetalFXQualityMode>(QualityMode));
}

// Temporal Upscaler 인터페이스에 알릴 수 있는 최소 입력 해상도 비율을 반환한다.
// 비 Shipping 빌드에서는 극단값 테스트를 위해 Min 프리셋이 1% 등 극단적인 수치로 설정될 수 있다.
// - 단, MetalFX 기본 업스케일링 제한인 x3을 초과할 수 있는 수치가 되는 경우, 런타임 에러가 발생할 수 있음.
inline float GetMetalFXMinUpscaleResolutionFraction()
{
	return ConvertMetalFXQualityModeToResolutionFraction(EMetalFXQualityMode::Min);
}

// Temporal Upscaler 인터페이스에 알릴 수 있는 최대 입력 해상도 비율을 반환.
METALFX_API float GetMetalFXMaxUpscaleResolutionFraction();

// 실행 중 QualityMode 또는 AutoScaling 설정이 바뀌었을 때 현재 Screen Percentage 상태를 갱신한다.
// 일반 빌드는 MetalFX가 소유한 r.ScreenPercentage에 반영하고, METALFX_DEBUG 빌드는 외부 입력값을 보존한다.
METALFX_API void ApplyMetalFXQualityModeToScreenPercentage(EMetalFXQualityMode QualityMode);

// 현재 ViewFamily의 Dynamic Resolution 및 Screen Percentage 기준값을 읽어 MetalFX 입력/출력 비율을 적용한다.
// 성공하면 ViewFamily.SecondaryViewFraction을 갱신하며, OutDebugInfo가 있으면 실제 계산 결과도 함께 기록한다.
METALFX_API bool ApplyMetalFXScreenPercentageToViewFamily(FSceneViewFamily& ViewFamily, EMetalFXQualityMode QualityMode, FMetalFXResolutionDebugInfo* OutDebugInfo = nullptr);

// MetalFX가 비활성화된 상태에서도 디버그 화면에 표시할 현재 설정 기준의 예상 해상도 정보를 계산한다. (읽기전용, 디버그)
METALFX_API FMetalFXResolutionDebugInfo GetConfiguredMetalFXResolutionDebugInfo(const FSceneViewFamily& ViewFamily, EMetalFXQualityMode QualityMode);

// MetalFX Screen Percentage 제어 상태를 종료하고 내부에 저장한 활성화 상태를 초기화한다.
// METALFX_DEBUG가 비활성화 상태라면, MetalFX 활성화 직전의 r.ScreenPercentage 값과 우선순위를 복구한다.
METALFX_API void RestoreMetalFXScreenPercentage();

// 텍스쳐 포멧 그룹
// Texture formats that require scaler recreation when they change.
using FMetalFXPixelFormat = uint64;
struct FMetalFXTemporalTextureFormatGroup
{
	FMetalFXPixelFormat Color = 0;
	FMetalFXPixelFormat Depth = 0;
	FMetalFXPixelFormat Motion = 0;
	FMetalFXPixelFormat Output = 0;

	bool IsReady() const
	{
		return Color != 0 && Depth != 0 && Motion != 0 && Output != 0;
	}

	bool operator==(const FMetalFXTemporalTextureFormatGroup& Other) const
	{
		const bool bColorMatches = Color == Other.Color;
		const bool bDepthMatches = Depth == Other.Depth;
		const bool bMotionMatches = Motion == Other.Motion;
		const bool bOutputMatches = Output == Other.Output;
		return bColorMatches && bDepthMatches && bMotionMatches && bOutputMatches;
	}

	bool operator!=(const FMetalFXTemporalTextureFormatGroup& Other) const
	{
		return !(*this == Other);
	}
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

// Native Metal resources exist only where the plugin is compiled.
#if METALFX_PLUGIN_ENABLED

#if METALFX_METALCPP
//텍스쳐 그룹
//2차 변환 처리를 위한 중간 구조체
// Move-only Metal-cpp texture view with optional ownership.
typedef struct FMetalFXCppTextureView
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
	}

	MTL::Texture* GetTexture() const
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
	}

	void ReleaseTextureDeferred(MTL::CommandBuffer* CommandBuffer)
	{
		if (bNeedsRelease && Texture != nullptr)
		{
			MTL::Texture* TextureToRelease = Texture;

			Texture = nullptr;
			bNeedsRelease = false;

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
			return;
		}

		Texture = nullptr;
		bNeedsRelease = false;
	}

	bool IsValid() const
	{
		return Texture != nullptr;
	}

private:
	void MoveFrom(FMetalFXCppTextureView& Other)
	{
		Texture = Other.Texture;
		bNeedsRelease = Other.bNeedsRelease;

		Other.Texture = nullptr;
		Other.bNeedsRelease = false;
	}

	MTL::Texture* Texture = nullptr;
	bool bNeedsRelease = false;
} FMetalFXTextureView;
#endif

#if METALFX_NATIVE
//2차 변환 처리를 위한 중간 구조체
// Move-only Objective-C texture view with optional ownership.
typedef struct FMetalFXObjCTextureView
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
	}

	id<MTLTexture> GetTexture() const
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
	}

	void ReleaseTextureDeferred(id<MTLCommandBuffer> CommandBuffer)
	{
		if (bNeedsRelease && Texture != nil)
		{
			id<MTLTexture> TextureToRelease = Texture;

			Texture = nil;
			bNeedsRelease = false;

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
			return;
		}

		Texture = nil;
		bNeedsRelease = false;
	}

	bool IsValid() const
	{
		return Texture != nil;
	}

private:
	void MoveFrom(FMetalFXObjCTextureView& Other)
	{
		Texture = Other.Texture;
		bNeedsRelease = Other.bNeedsRelease;

		Other.Texture = nil;
		Other.bNeedsRelease = false;
	}

	id<MTLTexture> Texture = nil;
	bool bNeedsRelease = false;
} FMetalFXTextureView;
#endif

// Temporal-only texture group; Spatial intentionally uses a smaller group.
struct FMetalFXTemporalTextureGroup
{
	FMetalFXTemporalTextureGroup() = default;
	FMetalFXTemporalTextureGroup(FMetalFXTemporalTextureGroup&&) noexcept = default;
	FMetalFXTemporalTextureGroup& operator=(FMetalFXTemporalTextureGroup&&) noexcept = default;
	~FMetalFXTemporalTextureGroup() = default;

	void ReleaseAllTextures()
	{
		ColorTexture.ReleaseTexture();
		DepthTexture.ReleaseTexture();
		VelocityTexture.ReleaseTexture();
		OutputTexture.ReleaseTexture();
	}

#if METALFX_METALCPP
	void ReleaseAllTexturesDeferred(MTL::CommandBuffer* CommandBuffer)
	{
		ColorTexture.ReleaseTextureDeferred(CommandBuffer);
		DepthTexture.ReleaseTextureDeferred(CommandBuffer);
		VelocityTexture.ReleaseTextureDeferred(CommandBuffer);
		OutputTexture.ReleaseTextureDeferred(CommandBuffer);
	}

	FMetalFXCppTextureView ColorTexture;
	FMetalFXCppTextureView DepthTexture;
	FMetalFXCppTextureView VelocityTexture;
	FMetalFXCppTextureView OutputTexture;
#endif

#if METALFX_NATIVE
	void ReleaseAllTexturesDeferred(id<MTLCommandBuffer> CommandBuffer)
	{
		ColorTexture.ReleaseTextureDeferred(CommandBuffer);
		DepthTexture.ReleaseTextureDeferred(CommandBuffer);
		VelocityTexture.ReleaseTextureDeferred(CommandBuffer);
		OutputTexture.ReleaseTextureDeferred(CommandBuffer);
	}

	FMetalFXObjCTextureView ColorTexture;
	FMetalFXObjCTextureView DepthTexture;
	FMetalFXObjCTextureView VelocityTexture;
	FMetalFXObjCTextureView OutputTexture;
#endif
};

// Spatial-only texture group with no temporal history resources.
struct FMetalFXSpatialTextureGroup
{
	FMetalFXSpatialTextureGroup() = default;
	FMetalFXSpatialTextureGroup(FMetalFXSpatialTextureGroup&&) noexcept = default;
	FMetalFXSpatialTextureGroup& operator=(FMetalFXSpatialTextureGroup&&) noexcept = default;
	~FMetalFXSpatialTextureGroup() = default;

	void ReleaseAllTextures()
	{
		ColorTexture.ReleaseTexture();
		OutputTexture.ReleaseTexture();
	}

#if METALFX_METALCPP
	void ReleaseAllTexturesDeferred(MTL::CommandBuffer* CommandBuffer)
	{
		ColorTexture.ReleaseTextureDeferred(CommandBuffer);
		OutputTexture.ReleaseTextureDeferred(CommandBuffer);
	}
#endif

#if METALFX_NATIVE
	void ReleaseAllTexturesDeferred(id<MTLCommandBuffer> CommandBuffer)
	{
		ColorTexture.ReleaseTextureDeferred(CommandBuffer);
		OutputTexture.ReleaseTextureDeferred(CommandBuffer);
	}
#endif

	FMetalFXTextureView ColorTexture;
	FMetalFXTextureView OutputTexture;
};

#endif
