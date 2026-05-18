#include "MetalFXUpscalerCore.h"
#include "MetalFXSettings.h"
#include "MetalFXCoreUtility.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#if METALFX_PLUGIN_ENABLED
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIContext.h"
#include "MetalCommandBuffer.h"
#include "MetalRHIUtility.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>

//------------Metal CPP Version------------
#if METALFX_METALCPP
//UE Tex to MTL(Metal Texture)
MTL::Texture* ToMTLTexture(const FRDGTextureRef& In)
{
	if (In == nullptr) { return nullptr; }
	// 1) RDG 텍스처 가져오기
	FRDGTextureRef   RdgTex = In;
	// 2) RHI 텍스처 → Metal 네이티브
	FRHITexture*     HiTex  = RdgTex->GetRHI();
	
	if (HiTex == nullptr) {return nullptr;}
	
	return static_cast<MTL::Texture *>(HiTex->GetNativeResource());
}

//MTL Array To MTL
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
//UE Tex to MTL(Metal Texture)
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

//MTL Tex Array To MTL Texture
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
		//Need Unique Texture.
		NSRange LevelRange = NSMakeRange(0, 1);
		NSRange SliceRange = NSMakeRange(0, 1);

		id<MTLTexture> temp = [SourceTexture newTextureViewWithPixelFormat:[SourceTexture pixelFormat]
								textureType:MTLTextureType2D levels:LevelRange slices:SliceRange];
	
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
#if METALFX_PLUGIN_ENABLED
#if METALFX_METALCPP
	NS::SharedPtr<MTLFX::TemporalScaler> m_CppScaler;
#endif
	
#if METALFX_NATIVE
	id<MTLFXTemporalScaler> m_Scaler;
#endif
#endif
	//공용 (텍스쳐 포멧)
	FMetalFXTextureFormatGroup Formats;
};

#if METALFX_PLUGIN_ENABLED
const float FMetalFXUpscalerCore::GetMinUpsampleResolutionFraction() const
{
	//SupportedInputContextMinScale
	float fracW = float(m_InW) / float(m_OutW);
	float fracH = float(m_InH) / float(m_OutH);
	return FMath::Min(fracW, fracH);
}	

const float FMetalFXUpscalerCore::GetMaxUpsampleResolutionFraction() const
{
	float fracW = float(m_InW) / float(m_OutW);
	float fracH = float(m_InH) / float(m_OutH);
	return FMath::Max(fracW, fracH);	
}

bool FMetalFXUpscalerCore::GenerateUpscaler()
{
	bool bSuccess = false;
	
#if METALFX_METALCPP
	//기존 것이 있다면 무조건 날리고 재생성
	if (pModules->m_CppScaler.get())
	{
		pModules->m_CppScaler.reset();
	}
	
	MTL::Device* MetalDevice = (MTL::Device*)GDynamicRHI->RHIGetNativeDevice();
	if (MetalDevice)
	{
		auto Desc = RetainPtr(MTLFX::TemporalScalerDescriptor::alloc()->init());
		if (Desc->supportsDevice(MetalDevice))
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
			
			pModules->m_CppScaler = NS::RetainPtr(Desc->newTemporalScaler(MetalDevice));
			
			bSuccess = (pModules->m_CppScaler.get() != nullptr);
		}
	}
	else 
	{
		NSLog(@"Metal Device for MetalFX Not Founded");	
	}
#endif
	
#if METALFX_NATIVE
	if (pModules->m_Scaler != nil)
	{
		//메모리 해제 대기상태로 만듬
		[pModules->m_Scaler release];
		pModules->m_Scaler = nil;
	}
	
	id<MTLDevice> MetalDevice = (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
	
	if (MetalDevice != nil)
	{
		pModules->m_Scaler = MetalFXCreateTemporalUpscaler(MetalDevice, pModules->Formats, m_InW, m_InH, m_OutW, m_OutH);
		bSuccess = pModules->m_Scaler != nil;
	}
	else 
	{
		NSLog(@"Metal Device for MetalFX Not Founded");	
	}
#endif
	
	if (bSuccess)
	{
		pModules->Formats.ResetChangeState();
	}
	else
	{
		UE_LOG(LogMetalFX, Error, TEXT("MetalFX TemporalScaler API Generated Failed."));
	}
	
	return bSuccess;
}

void FMetalFXUpscalerCore::UpdateInputRect(FIntPoint InRect)
{
	m_InW = InRect.X;
	m_InH = InRect.Y;

#if METALFX_METALCPP
	pModules->m_CppScaler->setInputContentWidth(m_InW);
	pModules->m_CppScaler->setInputContentHeight(m_InH);
#elif METALFX_NATIVE
	MetalFXUpdateScalerResolution(pModules->m_Scaler, m_InW, m_InH);
#endif
}

//Output Resolution 자체가 바뀌는 경우엔 아예 다시 생성해야됨.
bool FMetalFXUpscalerCore::UpdateResolution(FIntPoint InRect, FIntPoint OutRect)
{
	//하나라도 기존과 다르면 업데이트 & 재생성
	if ((m_OutW != OutRect.X) || (m_OutH != OutRect.Y))
	{
		m_InW = InRect.X;
		m_InH = InRect.Y;
		m_OutW = OutRect.X;
		m_OutH = OutRect.Y;
		
		return false;
	}
	else if ((m_InW != InRect.X) || (m_InH != InRect.Y))
	{
		UpdateInputRect(InRect);
	}
	
	return true;
}

bool FMetalFXUpscalerCore::SetTexturesToGroup(const FMetalFXParameters& Parameters, FMetalFXTextureGroup& OutTexGroup)
{	
	if ((Parameters.ColorTexture == nullptr ) || (Parameters.DepthTexture == nullptr) || (Parameters.VelocityTexture == nullptr) || (Parameters.OutputTexture == nullptr))
	{
		UE_LOG(LogMetalFX, Error, TEXT("Some Texture are Invalidate. it is Right Action?"));
		return false;
	}
	
#if METALFX_METALCPP || METALFX_NATIVE
	FMetalFXTextureFormatGroup TempFormats;
		
	if (pModules)
	{
		CheckValidate();
		
#if METALFX_METALCPP        
		//Color Texture
		OutTexGroup.ColorTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.ColorTexture));
        
		//Depth Texture 
		OutTexGroup.DepthTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.DepthTexture));
        
		//Velocity Texture 
		OutTexGroup.VelocityTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.VelocityTexture));
         
		//Output Texture 
		OutTexGroup.OutputTexture = GetMetalFX2DTextureView(ToMTLTexture(Parameters.OutputTexture));
		
		TempFormats.Color = OutTexGroup.ColorTexture.GetTexture()->pixelFormat();
		TempFormats.Depth = OutTexGroup.DepthTexture.GetTexture()->pixelFormat();
		TempFormats.Motion = OutTexGroup.VelocityTexture.GetTexture()->pixelFormat();
		TempFormats.Output = OutTexGroup.OutputTexture.GetTexture()->pixelFormat();
#endif
		
#if METALFX_NATIVE
        CheckValidate();
		
        //Color Texture
		OutTexGroup.ColorTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.ColorTexture));
        
        //Depth Texture 
		OutTexGroup.DepthTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.DepthTexture));
        
        //Velocity Texture 
		OutTexGroup.VelocityTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.VelocityTexture));
        
        //Output Texture 
		OutTexGroup.OutputTexture = GetMetalFX2DTextureView(ToOBJCTexture(Parameters.OutputTexture));
		
		TempFormats.Color = (FMetalFXPixelFormat)[OutTexGroup.ColorTexture.GetTexture() pixelFormat];
		TempFormats.Depth = (FMetalFXPixelFormat)[OutTexGroup.DepthTexture.GetTexture() pixelFormat];
		TempFormats.Motion = (FMetalFXPixelFormat)[OutTexGroup.VelocityTexture.GetTexture() pixelFormat];
		TempFormats.Output = (FMetalFXPixelFormat)[OutTexGroup.OutputTexture.GetTexture() pixelFormat];
#endif
		
		pModules->Formats.IsValidFormat(TempFormats);   
	
		if (pModules->Formats.GetIsChanged())
		{
			pModules->Formats = TempFormats;
		}
	}
#endif
	return true;
}

void FMetalFXUpscalerCore::SetJitterOffset(FVector2D Offset)
{
   if (pModules)
   {
#if METALFX_METALCPP	
	  CheckValidate();
	  pModules->m_CppScaler->setJitterOffsetX(static_cast<float>(Offset.X));
	  pModules->m_CppScaler->setJitterOffsetY(static_cast<float>(Offset.Y));
#endif
	   
#if METALFX_NATIVE
	MetalFXSetJitterOffset(pModules->m_Scaler, Offset.X, Offset.Y);
#endif
   }

}
void FMetalFXUpscalerCore::SetMotionVectorScale(FVector2f Scale)
{
	if (pModules)
	{
		CheckValidate();
#if METALFX_METALCPP		
		pModules->m_CppScaler->setMotionVectorScaleX(static_cast<float>(Scale.X));
		pModules->m_CppScaler->setMotionVectorScaleY(static_cast<float>(Scale.Y));
#endif
		
#if METALFX_NATIVE
		MetalFXSetMotionVectorScale(pModules->m_Scaler, Scale.X, Scale.Y);
#endif
	}	
}

bool FMetalFXUpscalerCore::TextureFormatMatchChecker()
{
	//Change False = Match
	return !pModules->Formats.GetIsChanged();
}

void FMetalFXUpscalerCore::ExecuteMetalFX(FRHICommandList& CmdList, FMetalFXTextureGroup& TextureGroup)
{	
	if (!CheckValidate())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Upscaler Broken. retry generate upscaler. skip Upscaling this frame."));
		TextureGroup.ReleaseAllTexture();
		return;
	}	
	Encode(CmdList, TextureGroup);
}

bool FMetalFXUpscalerCore::CheckForExecuteMetalFX(FIntPoint InRect, FIntPoint OutRect)
{
	bool bSuccess = true;
	bool bGenerated = true;
	
	//Upscaler Validation
	if (!CheckValidate())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("MetalFX Upscaler Broken. retry generate upscaler. skip Upscaling this frame."));
		
		bSuccess = false;
		bGenerated = false;
	}
	
	//Tex Rect Changed Check
	if (!UpdateResolution(InRect, OutRect))
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Output Texture size mismatch found. skip Upscaling this frame."));
		
		bSuccess = false;
		bGenerated = false;
	}
	
	//Tex Format match
	if (!TextureFormatMatchChecker())
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Texture Format mismatch found. skip Upscaling this frame."));
		
		bSuccess = false;
		bGenerated = false;
	}
	
	if (bSuccess == false)
	{
		if (bGenerated == false)
		{
			GenerateUpscaler();
			UE_LOG(LogMetalFX, Warning, TEXT("Upscaler ReGenerated. skip Upscaling this frame."));
		}
	}
	return bSuccess;
}

//텍스쳐 등 모든 세팅이 끝났을때 마지막으로 호출
void FMetalFXUpscalerCore::Encode(FRHICommandList& CmdList, FMetalFXTextureGroup& TextureGroup)
{
	FMetalCommandBuffer* CurrentCommandBuffer = FMetalRHIUtility::GetCurrentCommandBufferFromCmdList(CmdList);
	
	if (CurrentCommandBuffer == nullptr)
	{
		UE_LOG(LogMetalFX, Warning, TEXT("Can not found current activate Metal command buffer."));
		return;
	}
	
#if METALFX_METALCPP
	pModules->m_CppScaler->setColorTexture(TextureGroup.ColorTexture.GetTexture());
	pModules->m_CppScaler->setDepthTexture(TextureGroup.DepthTexture.GetTexture());
	pModules->m_CppScaler->setMotionTexture(TextureGroup.VelocityTexture.GetTexture());
	pModules->m_CppScaler->setOutputTexture(TextureGroup.OutputTexture.GetTexture());

	MTL::CommandBuffer* MetalCppCommandBuffer = CurrentCommandBuffer->GetMTLCmdBuffer();
	
	if (MetalCppCommandBuffer!=nullptr)
	{		
		@autoreleasepool
		{
			pModules->m_CppScaler->encodeToCommandBuffer(MetalCppCommandBuffer);
		}
		
		TextureGroup.ReleaseAllTextureDeferred(MetalCppCommandBuffer);
	}
	else
	{
		TextureGroup.ReleaseAllTexture();
	}
	
#endif
	
#if METALFX_NATIVE
	
	id<MTLTexture> ColorTex = TextureGroup.ColorTexture.GetTexture();
	id<MTLTexture> DepthTex = TextureGroup.DepthTexture.GetTexture();
	id<MTLTexture> MotionTex = TextureGroup.VelocityTexture.GetTexture();
	id<MTLTexture> OutTex = TextureGroup.OutputTexture.GetTexture();
	
	id<MTLCommandBuffer> MetalNativeCommandBuffer = (__bridge id<MTLCommandBuffer>)CurrentCommandBuffer;
	
	MetalFXEncode(pModules->m_Scaler, MetalNativeCommandBuffer, ColorTex, DepthTex, MotionTex, OutTex);
	
	TextureGroup.ReleaseAllTextureDeferred(MetalNativeCommandBuffer);
#endif
}
#endif  //METALFX_PLUGIN_ENABLED 

//-------Base & Utility Functions--------
FMetalFXUpscalerCore::FMetalFXUpscalerCore() :
bIsInitialized(false)
{
#if METALFX_PLUGIN_ENABLED
	pModules = std::make_unique<MetalFXModule>();

	//정해지지 않았을때의 디폴트 값. (QHD)
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
	pModules->m_CppScaler.reset();
	pModules->m_CppScaler = nullptr;
#endif
#if METALFX_NATIVE
	[pModules->m_Scaler release];
	pModules->m_Scaler = nil;
#endif	
	pModules.reset();
#endif	//METALFX_PLUGIN_ENABLED
}

void FMetalFXUpscalerCore::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}

	if (!pModules)
	{
		return;
	}
#if METALFX_PLUGIN_ENABLED
	bIsInitialized = GenerateUpscaler();
#endif
}

const bool FMetalFXUpscalerCore::CheckValidate()
{
	bool bValidate = false;
	
#if METALFX_METALCPP
	bValidate = ((pModules != nullptr) && (pModules->m_CppScaler.get() != nullptr));
#endif
	
#if METALFX_NATIVE
	bValidate = ((pModules != nullptr) && (pModules->m_Scaler != nil));
#endif
	
	if (!bValidate)
	{
		UE_LOG(LogMetalFX, Error, TEXT("You Trying To Using MetalFX. but MetalFX Upscaler Core Not Ready or Crashed. You Must Check MetalFX Upscaler Logics. see MetalFXUpscalerCore Class For More Infomations."));
	}
	
	return bValidate;
}

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

//-------Base & Utility Functions--------(End)
