#pragma once

#include "CoreMinimal.h"
#include "CustomResourcePool.h"
#include "HAL/CriticalSection.h"
#include "MetalFXHelper.h"

class FMetalCommandBuffer;

struct FMetalFXActiveDebugInfo
{
	FIntRect InputRect = FIntRect();
	FIntRect OutputRect = FIntRect();
	float ScreenPercentage = 100.0f;
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

	virtual EMetalFXUpscalerMode GetUpscalerMode() const = 0;

	static EMetalFXSupportReason GetIsSupportedDevice();
	static bool IsMetalFXSupported();
	static EMetalFXSupportedType GetMetalFXSupportedType();
	static EMetalFXSupportReason GetMetalFXSupportReason();
	static bool IsUpscalerModeSupported(EMetalFXSupportedType SupportedTypes, EMetalFXUpscalerMode UpscalerMode);

	static FRDGTextureRef CreateOutputTexture(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef InSceneColorTexture,
		FIntRect OutputViewRect);

	void UpdateActiveDebugInfo(FIntRect InputRect, FIntRect OutputRect, float ScreenPercentage);
	FMetalFXActiveDebugInfo GetActiveDebugInfo() const;

protected:
#if METALFX_PLUGIN_ENABLED
	static FMetalFXTextureView CreateMetalFXTextureView(FRDGTextureRef Texture);
	static FMetalCommandBuffer* GetCurrentMetalCommandBuffer(FRHICommandList& CmdList);
	static void* GetMetalDevice();
#endif

	bool ValidateCommonExtents(
		FIntPoint InputTextureExtent,
		FIntPoint InputContentExtent,
		FIntPoint OutputExtent) const;
	bool ValidateCommonRects(FIntRect InputRect, FIntRect OutputRect) const;

private:
	bool bIsInitialized = false;

	mutable FCriticalSection ActiveDebugInfoCS;
	FMetalFXActiveDebugInfo ActiveDebugInfo;
};
