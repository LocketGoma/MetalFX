#pragma once
#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMetalFX, Verbose, All);

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
	NotSupportedOldDiviceType,
	NotSupportedOSVersionOutOfDate,
	NotSupportedMetalFXFrameworkMissing,
	NotSupportedMetalFXCreationFailed,
};

//= MetalFX 관련 최종 판단 (SpatialType / TemporalType 인 경우에만 MetalFX 정상 작동)
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
};

using FMetalFXPixelFormat = uint64_t;
struct FMetalFXTextureFormatGroup
{
	FMetalFXPixelFormat Color = 0;
	FMetalFXPixelFormat Depth = 0;
	FMetalFXPixelFormat Motion = 0;
	FMetalFXPixelFormat Output = 0;

	FMetalFXTextureFormatGroup() = default;
	
	FMetalFXTextureFormatGroup& operator =(const FMetalFXTextureFormatGroup& InFormats)
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
	
	FMetalFXTextureFormatGroup(const FMetalFXTextureFormatGroup& InFormats)
	{
		bIsChanged = (Color != InFormats.Color || Depth != InFormats.Depth || Motion != InFormats.Motion || Output != InFormats.Output);
		
		Color = InFormats.Color;
		Depth = InFormats.Depth;
		Motion = InFormats.Motion;
		Output = InFormats.Output;
	}
	
	void IsValidFormat(FMetalFXTextureFormatGroup& InFormats)
	{
		bIsChanged = (Color != InFormats.Color || Depth != InFormats.Depth || Motion != InFormats.Motion || Output != InFormats.Output);
	}
	
	bool GetIsChanged()
	{
		return bIsChanged;
	}
	
	void ResetChangeState()
	{
		bIsChanged = false;
	}
	
private:
	bool bIsChanged = false;
};

struct FMetalFXTextureParameterGroup
{
	FRDGTextureRef ColorTexture = nullptr;
	FRDGTextureRef DepthTexture = nullptr;
	FRDGTextureRef VelocityTexture = nullptr;
	FRDGTextureRef OutputTexture = nullptr;
};

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


