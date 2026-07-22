#pragma once

#if METALFX_PLUGIN_ENABLED
#include "TemporalUpscaler.h"
#include "MetalFXTemporalUpscalerCore.h"

using ITemporalUpscaler = UE::Renderer::Private::ITemporalUpscaler;

//해당 DebugName은 항상 TemporalUpscaler의 DebugName과 같아야 함.
inline const TCHAR* GetMetalFXTemporalUpscalerDebugName()
{
	return TEXT("MetalFXTemporalUpscaler");
}

class FMetalFXHistory final : public ITemporalUpscaler::IHistory, public FRefCountBase
{
public:

	virtual const TCHAR* GetDebugName() const override { return GetMetalFXTemporalUpscalerDebugName(); }
	virtual uint64 GetGPUSizeBytes() const override { return 0; }

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

class FMetalFXTemporalUpscaler final : public ITemporalUpscaler
{
public:
	virtual float GetMinUpsampleResolutionFraction() const override;
	virtual float GetMaxUpsampleResolutionFraction() const override;
	explicit FMetalFXTemporalUpscaler(FMetalFXTemporalUpscalerCore* InUpscaler);

	const TCHAR* GetDebugName() const override { return GetMetalFXTemporalUpscalerDebugName(); }

	virtual ITemporalUpscaler* Fork_GameThread(const class FSceneViewFamily& ViewFamily) const final override;
	virtual ITemporalUpscaler::FOutputs AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FInputs& Inputs) const override;
private:
	bool CheckValidate() const;

	// Non-owning. FMetalFXModule owns the Core for the module lifetime.
	FMetalFXTemporalUpscalerCore* UpscalerCore = nullptr;
};
#endif // METALFX_PLUGIN_ENABLED
