// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetalFXSettings.h"
#include "Modules/ModuleManager.h"

class FMetalFXViewExtension;
class FMetalFXUpscalerCore;
class FMetalFXSpatialUpscalerCore;
class FMetalFXTemporalUpscalerCore;

class METALFX_API FMetalFXModule : public IModuleInterface
{
public:
	virtual ~FMetalFXModule() override;

	static FMetalFXModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMetalFXModule>(TEXT("MetalFX"));
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	EMetalSupportDevice GetMetalSupport() const;
	EMetalFXSupportReason GetMetalFXSupportReason() const;
	EMetalFXUpscalerType GetSupportedUpscalerType() const;
	// Compatibility wrappers for integrations using the pre-refactor API.
	EMetalSupportDevice QueryMetalSupport() const { return GetMetalSupport(); }
	EMetalFXSupportReason QueryMetalFXSupport() const { return GetMetalFXSupportReason(); }
	EMetalFXUpscalerType QueryMetalFXUpscalerType() const { return GetSupportedUpscalerType(); }

	/** Returns the Core created during startup RHI initialization. */
	FMetalFXUpscalerCore* GetMetalFXUpscaler() const;
	FMetalFXTemporalUpscalerCore* GetMetalFXTemporalUpscaler();
	FMetalFXSpatialUpscalerCore* GetMetalFXSpatialUpscaler();
	EMetalFXUpscalerType GetSelectedUpscalerType() const { return SelectedUpscalerType; }

	bool IsMetalFXSupported() const;
	bool GetIsSupportedByRHI() const { return IsMetalFXSupported(); }

private:
	FMetalFXUpscalerCore* CreateMetalFXUpscaler(EMetalFXUpscalerType RequestedType);
	void HandlePostRHIInitialized();
	FDelegateHandle OnPostRHIInitialized;

	// The module is the sole Core owner. Adapters keep non-owning typed pointers.
	TUniquePtr<FMetalFXUpscalerCore> MetalFXUpscaler;
	EMetalFXUpscalerType SelectedUpscalerType = EMetalFXUpscalerType::None;
	TSharedPtr<FMetalFXViewExtension, ESPMode::ThreadSafe> MetalFXViewExtension;

	EMetalSupportDevice MetalSupport = EMetalSupportDevice::NotSupported;
	EMetalFXSupportReason MetalFXSupport = EMetalFXSupportReason::NotSupported;
	EMetalFXUpscalerType SupportedUpscalerType = EMetalFXUpscalerType::None;
};
