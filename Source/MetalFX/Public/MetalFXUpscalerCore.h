#pragma once
#include "CoreMinimal.h"
#include "CustomResourcePool.h"
#include "MetalFXHelper.h"

//DLSS의 FDLSSUpscaler 클래스 포지션
class FMetalFXUpscalerCore final : public ICustomResourcePool
{
//--------Unreal Enviroment Block--------	
public:
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
//--------Unreal Enviroment Block-------- (End)
//--------MetalFX Enabled Enviroment Block--------		
#if METALFX_PLUGIN_ENABLED
public:
	const float GetMinUpsampleResolutionFraction() const;
	const float GetMaxUpsampleResolutionFraction() const;

	void SetJitterOffset(FVector2D Offset);
	void SetMotionVectorScale(FVector2f Scale);

	//DLSS 기반 통합 Encoder (외부에서는 얘만 호출)
	void ExecuteMetalFX(FRHICommandList& CmdList);
	
	//Execute 가능한지 체크
	bool CheckForExecuteMetalFX(FIntPoint InRect, FIntPoint OutRect);
	void SetTexturesToGroup(const FMetalFXParameters& Parameters);
private:
	bool TextureFormatMatchChecker();
	
	bool TextureSizeValidation();
	bool TextureSizeValidation_Cpp();
	bool TextureSizeValidation_Native();
	void Encode(FRHICommandList& CmdList);
	
	void UpdateInputRect(FIntPoint InRect);
	bool UpdateResolution(FIntPoint InRect, FIntPoint OutRect);

	bool GenerateUpscaler();
//--------MetalFX Enabled Enviroment Block-------- (End)
#endif //METALFX_PLUGIN_ENABLED
	
public:
	//유틸 함수
	const bool CheckValidate();

private:
	//DLSS의 NGXRHI 클래스 포지션 (UStruct 아님)
	std::unique_ptr<struct MetalFXModule> pModules;

	bool bIsInitialized;

	uint32_t m_InW, m_InH;
	uint32_t m_OutW, m_OutH;
};
