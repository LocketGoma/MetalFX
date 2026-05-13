#pragma once
#include "CoreMinimal.h"
#include "CustomResourcePool.h"
#include "MetalFXHelper.h"

//DLSS의 FDLSSUpscaler 클래스 포지션
class FMetalFXUpscalerCore final : public ICustomResourcePool
{
public:
	virtual void Tick(FRHICommandListImmediate& RHICmdList) override;

	UE_NONCOPYABLE(FMetalFXUpscalerCore)

	FMetalFXUpscalerCore();
	virtual ~FMetalFXUpscalerCore() override;

	void Initialize();
	
	static EMetalFXSupportReason GetIsSupportedDevice();
	
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
	
#if METALFX_PLUGIN_ENABLED
public:
	float GetMinUpsampleResolutionFraction() const;
	float GetMaxUpsampleResolutionFraction() const;

	//bool SetTextures(const FMetalFXTextureParameterGroup& Parameters);
	bool SetTextures(const FMetalFXParameters& Parameters);

	void SetJitterOffset(FVector2D Offset);
	void SetMotionVectorScale(FVector2f Scale);

	//DLSS 기반 통합 Encoder (외부에서는 얘만 호출)
	//void ExecuteMetalFX(FRHICommandList& CmdList, const FMetalFXTextureParameterGroup& Parameters, FIntPoint InRect, FIntPoint OutRect);
	void ExecuteMetalFX(FRHICommandList& CmdList, const FMetalFXParameters& Parameters, FIntPoint InRect, FIntPoint OutRect);
	
private:
	bool TextureSizeValidation_Cpp();
	bool TextureSizeValidation_Native();
	void Encode(FRHICommandList& CmdList);
	
	void UpdateInputRect(FIntPoint InRect);
	bool UpdateResolution(FIntPoint InRect, FIntPoint OutRect);
	

	bool GenerateUpscaler();
	
public:
	//유틸 함수
	const bool CheckValidate();

private:
	//DLSS의 NGXRHI 클래스 포지션 (UStruct 아님)
	std::unique_ptr<struct MetalFXModule> pModules;

	bool bIsInitialized;

	uint32_t m_InW, m_InH;
	uint32_t m_OutW, m_OutH;
#endif //METALFX_PLUGIN_ENABLED
};
