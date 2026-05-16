#pragma once
#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIContext.h"
#include "MetalCommandBuffer.h"
#include "MetalRHIUtility.h"
#endif

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

//텍스쳐 포멧 그룹
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
	class TemporalScaler;
	class TemporalScalerDescriptor;
}


//텍스쳐 그룹
#if METALFX_METALCPP
//2차 변환 처리를 위한 중간 구조체
struct FMetalFXCppTextureView
{	
	FMetalFXCppTextureView() = default;
	FMetalFXCppTextureView(const FMetalFXCppTextureView& inTexview)
	{
		ReleaseTexture();
		
		Texture = inTexview.Texture;
		bNeedsRelease = inTexview.bNeedsRelease;
		bIsValid = inTexview.bIsValid;	
	}

public:
	void SetTexture(MTL::Texture* inTexture, bool isNeedRelease = false)
	{
		//기존 텍스쳐 정리
		if (bIsValid == false)
		{
			ReleaseTexture();
		}
		
		Texture = inTexture;
		bNeedsRelease = isNeedRelease;
		bIsValid = true;
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
	FMetalFXObjCTextureView(const FMetalFXObjCTextureView& inTexview)
	{
		ReleaseTexture();
		
		Texture = inTexview.Texture;
		bNeedsRelease = inTexview.bNeedsRelease;
		bIsValid = inTexview.bIsValid;
	}

	~FMetalFXObjCTextureView()
	{
		ReleaseTexture();
	}
	
	void SetTexture(id<MTLTexture> inTexture, bool isNeedRelease = false)
	{
		//기존 텍스쳐 정리
		if (bIsValid == false)
		{
			ReleaseTexture();
		}
		
		Texture = inTexture;
		bNeedsRelease = isNeedRelease;
		bIsValid = true;
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
	id<MTLTexture> Texture = nil;
	bool bNeedsRelease = false;
	bool bIsValid = false;
};
#endif

//해제 일괄처리
struct FMetalFXTextureGroup
{
	FMetalFXTextureGroup() = default;
	
	FMetalFXTextureGroup(FMetalFXTextureGroup&& Other) noexcept
	{
		ColorTexture 	= MoveTemp(Other.ColorTexture);
		DepthTexture 	= MoveTemp(Other.DepthTexture);
		VelocityTexture = MoveTemp(Other.VelocityTexture);
		OutputTexture 	= MoveTemp(Other.OutputTexture);

		bReleased = Other.bReleased;
		Other.bReleased = true; // 포인터가 옮겨진것 뿐이라, 원본 그룹에서 소멸 처리하면 꼬임
	}
	
	~FMetalFXTextureGroup()
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
	void TextureRelease(FMetalFXCppTextureView TargetTexture)
	{
		TargetTexture.ReleaseTexture();
	}
	
	void ReleaseTextureDeferred(FMetalFXCppTextureView TargetTexture, MTL::CommandBuffer* CommandBuffer)
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
	void TextureRelease(FMetalFXObjCTextureView TargetTexture)
	{
		TargetTexture.ReleaseTexture();
	}
	
	void ReleaseTextureDeferred(FMetalFXCppTextureView TargetTexture, id<MTLCommandBuffer> CommandBuffer)
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

#endif
