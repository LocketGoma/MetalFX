#pragma once

#include "CoreMinimal.h"
#include "CustomResourcePool.h"
#include "MetalFXHelper.h"
#if !UE_BUILD_SHIPPING
#include "HAL/CriticalSection.h"		//디버그 메시지 출력용
#endif

class FMetalCommandBuffer;

struct FMetalFXEncodeGeometry
{
	FIntPoint DescriptorInputExtent = FIntPoint::ZeroValue;
	FIntPoint InputContentExtent = FIntPoint::ZeroValue;
	FIntPoint OutputExtent = FIntPoint::ZeroValue;
	FIntRect InputRect = FIntRect();
	FIntRect OutputRect = FIntRect();
};

struct FMetalFXActiveDebugInfo
{
	FIntRect InputRect = FIntRect();
	FIntRect OutputRect = FIntRect();
	FMetalFXResolutionDebugInfo Resolution;
	bool bIsValid = false;
};

/**
 * Common MetalFX Core functionality.
 *
 * Mode-specific scaler state belongs in FMetalFXTemporalUpscalerCore or
 * FMetalFXSpatialUpscalerCore. Adapters hold non-owning pointers to those
 * concrete types; the MetalFX module is the sole owner.
 */
class FMetalFXUpscalerCore : public ICustomResourcePool
{
public:
	UE_NONCOPYABLE(FMetalFXUpscalerCore)

	FMetalFXUpscalerCore();
	virtual ~FMetalFXUpscalerCore() override;

	virtual void Tick(FRHICommandListImmediate& RHICmdList) override final {}

	void Initialize();
	bool IsInitialized() const { return bIsInitialized; }

	virtual EMetalFXUpscalerType GetUpscalerType() const = 0;

	static EMetalFXUpscalerType QuerySupportedUpscalerType();
	static EMetalFXSupportReason QuerySupportReason(EMetalFXUpscalerType SupportedUpscalerType);

	static FRDGTextureRef CreateOutputTexture(FRDGBuilder& GraphBuilder, FRDGTextureRef InSceneColorTexture, FIntRect OutputViewRect);

protected:
#if METALFX_PLUGIN_ENABLED
	static FMetalFXTextureView CreateMetalFXTextureView(FRDGTextureRef Texture);
	static FMetalCommandBuffer* GetCurrentMetalCommandBuffer(FRHICommandList& CmdList);
	static void* GetMetalDevice();
#endif

	bool ValidateCommonExtents(FIntPoint InputTextureExtent, FIntPoint InputContentExtent, FIntPoint OutputExtent) const;
	static bool ValidateCommonRects(const FIntRect& InputRect, const FIntRect& OutputRect);

private:
	bool bIsInitialized = false;

#if !UE_BUILD_SHIPPING
public:
	void UpdateActiveDebugInfo(const FIntRect& InputRect, const FIntRect& OutputRect);
	void UpdateResolutionDebugInfo(const FMetalFXResolutionDebugInfo& ResolutionDebugInfo);
	FMetalFXActiveDebugInfo GetActiveDebugInfo() const;
	
private:
	mutable FCriticalSection ActiveDebugInfoCS;
	FMetalFXActiveDebugInfo ActiveDebugInfo;
#endif
};
