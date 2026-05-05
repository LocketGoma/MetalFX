#include "MetalFXCoreUtility.h"
#include "MetalFXHelper.h"

#if PLATFORM_IOS || PLATFORM_MAC
#define METAL_PLATFORM_TARGET 1
#else
#define METAL_PLATFORM_TARGET 0
#endif

//= Enabled If MetalFX Plugin Enabled
#if WITH_METAL_PLATFORM

//------------Checker Utility Functions For MetalFX----------------

//(if Metal RHI) Get Metal Device Info From RHI.
static id<MTLDevice> GetMetalFXDevice()
{
#if METAL_PLATFORM_TARGET
	if (GDynamicRHI == nullptr)
	{
		return nil;
	}

	return (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
#else
	return nil;
#endif
}


static BOOL IsSystemVersionAtLeast(NSString* MinVersion)
{
	if(@available(iOS 17.0, *))
	{
		NSString* cur = [[UIDevice currentDevice] systemVersion];
		NSComparisonResult r = [cur compare:MinVersion options:NSNumericSearch];
		return (r == NSOrderedSame || r == NSOrderedDescending);
	}
	return false;
}

static BOOL IsMetalFXSupported()
{
	
}

extern "C"
int32 MetalFXQuerySupportReason()
{
	using Reason = EMetalFXSupport;

	id<MTLDevice> MetalDevice = (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();

	// 1. MetalDevice가 없음 = 아예 실패
	if (MetalDevice == nil)
	{
		return static_cast<int32>(Reason::NotSupportedOldDiviceType);
	}

	// 2. OS 버전: 최소 iOS 18 이상에서만 MetalFX 시도 (정책에 맞게 조정 가능)
	if (!IsSystemVersionAtLeast(@"18.0"))
	{
		return static_cast<int32>(Reason::NotSupportediOSOutOfDate);
	}

	// 3. MetalFX 클래스 존재 여부 (헤더/런타임 불일치 or 프레임워크 미포함) - 보통 발생하면 안됨
	Class DescClass = NSClassFromString(@"MTLFXTemporalScalerDescriptor");
	if (DescClass == Nil)
	{
		return static_cast<int32>(Reason::NotSupportedMetalFXFrameworkMissing);
	}

	// 4. 실제 TemporalScaler 생성이 가능한지 확인
	id<MTLFXTemporalScaler> TestScaler = MetalFXCreateTemporalUpscaler(MetalDevice, 64, 64, 64, 64);
	
	if (TestScaler == nil)
	{
		return static_cast<int32>(Reason::NotSupportedMetalFXCreationFailed);	
	}

	[TestScaler release];
	
	return static_cast<int32>(Reason::Supported);	
}



#ifdef __cplusplus
extern "C"
#endif
id<MTLFXTemporalScaler> MetalFXCreateTemporalUpscaler(id<MTLDevice> Device, int InputWidth, int InputHeight, int OutputWidth, int OutputHeight) 
{
#if METAL_PLATFORM_TARGET
	// 이 버전 이하에선 MetalFX 자체를 못쓰도록 처리
	if (!IsSystemVersionAtLeast(@"18.0"))
	{
		NSLog(@"MetalFX Upscaler Can not Activate this OS Version. Please Check.");
	}

	MTLFXTemporalScalerDescriptor* Desc = [MTLFXTemporalScalerDescriptor new];
	
	Desc.inputWidth  = InputWidth;
	Desc.inputHeight = InputHeight;
	Desc.outputWidth  = OutputWidth;
	Desc.outputHeight = OutputHeight;
	
	//Format은 https://developer.apple.com/documentation/Metal/MTLPixelFormat?language=objc 참고
	Desc.colorTextureFormat = MTLPixelFormat::MTLPixelFormatRGBA16Float;
	Desc.depthTextureFormat = MTLPixelFormat::MTLPixelFormatDepth32Float;
	Desc.motionTextureFormat = MTLPixelFormat::MTLPixelFormatRGBA16Float;
	Desc.outputTextureFormat = MTLPixelFormat::MTLPixelFormatRGBA16Float;
	Desc.autoExposureEnabled = true;
	
	id<MTLFXTemporalScaler> Scaler = [Desc newTemporalScalerWithDevice:Device];
	
	return Scaler;

#endif // METAL_PLATFORM_TARGET
	return nil;
}

//------------Checker Utility Functions For MetalFX---------------- (End)


//------------MetalFX System Utility Functions--------------------

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
		NSLog(@"MetalFX Upscaler is Invalidate. Please Check.");
		return false;
	}

	//Output은 둘중 하나만 0일때 실패 판정
	if (InputWidth == 0 || InputHeight == 0)
	{
		NSLog(@"MetalFX Upscaler's Resolution Data Invalid.");
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
		NSLog(@"MetalFX Upscaler's Input Resolution Data Invalid.");
		return false;
	}

	return true;
}

#ifdef __cplusplus
extern "C"
#endif
void MetalFXEncode(id<MTLFXTemporalScaler> Scaler, id<MTLCommandBuffer> CmdBuffer, id<MTLTexture> Color, id<MTLTexture> Depth, id<MTLTexture> Motion, id<MTLTexture> Output, bool bReset)
{
#if METAL_PLATFORM_TARGET
	if (!Scaler || !CmdBuffer)
	{
		NSLog(@"MetalFX Upscaler or CommandBuffer is Invalidate.");
		return;
	}

	Scaler.colorTexture		= Color;	//Base Texture
	Scaler.depthTexture		= Depth;
	Scaler.motionTexture	= Motion;	//Motion, Velocity Texture
	Scaler.outputTexture	= Output;
	if (bReset)
	{
		Scaler.reset = YES;
	}

	[Scaler encodeToCommandBuffer:CmdBuffer];
	[CmdBuffer commit];
#endif //METAL_PLATFORM_TARGET
}

#ifdef __cplusplus
extern "C"
#endif
void MetalFXSetJitterOffset(id<MTLFXTemporalScaler> Scaler, int OffsetX, int OffsetY)
{
#if PLATFORM_IOS || PLATFORM_MAC
	if (!Scaler)
	{
		NSLog(@"MetalFX Upscaler is Invalidate.");
		return;
	}
	
	Scaler.jitterOffsetX		= OffsetX;
	Scaler.jitterOffsetY		= OffsetY;
#endif //PLATFORM_IOS || PLATFORM_MAC
}

#ifdef __cplusplus
extern "C"
#endif
void MetalFXSetMotionVectorScale(id<MTLFXTemporalScaler> Scaler, int OffsetX, int OffsetY)
{
#if METAL_PLATFORM_TARGET
	if (!Scaler)
	{
		NSLog(@"MetalFX Upscaler is Invalidate.");
		return;
	}

	Scaler.motionVectorScaleX	= OffsetX;
	Scaler.motionVectorScaleY	= OffsetY;
#endif //METAL_PLATFORM_TARGET
}
//------------MetalFX System Utility Functions-------------------- (End)



#endif //WITH_METAL_PLATFORM
