#include "MetalFXCoreUtility.h"

//= Enabled If MetalFX Plugin Enabled
#if METALFX_PLUGIN_ENABLED

//------------Checker Utility Functions For MetalFX----------------

//------------Inner Utility Functions------------
//aka Obj-C Utility Function
//현재 파일 안에서만 작동되는 유틸리티 함수들

//(if Metal RHI) Get Metal Device Info From RHI.
//예기치 못한 RHI 정보 유실을 방지하기 위해 매번 새로 얻어오도록 처리
static id<MTLDevice> GetMetalFXDevice()
{
#if METALFX_PLUGIN_ENABLED
	if (GDynamicRHI == nullptr)
	{
		return nil;
	}

	return (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
#else
	return nil;
#endif
}

static BOOL IsSystemVersionAtLeast(NSInteger Major, NSInteger Minor = 0, NSInteger Patch = 0)
{
#if METALFX_PLUGIN_ENABLED
	NSOperatingSystemVersion RequiredVersion;
	RequiredVersion.majorVersion = Major;
	RequiredVersion.minorVersion = Minor;
	RequiredVersion.patchVersion = Patch;

	return [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:RequiredVersion];

#endif
	return NO;
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
	const BOOL bSupportsMetal3 = [MetalDevice supportsFamily:MTLGPUFamilyMetal3];
	return bHasMetalFXSpatial && bSupportsMetal3;
}

//MetalFX 는 가능한데, Temporal 이 가능한지 체크 여부 (Mac - M3 / iPhone&iPad - A17Pro)
static BOOL IsMetalFXTemporalSupported()
{
	id<MTLDevice> MetalDevice = GetMetalFXDevice();
	// 1. MetalDevice가 없음 = 아예 실패
	if (MetalDevice == nil)
	{
		return NO;
	}

#if WITH_METALFX_TARGET_MAC
	if (!IsSystemVersionAtLeast(13, 0) || ![MetalDevice supportsFamily:MTLGPUFamilyApple1])
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

	return NSClassFromString(@"MTLFXTemporalScalerDescriptor") != Nil
		&& [MTLFXTemporalScalerDescriptor supportsDevice:MetalDevice]
		&& [MetalDevice supportsFamily:MTLGPUFamilyApple9];
}

//내부 함수 - MetalFX 기동 조건 체크
static BOOL IsMetalFXSupported()
{
	return IsMetalFXSpatialSupported() || IsMetalFXTemporalSupported();
}

EMetalFXSupportedType GetMetalFXSupportedType()
{
	if (!IsMetalFXSupported())
	{
		return EMetalFXSupportedType::None;
	}
	return IsMetalFXTemporalSupported() ? EMetalFXSupportedType::Temporal : EMetalFXSupportedType::Spatial;
}

//------------Inner Utility Functions------------ (End)

//------------Outer Utility Functions------------
// aka. Obj-C Wrapper for UE Native

//어떤 타입의 MetalFX 기동이 가능한지 확인하는 유틸함수
extern "C"
int32 MetalFXQuerySupportReason()
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
	Class DescClass = NSClassFromString(@"MTLFXTemporalScalerDescriptor");
	if (DescClass == Nil)
	{
		return static_cast<int32>(Reason::NotSupportedMetalFXFrameworkMissing);
	}

	// 5. Descriptor / device 지원 여부만 확인한다.
	// 실제 scaler 생성은 첫 렌더 패스에서 texture descriptor가 준비된 뒤 수행한다.
	if (![MTLFXTemporalScalerDescriptor supportsDevice:MetalDevice])
	{
		return static_cast<int32>(Reason::NotSupportedMetalFXCreationFailed);	
	}
	
	return static_cast<int32>(Reason::Supported);	
}


//------------Outer Utility Functions------------ (End)
//------------Checker Utility Functions For MetalFX---------------- (End)


//------------MetalFX System Utility Functions--------------------

#ifdef __cplusplus
extern "C"
#endif
id<MTLFXTemporalScaler> MetalFXCreateTemporalUpscaler(id<MTLDevice> Device, const FMetalFXTemporalTextureFormatGroup Formats, int InputWidth, int InputHeight, int OutputWidth, int OutputHeight)
{
#if METALFX_PLUGIN_ENABLED
	// 이 버전 이하에선 MetalFX 자체를 못쓰도록 처리
	if (!IsMetalFXTemporalSupported())
	{
		NSLog(@"MetalFX Temporal Upscaler cannot be activated in this environment. Please check.");
		return nil;
	}

	MTLFXTemporalScalerDescriptor* Desc = [MTLFXTemporalScalerDescriptor new];
	
	Desc.inputWidth  = InputWidth;
	Desc.inputHeight = InputHeight;
	Desc.outputWidth  = OutputWidth;
	Desc.outputHeight = OutputHeight;
	
	Desc.colorTextureFormat  = (MTLPixelFormat)Formats.Color;
	Desc.depthTextureFormat  = (MTLPixelFormat)Formats.Depth;
	Desc.motionTextureFormat = (MTLPixelFormat)Formats.Motion;
	Desc.outputTextureFormat = (MTLPixelFormat)Formats.Output;
	Desc.autoExposureEnabled = true;
	
	id<MTLFXTemporalScaler> Scaler = [Desc newTemporalScalerWithDevice:Device];
	[Desc release];
	
	return Scaler;

#endif // METALFX_PLUGIN_ENABLED
	return nil;
}

#ifdef __cplusplus
extern "C"
#endif
//Note. "Output 해상도 바뀜" = Actual Output Resolution 바뀜 (=출력 해상도 바뀜) 이라서 이때는 그냥 업스케일러 다시 만들어야됨
//근데 iOS는 해상도 변경할 일이 없으니 처리 X
//해당 함수는 input이 바뀐 경우 (r.ScreenPercentage 등으로 변경된 경우) 에만 해당
bool MetalFXUpdateScalerResolution(id<MTLFXTemporalScaler> Scaler, int InputWidth, int InputHeight)
{
	if (Scaler == nil)
	{
		NSLog(@"MetalFX Upscaler is invalid. Please check.");
		return false;
	}

	//Output은 둘중 하나만 0일때 실패 판정
	if (InputWidth == 0 || InputHeight == 0)
	{
		NSLog(@"MetalFX Upscaler resolution data is invalid.");
		return false;
	}

	//입력 컨텐츠 크기 변경시
	if ([Scaler respondsToSelector:@selector(setInputContentWidth:)] && [Scaler respondsToSelector:@selector(setInputContentHeight:)])
	{
		Scaler.inputContentWidth  = InputWidth;
		Scaler.inputContentHeight = InputHeight;
	}
	else
	{
		NSLog(@"MetalFX Upscaler input resolution data is invalid.");
		return false;
	}

	return true;
}

#ifdef __cplusplus
extern "C"
#endif
void MetalFXEncode(id<MTLFXTemporalScaler> Scaler, id<MTLCommandBuffer> CmdBuffer, id<MTLTexture> Color, id<MTLTexture> Depth, id<MTLTexture> Motion, id<MTLTexture> Output)
{
#if METALFX_PLUGIN_ENABLED
	if (!Scaler || !CmdBuffer)
	{
		NSLog(@"MetalFX Upscaler or CommandBuffer is invalid.");
		return;
	}

	Scaler.colorTexture		= Color;	//Base Texture
	Scaler.depthTexture		= Depth;
	Scaler.motionTexture	= Motion;	//Motion, Velocity Texture
	Scaler.outputTexture	= Output;

	NSLog(@"[MetalFX] CmdBuffer class = %@", NSStringFromClass([CmdBuffer class]));
	NSLog(@"[MetalFX] Scaler class = %@", NSStringFromClass([Scaler class]));
	
	[Scaler encodeToCommandBuffer:CmdBuffer];
	//[CmdBuffer commit];
#endif //METALFX_PLUGIN_ENABLED
}

#ifdef __cplusplus
extern "C"
#endif
void MetalFXSetJitterOffset(id<MTLFXTemporalScaler> Scaler, float OffsetX, float OffsetY)
{
#if PLATFORM_IOS || PLATFORM_MAC
	if (!Scaler)
	{
		NSLog(@"MetalFX Upscaler is invalid.");
		return;
	}
	
	Scaler.jitterOffsetX		= OffsetX;
	Scaler.jitterOffsetY		= OffsetY;
#endif //PLATFORM_IOS || PLATFORM_MAC
}

#ifdef __cplusplus
extern "C"
#endif
void MetalFXSetMotionVectorScale(id<MTLFXTemporalScaler> Scaler, float ScaleX, float ScaleY)
{
#if METALFX_PLUGIN_ENABLED
	if (!Scaler)
	{
		NSLog(@"MetalFX Upscaler is invalid.");
		return;
	}

	Scaler.motionVectorScaleX	= ScaleX;
	Scaler.motionVectorScaleY	= ScaleY;
#endif //METALFX_PLUGIN_ENABLED
}
//------------MetalFX System Utility Functions-------------------- (End)



#endif //METALFX_PLUGIN_ENABLED
