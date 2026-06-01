#pragma once
#include "CoreMinimal.h"
#include "CustomResourcePool.h"
#include "MetalFXHelper.h"
#include <memory>

//DLSS의 FDLSSUpscaler 클래스 포지션
class FMetalFXUpscalerCore final : public ICustomResourcePool
{
//--------Unreal Enviroment Block--------	
public:
	UE_NONCOPYABLE(FMetalFXUpscalerCore)

	FMetalFXUpscalerCore();
	virtual ~FMetalFXUpscalerCore() override;

	
	//Do nothing.
	virtual void Tick(FRHICommandListImmediate& RHICmdList) override final {;};


	void Initialize();
	
	static EMetalFXSupportReason GetIsSupportedDevice();	
	
//------.Velocity File------	
public:
	static FRDGTextureRef PrepareVelocityTexture(	
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef InSceneColorTexture,
	FRDGTextureRef InSceneDepthTexture,
	FRDGTextureRef InVelocityTexture,
	FIntRect InputViewRect,
	FIntRect OutputViewRect,
	FVector2D TemporalJitterPixels);
	
	static FRDGTextureRef GenerateVelocityTexturePass(	
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef InSceneDepthTexture,
	FRDGTextureRef InVelocityTexture,
	FIntRect InputViewRect,
	FIntRect OutputViewRect,
	FVector2D TemporalJitterPixels);	
	
private:
	//임시 조치용
	static FRDGTextureRef AddBlackVelocityTexturePass(
	FRDGBuilder& GraphBuilder,
	FIntPoint OutputExtent);
//------.Velocity File------ (End)		
//------.Output File------	
public:
	static FRDGTextureRef CreateOutputTexture(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef InSceneColorTexture,
	FIntRect OutputViewRect);
private:
	
//------.Output File------ (End)	
//--------Unreal Enviroment Block-------- (End)
//--------MetalFX Enabled Enviroment Block--------		
#if METALFX_PLUGIN_ENABLED
public:
	const float GetMinUpsampleResolutionFraction() const;
	const float GetMaxUpsampleResolutionFraction() const;

	void SetJitterOffset(FVector2D Offset);
	void SetMotionVectorScale(FVector2f Scale);

	//DLSS 기반 통합 Encoder (외부에서는 얘만 호출)
	void ExecuteMetalFX(FRHICommandList& CmdList, FMetalFXTextureGroup& TextureGroup);
	
	//Execute 가능한지 체크
	bool CheckForExecuteMetalFX(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent);
	bool SetTexturesToGroup(const FMetalFXParameters& Parameters, FMetalFXTextureGroup& OutTexGroup);
private:
	//모든 Execute 조건 통과시 수행
	void Encode(FRHICommandList& CmdList, FMetalFXTextureGroup& TextureGroup);
	
	//업스케일러 생성 / 업데이트 필요 시 작동 (무조건 현재 업스케일러를 릴리즈 한 뒤 재생성)
	bool GenerateUpscaler();
	bool EnsureUpscalerForTextures(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent, const FMetalFXTextureFormatGroup& Formats);
	
	void UpdateInputContentSize(FIntPoint InputContentExtent);
	bool UpdateResolution(FIntPoint InputTextureExtent, FIntPoint OutputExtent);

	
	bool TextureFormatMatchChecker();
	

//--------MetalFX Enabled Enviroment Block-------- (End)
#endif //METALFX_PLUGIN_ENABLED
	
public:
	//유틸 함수
	const bool CheckValidate();

private:
	//DLSS의 NGXRHI 클래스 포지션 (UStruct 아님)
	std::unique_ptr<struct MetalFXModule> pModules;

	bool bIsInitialized;

	uint32_t m_InputTextureW, m_InputTextureH;
	uint32_t m_InputContentW, m_InputContentH;
	uint32_t m_OutputW, m_OutputH;
};
