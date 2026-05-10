#include "MetalFXUpscalerCore.h"
#include "MetalFXSettings.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ColorManagement/TransferFunctions.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalRHI.h"
#include "MetalFXCoreUtility.h"
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#endif	//METALFX_PLUGIN_ENABLED 

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

#if METALFX_METALCPP
MTL::Texture* ToMTLTexture(const FRDGTextureRef& In)
{
	if (In == nullptr) { return nullptr; }
   // 1) RDG 텍스처 가져오기
   FRDGTextureRef   RdgTex = In;
   // 2) RHI 텍스처 → Metal 네이티브
   FRHITexture*     HiTex  = RdgTex->GetRHI();
   return static_cast<MTL::Texture *>(HiTex->GetNativeResource());
}

//2차 변환 처리를 위한 중간 구조체
struct FMetalFXCppTextureView
{	
	//FMetalFXCppTextureView() = default;
	//FMetalFXCppTextureView(const FMetalFXCppTextureView&) = delete;
	
	//To do. Command Buffer 진행 이후 릴리즈가 필요한 경우 보완필요함!
public:
	void SetTexture(MTL::Texture* inTexture, bool isNeedRelease = false)
	{
		//기존 텍스쳐 정리
		ReleaseTexture();
		
		Texture = inTexture;
		bNeedsRelease = isNeedRelease;
		bIsValid = true;
	}
	
	MTL::Texture* GetTexture()
	{
		return Texture;
	}
	
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
	
	bool IsValid() const
	{
		return bIsValid && Texture != nullptr;
	}
	
private:	
	MTL::Texture* Texture = nullptr;
	bool bNeedsRelease = false;
	bool bIsValid = false;
};

//해제 일괄처리
struct FMetalFXCppTextureGroup
{
	~FMetalFXCppTextureGroup()
	{
		ReleaseAllTexture();
	}
	
public:
	void ReleaseAllTexture()
	{
		TextureRelease(ColorTexture);
		TextureRelease(DepthTexture);
		TextureRelease(VelocityTexture);
		TextureRelease(OutputTexture);
	}

	void TextureRelease(FMetalFXCppTextureView TargetTexture)
	{
		TargetTexture.ReleaseTexture();
	}
	
	FMetalFXCppTextureView ColorTexture;
	FMetalFXCppTextureView DepthTexture;
	FMetalFXCppTextureView VelocityTexture;
	FMetalFXCppTextureView OutputTexture;
};

static FMetalFXCppTextureView GetMetalFX2DTextureView(MTL::Texture* SourceTexture)
{
	FMetalFXCppTextureView Result;

	if (SourceTexture == nullptr)
	{
		return Result;
	}

	//1) 현재 텍스쳐 타입이 이미 MetalFX에서 사용하는 2D인 경우
	if (SourceTexture->textureType() == MTL::TextureType2D)
	{
		Result.SetTexture(SourceTexture, false);
		return Result;
	}

	//2) 현재 텍스쳐 타입이 2D Array인 경우
	if (SourceTexture->textureType() == MTL::TextureType2DArray)
	{
		NS::Range LevelRange = NS::Range::Make(0, 1);
		NS::Range SliceRange = NS::Range::Make(0, 1);

		MTL::Texture* Temp = SourceTexture->newTextureView(
			SourceTexture->pixelFormat(),
			MTL::TextureType2D,
			LevelRange,
			SliceRange
		);

		Result.SetTexture(Temp, Temp != nullptr);
		return Result;
	}

	return Result;
}
#endif

#if METALFX_NATIVE
id<MTLTexture> ToOBJCTexture(const FRDGTextureRef& In)
{
   // 1) RDG 텍스처 가져오기
   FRDGTextureRef   RdgTex = In;
   // 2) RHI 텍스처 → Metal 네이티브
   FRHITexture*     HiTex  = RdgTex->GetRHI();
	
	void* Native = HiTex->GetNativeResource();
	if (!Native)
	{
		return nil;
	}
	
	return (__bridge id<MTLTexture>)Native;
}

//2차 변환 처리를 위한 중간 구조체
struct FMetalFXObjCTextureView
{	
	//FMetalFXObjCTextureView() = default;
	//FMetalFXObjCTextureView(const FMetalFXCppTextureView&) = delete;
	
	//To do. Command Buffer 진행 이후 릴리즈가 필요한 경우 보완필요함!
public:
	void SetTexture(id<MTLTexture> inTexture, bool isNeedRelease = false)
	{
		//기존 텍스쳐 정리
		ReleaseTexture();
		
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
	
	bool IsValid() const
	{
		return bIsValid && Texture != nil;
	}
	
private:	
	id<MTLTexture> Texture = nil;
	bool bNeedsRelease = false;
	bool bIsValid = false;
};

//해제 일괄처리
struct FMetalFXObjCTextureGroup
{
	~FMetalFXObjCTextureGroup()
	{
		ReleaseAllTexture();
	}
	
public:
	void ReleaseAllTexture()
	{
		TextureRelease(ColorTexture);
		TextureRelease(DepthTexture);
		TextureRelease(VelocityTexture);
		TextureRelease(OutputTexture);
	}

	void TextureRelease(FMetalFXObjCTextureView TargetTexture)
	{
		TargetTexture.ReleaseTexture();
	}
	
	FMetalFXObjCTextureView ColorTexture;
	FMetalFXObjCTextureView DepthTexture;
	FMetalFXObjCTextureView VelocityTexture;
	FMetalFXObjCTextureView OutputTexture;
};

static FMetalFXObjCTextureView GetMetalFX2DTextureView(id<MTLTexture> SourceTexture)
{
	FMetalFXObjCTextureView Result;

	if (SourceTexture == nil)
	{
		return Result;
	}

	if ([SourceTexture textureType] == MTLTextureType2D)
	{
		Result.SetTexture(SourceTexture, false);
		return Result;
	}

	if ([SourceTexture textureType] == MTLTextureType2DArray)
	{
		NSRange LevelRange = NSMakeRange(0, 1);
		NSRange SliceRange = NSMakeRange(0, 1);

		id<MTLTexture> temp = [SourceTexture newTextureViewWithPixelFormat:[SourceTexture pixelFormat]
											 textureType:MTLTextureType2D
												  levels:LevelRange
												  slices:SliceRange];
	
		Result.SetTexture(temp, (temp != nil));
		return Result;
	}

	return Result;
}
#endif


#endif

//내부 변수 컨트롤용 구조체
struct MetalFXModule
{
#if METALFX_METALCPP
	NS::SharedPtr<MTL::Device> m_CppDevice;
	NS::SharedPtr<MTL::CommandQueue> m_CppCommandQueue;
	NS::SharedPtr<MTLFX::TemporalScaler> m_CppScaler;
	FMetalFXCppTextureGroup TextureGroup;
#endif
	
#if METALFX_NATIVE
	id<MTLDevice> m_Device;
	id<MTLFXTemporalScaler> m_Scaler;
	FMetalFXObjCTextureGroup TextureGroup;	
#endif
	//공용 (텍스쳐 포멧)
	FMetalFXTextureFormatGroup Formats;
};

void FMetalFXUpscalerCore::Tick(FRHICommandListImmediate& RHICmdList)
{
	;//Do nothing
}
FMetalFXUpscalerCore::FMetalFXUpscalerCore()
{
#if METALFX_PLUGIN_ENABLED
	pModules = std::make_unique<MetalFXModule>();

	//정해지지 않았을때의 디폴트 값.
	m_InW = 2560;
	m_OutW = 2560;
	m_InH = 1440; 
	m_OutH = 1440;
#endif //METALFX_PLUGIN_ENABLED 
}

FMetalFXUpscalerCore::~FMetalFXUpscalerCore()
{
#if METALFX_PLUGIN_ENABLED
#if METALFX_METALCPP
	pModules->m_CppCommandQueue.reset();
	pModules->m_CppScaler.reset();
	
	//RHI에서 직접 가져온거라 reset 하면 터질듯... 테스트 필요함.
	//pModules->m_CppDevice.reset();
#elif METALFX_NATIVE
	//[pModules->m_Device release]
	[pModules->m_Scaler release];
	pModules->m_Scaler = nil;
	
	pModules.reset();
#endif
#endif	//METALFX_PLUGIN_ENABLED
}

#if METALFX_PLUGIN_ENABLED
float FMetalFXUpscalerCore::GetMinUpsampleResolutionFraction() const
{
	//SupportedInputContextMinScale
	float fracW = float(m_InW) / float(m_OutW);
	float fracH = float(m_InH) / float(m_OutH);
	return FMath::Min(fracW, fracH);
}	

float FMetalFXUpscalerCore::GetMaxUpsampleResolutionFraction() const
{
	float fracW = float(m_InW) / float(m_OutW);
	float fracH = float(m_InH) / float(m_OutH);
	return FMath::Max(fracW, fracH);	
}

bool FMetalFXUpscalerCore::Initialize()
{
	if (bIsInitalized)
	{
		return true;
	}
	bool bSuccess = false;

	id<MTLDevice> MetalDevice = (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
	
#if METALFX_METALCPP
	if (pModules->m_CppScaler.get())
	{
		pModules->m_CppScaler.reset();
	}
	
	void* MetalDeviceVoid = (__bridge void *)MetalDevice;
	MTL::Device* MetalDeviceCpp = static_cast<MTL::Device*>(MetalDeviceVoid);
	if (MetalDeviceCpp)
	{
		pModules->m_CppDevice = NS::RetainPtr(MetalDeviceCpp);
	}
  
	if (!pModules)
	{
		return false;
	}
	if(!pModules->m_CppDevice)
	{
		NSLog(@"MetalFX Device Not Found.");
		return false;
	}

	//Descriptor는 굳이 갖고있을 필요 X
	auto Desc = RetainPtr(MTLFX::TemporalScalerDescriptor::alloc()->init());
	if (Desc->supportsDevice(pModules->m_CppDevice.get()))
	{
		Desc->setInputWidth(m_InW);
		Desc->setInputHeight(m_InH);
		Desc->setOutputWidth(m_OutW);
		Desc->setOutputHeight(m_OutH);
		Desc->setColorTextureFormat(static_cast<MTL::PixelFormat>(pModules->Formats.Color));
		Desc->setDepthTextureFormat(static_cast<MTL::PixelFormat>(pModules->Formats.Depth));
		Desc->setMotionTextureFormat(static_cast<MTL::PixelFormat>(pModules->Formats.Motion));
		Desc->setOutputTextureFormat(static_cast<MTL::PixelFormat>(pModules->Formats.Output));
		Desc->setAutoExposureEnabled(true);
   	
		pModules->m_CppScaler = NS::RetainPtr(Desc->newTemporalScaler(pModules->m_CppDevice.get()));
	 
		SetCommandQueue();
	}
	else
	{
		NSLog(@"MetalFX TemporalScaler API Not Supported.");
	}
	bSuccess = (pModules->m_CppScaler.get() != nullptr);

#elif METALFX_NATIVE
	if (pModules->m_Scaler != nil)
	{
		//메모리 해제 대기상태로 만듬
		[pModules->m_Scaler release];
		pModules->m_Scaler = nil;
	}
	
	pModules->m_Scaler = MetalFXCreateTemporalUpscaler(MetalDevice, pModules->Formats, m_InW, m_InH, m_OutW, m_OutH);
	bSuccess = pModules->m_Scaler != nil;
#endif
	
	bIsInitalized = bSuccess;
  
	return bSuccess;
}

void FMetalFXUpscalerCore::SetCommandQueue()
{
#if METALFX_METALCPP
	MTL::Device* pDevice = pModules->m_CppDevice.get();
	pModules->m_CppCommandQueue = NS::RetainPtr(pDevice->newCommandQueue());
	check(pModules->m_CppCommandQueue);
#endif
}

void FMetalFXUpscalerCore::UpdateInputRect(FIntPoint InRect)
{
	m_InW = InRect.X;
	m_InH = InRect.Y;

#if METALFX_METALCPP
	pModules->m_CppScaler->setInputContentWidth(m_InW);
	pModules->m_CppScaler->setInputContentHeight(m_InW);
#elif METALFX_NATIVE
	MetalFXUpdateScalerResolution(pModules->m_Scaler, m_InW, m_InH);
#endif
}

//Resolution 자체가 바뀌는 경우엔 아예 다시 생성해야됨.
void FMetalFXUpscalerCore::UpdateResolution(FIntPoint InRect, FIntPoint OutRect)
{
	//하나라도 기존과 다르면 업데이트 & 재생성
	if (!((m_InW == InRect.X) && (m_InH == InRect.Y) && (m_OutW == OutRect.X) && (m_OutH = OutRect.Y)))
	{
		m_InW = InRect.X;
		m_InH = InRect.Y;
		m_OutW = OutRect.X;
		m_OutH = OutRect.Y;
   	
		bIsInitalized = false;

   		Initialize();
	}
}

const void FMetalFXUpscalerCore::CheckValidate() const
{
	bool bValidate = false;
#if METALFX_METALCPP
	bValidate = (pModules != nullptr) && (pModules->m_CppScaler.get() != nullptr);
#elif METALFX_NATIVE
	bValidate = (pModules != nullptr) && (pModules->m_Scaler != nil);
#endif	
   checkf(bValidate, TEXT("You Trying To Using MetalFX. but MetalFX Upscaler Core Not Ready or Crashed. You Must Check MetalFX Upscaler Logics. see MetalFXUpscalerCore Class For More Infomations."));
}

bool FMetalFXUpscalerCore::SetTextures(const FMetalFXParameters& Parameters)
{
	bool bIsSuccess = false;
#if METALFX_METALCPP || METALFX_NATIVE
	FMetalFXTextureFormatGroup TempFormats;
	
	if (pModules)
	{
		CheckValidate();
		
#if METALFX_METALCPP        
		//Color Texture
		pModules->TextureGroup.ColorTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.ColorTexture));
        
		//Depth Texture 
		pModules->TextureGroup.DepthTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.DepthTexture));
        
		//Velocity Texture 
		pModules->TextureGroup.VelocityTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.VelocityTexture));
         
		//Output Texture 
		pModules->TextureGroup.OutputTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.OutputTexture));
		
		TempFormats.Color = pModules->TextureGroup.ColorTexture.GetTexture()->pixelFormat();
		TempFormats.Depth = pModules->TextureGroup.DepthTexture.GetTexture()->pixelFormat();
		TempFormats.Motion = pModules->TextureGroup.VelocityTexture.GetTexture()->pixelFormat();
		TempFormats.Output = pModules->TextureGroup.OutputTexture.GetTexture()->pixelFormat();
#endif
		
#if METALFX_NATIVE
        CheckValidate();
        //Color Texture
        pModules->TextureGroup.ColorTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.ColorTexture));
        
        //Depth Texture 
        pModules->TextureGroup.DepthTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.DepthTexture));
        
        //Velocity Texture 
        pModules->TextureGroup.VelocityTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.VelocityTexture));
        
        //Output Texture 
        pModules->TextureGroup.OutputTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.OutputTexture));
		
		TempFormats.Color = (FMetalFXPixelFormat)[pModules->TextureGroup.ColorTexture.GetTexture() pixelFormat];
		TempFormats.Depth = (FMetalFXPixelFormat)[pModules->TextureGroup.DepthTexture.GetTexture() pixelFormat];
		TempFormats.Motion = (FMetalFXPixelFormat)[pModules->TextureGroup.VelocityTexture.GetTexture() pixelFormat];
		TempFormats.Output = (FMetalFXPixelFormat)[pModules->TextureGroup.OutputTexture.GetTexture() pixelFormat];
#endif
		
		bIsSuccess =(pModules->Formats.IsValidFormat(TempFormats));   
	
		if (!bIsSuccess)
		{
			pModules->Formats = TempFormats;
			bIsInitalized = false;
			Initialize();
		}
	}
#endif
	
	return bIsSuccess;
}

void FMetalFXUpscalerCore::SetJitterOffset(FVector2D Offset)
{
   if (pModules)
   {
#if METALFX_METALCPP	
	  CheckValidate();
	  pModules->m_CppScaler->setJitterOffsetX(static_cast<float>(Offset.X));
	  pModules->m_CppScaler->setJitterOffsetY(static_cast<float>(Offset.Y));
#elif METALFX_NATIVE
	MetalFXSetJitterOffset(pModules->m_Scaler, Offset.X, Offset.Y);
#endif
   }

}
void FMetalFXUpscalerCore::SetMotionVectorScale(FVector2D Scale)
{
	if (pModules)
	{
		CheckValidate();
#if METALFX_METALCPP		
		pModules->m_CppScaler->setMotionVectorScaleX(static_cast<float>(Scale.X));
		pModules->m_CppScaler->setMotionVectorScaleY(static_cast<float>(Scale.Y));
#elif METALFX_NATIVE
		MetalFXSetMotionVectorScale(pModules->m_Scaler, Scale.X, Scale.Y);
#endif
	}	
}

#pragma region Encode_OldType

//Native Objective-C++ Version
void FMetalFXUpscalerCore::Encode(const FMetalFXParameters& Parameters)
{
#if !METALFX_NATIVE
	UE_LOG(LogMetalFX, Error, TEXT("You Try Call [Objective-C++ Version], but this Enviroment is MetalCPP."));
#else
   CheckValidate();
   SetTextures(Parameters);
	
	uint64 ColorTexWidth = (unsigned long)[pModules->TextureGroup.ColorTexture.GetTexture() width];
	uint64 ColorTexHeight = (unsigned long)[pModules->TextureGroup.ColorTexture.GetTexture() height];
	uint64 VeloTexWidth = (unsigned long)[pModules->TextureGroup.VelocityTexture.GetTexture() width];
	uint64 VeloTexHeight = (unsigned long)[pModules->TextureGroup.VelocityTexture.GetTexture() height];

	if (!((ColorTexWidth == VeloTexWidth) && (ColorTexHeight == VeloTexHeight)))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("[MetalFX] Color: %lux%lu Motion: %lux%lu"), ColorTexWidth, ColorTexHeight, VeloTexWidth, VeloTexHeight);
		
		UE_LOG(LogMetalFX, Error, TEXT("Texture Size Mismatch! Skip."));
		return;
	}
	
   @autoreleasepool
   {	   
		//CommandQueue&CommandBuffer를 외부에서 가져오는것으로 변경시 아래 3줄 주석처리
		//id<MTLDevice> MetalDevice = (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
		//id<MTLCommandQueue> CmdQueue = [MetalDevice newCommandQueue];	   
		//id<MTLCommandBuffer> CommandBuffer = [CmdQueue commandBuffer];
	   
   		id<MTLTexture> ColorTex = pModules->TextureGroup.ColorTexture.GetTexture();
		id<MTLTexture> DepthTex = pModules->TextureGroup.DepthTexture.GetTexture();
		id<MTLTexture> MotionTex = pModules->TextureGroup.VelocityTexture.GetTexture();
		id<MTLTexture> OutTex = pModules->TextureGroup.OutputTexture.GetTexture();
	   
	   UE_LOG(LogMetalFX, Warning,
		   TEXT("[MetalFX] ColorTexture.pixelFormat=%lu"),
		   (unsigned long)[pModules->TextureGroup.ColorTexture.GetTexture() pixelFormat]);
	   
	   UE_LOG(LogMetalFX, Warning,
		   TEXT("[MetalFX] ViewType=%lu ViewPixelFormat=%lu"),
		   (unsigned long)[pModules->TextureGroup.ColorTexture.GetTexture() textureType],
		   (unsigned long)[pModules->TextureGroup.ColorTexture.GetTexture() pixelFormat]);
	   
		MetalFXEncode(pModules->m_Scaler, CommandBuffer, ColorTex, DepthTex, MotionTex, OutTex, true);
   }
#endif
}

//MetalCPP Version
void FMetalFXUpscalerCore::Encode()
{
#if !METALFX_METALCPP
	UE_LOG(LogMetalFX, Error, TEXT("You Try Call [MetalCPP Version], but this Enviroment is Objective-C++."));
#else
	CheckValidate();
	
	if (pModules)
	{
	   //Color Texture
	   pModules->m_CppScaler->setColorTexture(pModules->TextureGroup.ColorTexture.GetTexture());
	   
	   //Depth Texture 
	   pModules->m_CppScaler->setDepthTexture(pModules->TextureGroup.DepthTexture.GetTexture());
	   
	   //Velocity Texture 
	   pModules->m_CppScaler->setMotionTexture(pModules->TextureGroup.VelocityTexture.GetTexture());
		
	   //Output Texture 
	   pModules->m_CppScaler->setOutputTexture(pModules->TextureGroup.OutputTexture.GetTexture());
	}
	
	@autoreleasepool
	{
		NS::SharedPtr<MTL::CommandBuffer> CommandBuffer = NS::RetainPtr(pModules->m_CppCommandQueue->commandBuffer());
		pModules->m_CppScaler->encodeToCommandBuffer(CommandBuffer.get());	  
	}
#endif
}
#pragma endregion

bool FMetalFXUpscalerCore::TextureSizeValidation(const FMetalFXCppTextureGroup& TextureGroup)
{
	bool Result = true;
	uint64 ColorTexWidth = 0;
	uint64 ColorTexHeight = 0;
	uint64 VeloTexWidth = 0;
	uint64 VeloTexHeight = 0;
	
#if METALFX_NATIVE
	ColorTexWidth	= (unsigned long)[TextureGroup.ColorTexture.GetTexture() width];
	ColorTexHeight	= (unsigned long)[TextureGroup.ColorTexture.GetTexture() height];
	VeloTexWidth	= (unsigned long)[TextureGroup.VelocityTexture.GetTexture() width];
	VeloTexHeight	= (unsigned long)[TextureGroup.VelocityTexture.GetTexture() height];
#endif
	
#if METALFX_METALCPP
	ColorTexWidth	= static_cast<uint64>(TextureGroup.ColorTexture.GetTexture()->width());
	ColorTexHeight	= static_cast<uint64>(TextureGroup.ColorTexture.GetTexture()->height());
	VeloTexWidth	= static_cast<uint64>(TextureGroup.VelocityTexture.GetTexture()->width());
	VeloTexHeight	= static_cast<uint64>(TextureGroup.VelocityTexture.GetTexture()->height());
	
#endif
	Result = ((ColorTexWidth == VeloTexWidth) && (ColorTexHeight == VeloTexHeight));
	
	if (!Result)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("[MetalFX] TextureSize Mismatch! - Color: %llux%llu Motion: %llux%llu"), ColorTexWidth, ColorTexHeight, VeloTexWidth, VeloTexHeight);
		Result = false;
	}
	
	return Result;
}

void FMetalFXUpscalerCore::ExecuteMetalFX(FRHICommandList& CmdList, const FMetalFXParameters& Parameters)
{
	if (!pModule)
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX Upscaler Module was broken! skip Upscaling this frame."));
		
		return;		
	}
	
	if (!SetTextures(Parameters))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Texture Format mismatch found. skip Upscaling this frame."));
		
		return;
	}
	
	if (!TextureSizeValidation(pModules->TextureGroup))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Texture Rect mismatch found. skip Upscaling this frame."));
		
		return;
	}
	
	Encode(CmdList);	
}

//텍스쳐 등 모든 세팅이 끝났을때 마지막으로 호출
void FMetalFXUpscalerCore::Encode(FRHICommandList& CmdList)
{
	//TRHICommandList_RecursiveHazardous<IRHICommandContext> RHICmdList(&CmdList.GetContext());
	
	FMetalRHICommandContext& MetalContext = FMetalRHICommandContext::Get(CmdList);
	FMetalCommandBuffer* CurrentCommandBuffer =	MetalContext.GetCurrentCommandBuffer();
	
	if (CurrentCommandBuffer == nullptr)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Can not found current activate Metal command buffer."));
		return;
	}
	
#if METALFX_NATIVE
	
	id<MTLTexture> ColorTex = pModules->TextureGroup.ColorTexture.GetTexture();
	id<MTLTexture> DepthTex = pModules->TextureGroup.DepthTexture.GetTexture();
	id<MTLTexture> MotionTex = pModules->TextureGroup.VelocityTexture.GetTexture();
	id<MTLTexture> OutTex = pModules->TextureGroup.OutputTexture.GetTexture();
	
	id<MTLCommandBuffer> MetalNativeCommandBuffer = (__bridge id<MTLCommandBuffer>)CurrentCommandBuffer;
	
	MetalFXEncode(pModules->m_Scaler, MetalNativeCommandBuffer, ColorTex, DepthTex, MotionTex, OutTex, true);
	
#endif
	
#if METALFX_METALCPP
	
	pModules->m_CppScaler->setColorTexture(pModules->TextureGroup.ColorTexture.GetTexture());
	pModules->m_CppScaler->setDepthTexture(pModules->TextureGroup.DepthTexture.GetTexture());
	pModules->m_CppScaler->setMotionTexture(pModules->TextureGroup.VelocityTexture.GetTexture());
	pModules->m_CppScaler->setOutputTexture(pModules->TextureGroup.OutputTexture.GetTexture());
	
	@autoreleasepool
	{
		//NS::SharedPtr<MTL::CommandBuffer> MetalCppCommandBuffer = NS::RetainPtr(CurrentCommandBuffer->GetMTLCmdBuffer());
		//pModules->m_CppScaler->encodeToCommandBuffer(MetalCppCommandBuffer.get());	  
		
		MTL::CommandBuffer* MetalCppCommandBuffer = CurrentCommandBuffer->GetMTLCmdBuffer();
		pModules->m_CppScaler->encodeToCommandBuffer(MetalCppCommandBuffer);	  
	}	
#endif
}
#endif  //METALFX_PLUGIN_ENABLED 


EMetalFXSupportReason FMetalFXUpscalerCore::GetIsSupportedDevice()
{
#if METALFX_PLUGIN_ENABLED
	switch (MetalFXQuerySupportReason())
	{
		case static_cast<int32>(EMetalFXSupportReason::Supported) :
		{
			UE_LOG(LogRHI, Log, TEXT("MetalFX Supported Device."));
			return EMetalFXSupportReason::Supported;
		}
		case static_cast<int32>(EMetalFXSupportReason::NotSupportedOldDiviceType) :
		{
			UE_LOG(LogRHI, Warning, TEXT("MetalFX Not Supported this Device. Device is Too old"));
			return EMetalFXSupportReason::NotSupportedOldDiviceType;
		}
		case static_cast<int32>(EMetalFXSupportReason::NotSupportedOSVersionOutOfDate) :
		{
			UE_LOG(LogRHI, Warning, TEXT("MetalFX Not Supported, OS version is Too old"));
			return EMetalFXSupportReason::NotSupportedOSVersionOutOfDate;
		}
		case static_cast<int32>(EMetalFXSupportReason::NotSupportedMetalFXFrameworkMissing) :
		{
			UE_LOG(LogRHI, Warning, TEXT("MetalFX Not Supported, Framework Missing."));
			return EMetalFXSupportReason::NotSupportedMetalFXFrameworkMissing;
		}
		case static_cast<int32>(EMetalFXSupportReason::NotSupportedMetalFXCreationFailed) :
		{
			UE_LOG(LogRHI, Warning, TEXT("MetalFX Not Supported, MetalFX Creation Failed"));
			return EMetalFXSupportReason::NotSupportedMetalFXCreationFailed;
		}
	}
#endif
	UE_LOG(LogRHI, Warning, TEXT("MetalFX Not Supported this Environment."));
	return EMetalFXSupportReason::NotSupported;
}
