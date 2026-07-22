#include "MetalFXCoreUtility.h"

//= Enabled If MetalFX Plugin Enabled
#if METALFX_PLUGIN_ENABLED
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

//------------Checker Utility Functions For MetalFX----------------

//------------Inner Utility Functions------------
//aka Obj-C Utility Function
//현재 파일 안에서만 작동되는 유틸리티 함수들

//(if Metal RHI) Get Metal Device Info From RHI.
//예기치 못한 RHI 정보 유실을 방지하기 위해 매번 새로 얻어오도록 처리
static id<MTLDevice> GetMetalFXDevice()
{
	if (GDynamicRHI == nullptr)
	{
		return nil;
	}

	return (__bridge id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
}

static BOOL IsSystemVersionAtLeast(NSInteger Major, NSInteger Minor = 0, NSInteger Patch = 0)
{
	NSOperatingSystemVersion RequiredVersion;
	RequiredVersion.majorVersion = Major;
	RequiredVersion.minorVersion = Minor;
	RequiredVersion.patchVersion = Patch;

	return [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:RequiredVersion];
}

//기본 MetalFX Upscaler인 Spatial Upscaler 가능한지 여부 (Temporal 보다는 조건이 간단함)
static BOOL IsMetalFXSpatialSupported()
{
	id<MTLDevice> MetalDevice = GetMetalFXDevice();
	if (MetalDevice == nil)
	{
		return NO;
	}

	const BOOL bHasMetalFXSpatial = NSClassFromString(@"MTLFXSpatialScalerDescriptor") != Nil;
	const BOOL bSupportsSpatialScaler = [MTLFXSpatialScalerDescriptor supportsDevice:MetalDevice];
	return bHasMetalFXSpatial && bSupportsSpatialScaler;
}

//현재 Metal 장치가 MetalFX Temporal Scaler를 지원하는지 확인
static BOOL IsMetalFXTemporalSupported()
{
	id<MTLDevice> MetalDevice = GetMetalFXDevice();
	// 1. MetalDevice가 없음 = 아예 실패
	if (MetalDevice == nil)
	{
		return NO;
	}

#if WITH_METALFX_TARGET_MAC
	const BOOL bSupportsRequiredOS = IsSystemVersionAtLeast(13, 0);
	const BOOL bSupportsAppleGPU = [MetalDevice supportsFamily:MTLGPUFamilyApple1];
	if (!bSupportsRequiredOS || !bSupportsAppleGPU)
	{
		return NO;
	}
#endif

#if WITH_METALFX_TARGET_IOS
	if (!IsSystemVersionAtLeast(18, 0))
	{
		return NO;
	}
#endif

	const BOOL bHasTemporalDescriptor = NSClassFromString(@"MTLFXTemporalScalerDescriptor") != Nil;
	const BOOL bSupportsTemporalScaler = [MTLFXTemporalScalerDescriptor supportsDevice:MetalDevice];
	return bHasTemporalDescriptor && bSupportsTemporalScaler;
}

//내부 함수 - MetalFX 기동 조건 체크
static BOOL IsMetalFXSupported()
{
	return IsMetalFXSpatialSupported() || IsMetalFXTemporalSupported();
}

// 의도된 사항: 지원 타입을 OR로 누적하지 않고 이번 실행에 사용할 타입 하나만 반환한다.
// Temporal을 지원하면 Temporal만 선택하며, Spatial은 Temporal을 지원하지 않을 때만 선택한다.
EMetalFXUpscalerType QuerySupportedMetalFXUpscalerType()
{
	if (!IsMetalFXSupported())
	{
		return EMetalFXUpscalerType::None;
	}

	return IsMetalFXTemporalSupported() ? EMetalFXUpscalerType::Temporal : EMetalFXUpscalerType::Spatial;
}

//------------Inner Utility Functions------------ (End)

//------------Outer Utility Functions------------
// aka. Obj-C Wrapper for UE Native

//어떤 타입의 MetalFX 기동이 가능한지 확인하는 유틸함수
extern "C" int32 MetalFXQuerySupportReason(EMetalFXUpscalerType SupportedUpscalerType)
{
	using Reason = EMetalFXSupportReason;

	// 1. MetalDevice가 없음 = 아예 실패
	id<MTLDevice> MetalDevice = GetMetalFXDevice();
	if (MetalDevice == nil)
	{
		return static_cast<int32>(Reason::NotSupported);
	}

	// 2. OS 버전 체크
#if WITH_METALFX_TARGET_MAC
	// Mac OS - 최소 Mac OS 13 이상에서만 MetalFX 시도 (정책에 맞게 조절 가능)
	if (!IsSystemVersionAtLeast(13, 0))
	{
		return static_cast<int32>(Reason::NotSupportedOSVersionOutOfDate);
	}
#endif

#if WITH_METALFX_TARGET_IOS
	// iOS - 최소 iOS 18 이상에서만 MetalFX 시도 (정책에 맞게 조정 가능)
	if (!IsSystemVersionAtLeast(18, 0))
	{
		return static_cast<int32>(Reason::NotSupportedOSVersionOutOfDate);
	}
#endif

	// 3. Device Type 체크
#if WITH_METALFX_TARGET_MAC
	if (![MetalDevice supportsFamily:MTLGPUFamilyApple1])
	{
		return static_cast<int32>(Reason::NotSupportedOldDeviceType);
	}
#endif

	// 4. MetalFX 클래스 존재 여부 (헤더/런타임 불일치 or 프레임워크 미포함) - 보통 발생하면 안됨
	const BOOL bHasTemporalDescriptor = NSClassFromString(@"MTLFXTemporalScalerDescriptor") != Nil;
	const BOOL bHasSpatialDescriptor = NSClassFromString(@"MTLFXSpatialScalerDescriptor") != Nil;
	if (!bHasTemporalDescriptor && !bHasSpatialDescriptor)
	{
		return static_cast<int32>(Reason::NotSupportedMetalFXFrameworkMissing);
	}

	// 5. Descriptor / device 지원 여부만 확인한다.
	// 실제 scaler 생성은 첫 렌더 패스에서 texture descriptor가 준비된 뒤 수행.
	const bool bTemporalSupported = SupportedUpscalerType == EMetalFXUpscalerType::Temporal;
	const bool bSpatialSupported = SupportedUpscalerType == EMetalFXUpscalerType::Spatial;
	if (!bTemporalSupported && !bSpatialSupported)
	{
		return static_cast<int32>(Reason::NotSupportedMetalFXCreationFailed);
	}

	return static_cast<int32>(Reason::Supported);
}

//------------Outer Utility Functions------------ (End)
//------------Checker Utility Functions For MetalFX---------------- (End)

#if METALFX_NATIVE
//------------MetalFX System Utility Functions For Native--------------------

extern "C" id<MTLFXTemporalScaler> MetalFXCreateTemporalUpscaler(id<MTLDevice> Device, const FMetalFXTemporalTextureFormatGroup& Formats, int32 InputWidth, int32 InputHeight, int32 OutputWidth, int32 OutputHeight)
{
	// 이 버전 이하에선 MetalFX 자체를 못쓰도록 처리
	if (!IsMetalFXTemporalSupported())
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX TemporalScaler creation skipped because the environment is unsupported."));
		return nil;
	}

	if (Device == nil)
	{
		return nil;
	}

	MTLFXTemporalScalerDescriptor* Desc = [MTLFXTemporalScalerDescriptor new];
	if (Desc == nil)
	{
		return nil;
	}

	if (![MTLFXTemporalScalerDescriptor supportsDevice:Device])
	{
		[Desc release];
		return nil;
	}

	Desc.inputWidth = InputWidth;
	Desc.inputHeight = InputHeight;
	Desc.outputWidth = OutputWidth;
	Desc.outputHeight = OutputHeight;
	Desc.colorTextureFormat = static_cast<MTLPixelFormat>(Formats.Color);
	Desc.depthTextureFormat = static_cast<MTLPixelFormat>(Formats.Depth);
	Desc.motionTextureFormat = static_cast<MTLPixelFormat>(Formats.Motion);
	Desc.outputTextureFormat = static_cast<MTLPixelFormat>(Formats.Output);
	Desc.autoExposureEnabled = true;

	id<MTLFXTemporalScaler> Scaler = [Desc newTemporalScalerWithDevice:Device];
	[Desc release];
	return Scaler;
}

// Note. "Output 해상도 바뀜" = Actual Output Resolution 바뀜 (=출력 해상도 바뀜) 이라서 이때는 그냥 업스케일러 다시 만들어야됨
// 해당 함수는 input이 바뀐 경우 (r.ScreenPercentage 등으로 변경된 경우) 에만 해당
extern "C" void MetalFXUpdateScalerResolution(id<MTLFXTemporalScaler> Scaler, int32 InputWidth, int32 InputHeight)
{
	if (Scaler == nil)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX cannot update input content because the TemporalScaler is invalid."));
		return;
	}

	// Output은 둘중 하나만 0일때 실패 판정
	if (InputWidth <= 0 || InputHeight <= 0)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX cannot update an empty input-content extent (%dx%d)."), InputWidth, InputHeight);
		return;
	}

	// 입력 컨텐츠 크기 변경시
	// TODO: 지원 OS 하한을 낮춰 해당 속성이 없는 런타임까지 대응할 경우, 갱신을 생략하지 않는 명시적인 재생성 fallback을 추가한다.
	Scaler.inputContentWidth = InputWidth;
	Scaler.inputContentHeight = InputHeight;
}

extern "C" void MetalFXEncode(id<MTLFXTemporalScaler> Scaler, id<MTLCommandBuffer> CmdBuffer, id<MTLTexture> Color, id<MTLTexture> Depth, id<MTLTexture> Motion, id<MTLTexture> Output)
{
	if (Scaler == nil || CmdBuffer == nil)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX native encode received an invalid TemporalScaler or command buffer."));
		return;
	}

	@autoreleasepool
	{
		Scaler.colorTexture = Color; // Base Texture
		Scaler.depthTexture = Depth;
		Scaler.motionTexture = Motion; // Motion, Velocity Texture
		Scaler.outputTexture = Output;
		[Scaler encodeToCommandBuffer:CmdBuffer];
	}
}

extern "C" void MetalFXSetJitterOffset(id<MTLFXTemporalScaler> Scaler, float OffsetX, float OffsetY)
{
	if (Scaler == nil)
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX ignored jitter because the TemporalScaler is invalid."));
		return;
	}

	Scaler.jitterOffsetX = OffsetX;
	Scaler.jitterOffsetY = OffsetY;
}

extern "C" void MetalFXSetMotionVectorScale(id<MTLFXTemporalScaler> Scaler, float ScaleX, float ScaleY)
{
	if (Scaler == nil)
	{
		UE_LOG(LogMetalFX, Verbose, TEXT("MetalFX ignored motion-vector scale because the TemporalScaler is invalid."));
		return;
	}

	Scaler.motionVectorScaleX = ScaleX;
	Scaler.motionVectorScaleY = ScaleY;
}

//------------MetalFX System Utility Functions For Native-------------------- (End)
#endif // METALFX_NATIVE

#endif // METALFX_PLUGIN_ENABLED
