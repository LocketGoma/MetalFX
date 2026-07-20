// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetalFXHelper.h"
#include "Modules/ModuleManager.h"

class FMetalFXViewExtension;
class FMetalFXUpscalerCore;
class FMetalFXSpatialUpscalerCore;
class FMetalFXTemporalUpscaler;
class FMetalFXTemporalUpscalerCore;

class METALFX_API FMetalFXModule : public IModuleInterface
{
public:
	virtual ~FMetalFXModule() override;

	static FMetalFXModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMetalFXModule>("MetalFX");
	}
	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;	
	
	EMetalSupportDevice QueryMetalSupport() const;
	EMetalFXSupportReason QueryMetalFXSupport() const;
	EMetalFXUpscalerType QueryMetalFXUpscalerType() const;

	/** Returns the Core created during startup RHI initialization. */
	FMetalFXUpscalerCore* GetMetalFXUpscaler() const;
	FMetalFXTemporalUpscalerCore* GetMetalFXTemporalUpscaler();
	FMetalFXSpatialUpscalerCore* GetMetalFXSpatialUpscaler();
	EMetalFXUpscalerType GetSelectedUpscalerType() const { return SelectedUpscalerType; }

	bool GetIsSupportedByRHI() const;
	
private:
	FMetalFXUpscalerCore* CreateMetalFXUpscaler(EMetalFXUpscalerType RequestedType);
	void HandlePostRHIInitialized();
	FDelegateHandle OnPostEngineInitSettings;
	FDelegateHandle OnPostRHIInitialized;
private:
	// The module is the sole Core owner. Adapters keep non-owning typed pointers.
	TUniquePtr<FMetalFXUpscalerCore> MetalFXUpscaler;
	EMetalFXUpscalerType SelectedUpscalerType = EMetalFXUpscalerType::None;
	TSharedPtr<FMetalFXViewExtension, ESPMode::ThreadSafe> MetalFXViewExtension;

	EMetalSupportDevice MetalSupport = EMetalSupportDevice::NotSupported;
	EMetalFXSupportReason MetalFXSupport = EMetalFXSupportReason::NotSupported;
	EMetalFXUpscalerType MetalFXUpscalerType = EMetalFXUpscalerType::None;
};
