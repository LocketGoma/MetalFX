#pragma once

#include "CoreMinimal.h"

//MetalFX Type 0 = Obj-C Wrapper
//iOS Version이 급격히 바뀐 경우 등에 사용

//MetalFX Type 1 = MetalCPP Wrapper
//안정된 디버그가 필요한 경우 등의 상황에서 사용

DECLARE_LOG_CATEGORY_EXTERN(LogMetalFX, Verbose, All);

#define METALFX_METALCPP 1

//=Metal이 지원되는 Apple 기기인지 아닌지
enum class EMetalSupportDevice : uint8
{
	Supported,
	NotSupported
};

//=MetalFX 가능한 환경인지 여부
enum class EMetalFXSupportReason : uint8
{
	Supported,
	NotSupported,
	NotSupportedOldDiviceType,
	NotSupportediOSOutOfDate,
	NotSupportedMetalFXFrameworkMissing,
	NotSupportedMetalFXCreationFailed,
};

//=MetalFX 관련 최종 판단 (SpatialType / TemporalType 인 경우에만 MetalFX 정상 작동)
enum class EMetalFXServiceReason : uint8
{
	//Something Wrong
	Error,
	
	//=aka Intel System or Windows OS or... other.
	NotAppleDevice,
	
	//=aka Apple Device but not AppleSilicon or Very Very Older Device
	AppleDeviceButNotSupported,
	
	//=aka Apple Device but Older Device 
	SpatialType,
	
	//=aka Apple Device & (like) flagship devices.
	TemporalType
}



BEGIN_SHADER_PARAMETER_STRUCT(FMetalFXParameters, )
	RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(DepthTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(VelocityTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(ReactiveMaskTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(CompositeMaskTexture, ERHIAccess::SRVMask)
	RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::UAVMask)
	SHADER_PARAMETER(float,      PreExposure)
	SHADER_PARAMETER(uint32,     bReversedDepth)   // 0 or 1
END_SHADER_PARAMETER_STRUCT()
