#pragma once

#include "TemporalUpscaler.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFXHelper.h"

using ITemporalUpscaler = UE::Renderer::Private::ITemporalUpscaler;

BEGIN_SHADER_PARAMETER_STRUCT(FMetalFXPassParameters, )
	SHADER_PARAMETER(FUintVector4, Const0)
	SHADER_PARAMETER(FUintVector4, Const1)
	SHADER_PARAMETER(FUintVector4, Const2)
	SHADER_PARAMETER(FUintVector4, Const3)
	SHADER_PARAMETER(FVector2f, VPColor_ExtentInverse)
	SHADER_PARAMETER(FVector2f, VPColor_ViewportMin)
	SHADER_PARAMETER_SAMPLER(SamplerState, samLinearClamp)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FMetalFXSharpnessPassParameters, )
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
SHADER_PARAMETER(FUintVector4, Const0)
SHADER_PARAMETER(FVector2f, VPColor_ExtentInverse)
END_SHADER_PARAMETER_STRUCT()

struct FMetalFXDispatchParameters
{
	FIntRect SrcRect = FIntRect(FIntPoint::ZeroValue, FIntPoint::ZeroValue);
	FIntRect DestRect = FIntRect(FIntPoint::ZeroValue, FIntPoint::ZeroValue);
	FVector2D JitterOffset = FVector2D::ZeroVector;
	FVector2D MotionVectorScale = FVector2D::UnitVector;
		
	FMatrix InvViewProjectionMatrix;
	FMatrix ClipToPrevClipMatrix;
};

class FMetalFXHistory final : public ITemporalUpscaler::IHistory, public FRefCountBase
{
public:

	//해당 DebugName은 항상 TemporalUpscaler의 DebugName과 같아야 함.
	virtual const TCHAR* GetDebugName() const override { return TEXT("MetalFXTemporalUpscaler"); }
	virtual uint64 GetGPUSizeBytes() const override { return sizeof(FMetalFXHistory); };

private:
	virtual FReturnedRefCountValue AddRef() const final
	{
		return FRefCountBase::AddRef();
	}

	virtual uint32 Release() const final
	{
		return FRefCountBase::Release();
	}

	virtual uint32 GetRefCount() const final
	{
		return FRefCountBase::GetRefCount();
	}
};

#if WITH_METAL_PLATFORM
class FMetalFXTemporalUpscaler final : public ITemporalUpscaler
{
public:
	virtual float GetMinUpsampleResolutionFraction() const override;
	virtual float GetMaxUpsampleResolutionFraction() const override;
	FMetalFXTemporalUpscaler(FMetalFXUpscalerCore* InUpscaler);

	const bool GetIsSupportedDevice();

	const TCHAR* GetDebugName() const override { return TEXT("MetalFXTemporalUpscaler"); }

	virtual ITemporalUpscaler* Fork_GameThread(const class FSceneViewFamily& ViewFamily) const final override;
	virtual ITemporalUpscaler::FOutputs AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FInputs& Inputs) const override;
	const void CheckValidate() const;
private:
	FMetalFXUpscalerCore* m_FxUpscaler;

	mutable TRefCountPtr<IPooledRenderTarget> ReactiveExtractedTexture;
	mutable TRefCountPtr<IPooledRenderTarget> CompositeExtractedTexture;

	FIntRect ViewRect;
	FIntRect OutputRect;
};
#endif
