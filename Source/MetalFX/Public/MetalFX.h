// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetalFXUpscalerCore.h"
#include "MetalFXHelper.h"
#include "Modules/ModuleManager.h"

class FMetalFXViewExtension;
class FMetalFXTemporalUpscaler;

using IMetalFXemporalUpscaler = UE::Renderer::Private::ITemporalUpscaler;

class METALFX_API FMetalFXModule : public IModuleInterface
{
public:
	static FMetalFXModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMetalFXModule>("MetalFX");
	}
	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;	
	
	EMetalSupport QueryMetalSupport() const;
	EMetalFXSupport QueryMetalFXSupport() const;

	FMetalFXUpscalerCore* GetMetalFXUpscaler() const;
	bool GetIsSupportedByRHI() const;
	void SetMetalFXUpscaler(TSharedPtr<FMetalFXUpscalerCore, ESPMode::ThreadSafe> Upscaler);
	
private:
	void HandlePostRHIInitialized();
	void HandleWorldBeginPlay(UWorld* World, const UWorld::InitializationValues InitValue);
	FDelegateHandle OnPostRHIInitialized;
	FDelegateHandle OnPostWorldBeginPlay;
private:
	TSharedPtr<FMetalFXUpscalerCore, ESPMode::ThreadSafe> MetalFXUpscaler;
	TSharedPtr<FMetalFXViewExtension, ESPMode::ThreadSafe> MetalFXViewExtension;

#if WITH_METAL_PLATFORM
#endif
	EMetalSupport MetalSupport = EMetalSupport::NotSupported;
	EMetalFXSupport MetalFXSupport = EMetalFXSupport::NotSupported;
};
