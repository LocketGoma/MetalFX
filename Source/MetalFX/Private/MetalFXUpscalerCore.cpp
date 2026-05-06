#include "MetalFXUpscalerCore.h"
#include "MetalFXSettings.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalRHI.h"
#include "MetalFXCoreUtility.h"
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

//내부 변수 컨트롤용 구조체
struct MetalFXModule
{
#if METALFX_METALCPP
	NS::SharedPtr<MTL::Device> m_CppDevice;
	NS::SharedPtr<MTL::CommandQueue> m_CppCommandQueue;
	NS::SharedPtr<MTLFX::TemporalScaler> m_CppScaler;
#elif METALFX_NATIVE
	id<MTLDevice> m_Device;
	id<MTLFXTemporalScaler> m_Scaler;
#endif
};

#endif	//METALFX_PLUGIN_ENABLED 

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
   // 1) RDG 텍스처 가져오기
   FRDGTextureRef   RdgTex = In;
   // 2) RHI 텍스처 → Metal 네이티브
   FRHITexture*     HiTex  = RdgTex->GetRHI();
   return static_cast<MTL::Texture *>(HiTex->GetNativeResource());
}
#elif METALFX_NATIVE
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
#endif

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
		Desc->setColorTextureFormat(MTL::PixelFormat::PixelFormatRGBA16Float);
		Desc->setDepthTextureFormat(MTL::PixelFormat::PixelFormatDepth32Float);
		Desc->setMotionTextureFormat(MTL::PixelFormatRG16Float);
		Desc->setOutputTextureFormat(MTL::PixelFormat::PixelFormatRGBA16Float);
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
	
	pModules->m_Scaler = MetalFXCreateTemporalUpscaler(MetalDevice, m_InW, m_InH, m_OutW, m_OutH);
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
void FMetalFXUpscalerCore::SetTextures(const FMetalFXParameters& Parameters)
{
#if METALFX_METALCPP
   if (pModules)
   {
	  CheckValidate();
	  pModules->m_CppScaler->setColorTexture(ToMTLTexture(Parameters.ColorTexture));
	  pModules->m_CppScaler->setDepthTexture(ToMTLTexture(Parameters.DepthTexture));
	  pModules->m_CppScaler->setMotionTexture(ToMTLTexture(Parameters.VelocityTexture));
	  pModules->m_CppScaler->setOutputTexture(ToMTLTexture(Parameters.OutputTexture));
   }
#endif
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

void FMetalFXUpscalerCore::Encode(const FMetalFXParameters& Parameters)
{
#if !METALFX_NATIVE
	UE_LOG(LogMetalFX, Error, TEXT("You Try Call [Objective-C++ Version], but this Enviroment is MetalCPP."));
   CheckValidate();
   @autoreleasepool
   {
		//CommandQueue&CommandBuffer를 외부에서 가져오는것으로 변경시 아래 3줄 주석처리
		id<MTLDevice> MetalDevice = (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
		id<MTLCommandQueue> CmdQueue = [MetalDevice newCommandQueue];	   
		id<MTLCommandBuffer> CommandBuffer = [CmdQueue commandBuffer];

   		id<MTLTexture> ColorTex = ToOBJCTexture(Parameters.ColorTexture);
		id<MTLTexture> DepthTex = ToOBJCTexture(Parameters.DepthTexture);
		id<MTLTexture> MotionTex = ToOBJCTexture(Parameters.VelocityTexture);
		id<MTLTexture> OutTex = ToOBJCTexture(Parameters.OutputTexture);
		MetalFXEncode(pModules->m_Scaler, CommandBuffer, ColorTex, DepthTex, MotionTex, OutTex, true);
   }
#endif
}

void FMetalFXUpscalerCore::Encode()
{
#if !METALFX_METALCPP
	UE_LOG(LogMetalFX, Error, TEXT("You Try Call [MetalCPP Version], but this Enviroment is Objective-C++."));
#else
	CheckValidate();
	@autoreleasepool
	{
		NS::SharedPtr<MTL::CommandBuffer> CommandBuffer = NS::RetainPtr(pModules->m_CppCommandQueue->commandBuffer());
		pModules->m_CppScaler->encodeToCommandBuffer(CommandBuffer.get());	  
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
		case static_cast<int32>(EMetalFXSupportReason::NotSupportediOSOutOfDate) :
		{
			UE_LOG(LogRHI, Warning, TEXT("MetalFX Not Supported, iOS version is Too old"));
			return EMetalFXSupportReason::NotSupportediOSOutOfDate;
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
