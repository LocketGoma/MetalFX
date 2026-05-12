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

	static EMetalFXSupportReason GetIsSupportedDevice();
	
#if METALFX_PLUGIN_ENABLED
	float GetMinUpsampleResolutionFraction() const;
	float GetMaxUpsampleResolutionFraction() const;

	bool SetTextures(const FMetalFXParameters& Parameters);

	void SetJitterOffset(FVector2D Offset);
	void SetMotionVectorScale(FVector2D Scale);

	//DLSS 기반 통합 Encoder (외부에서는 얘만 호출)
	void ExecuteMetalFX(FRHICommandList& CmdList, const FMetalFXParameters& Parameters, FIntPoint InRect, FIntPoint OutRect);
	
private:
	bool TextureSizeValidation_Cpp(struct FMetalFXCppTextureGroup& TextureGroup);
	bool TextureSizeValidation_Native(struct FMetalFXObjCTextureGroup& TextureGroup);
	void Encode(FRHICommandList& CmdList);
	
	void UpdateInputRect(FIntPoint InRect);
	bool UpdateResolution(FIntPoint InRect, FIntPoint OutRect);
	
	bool Initialize();
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
