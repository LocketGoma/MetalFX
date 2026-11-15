// Copyright (c) 2026 LocketGoma.

#pragma once

#include "CoreMinimal.h"
#include "MetalFXSettings.h"
#include "Modules/ModuleManager.h"

//Note : MetalFX Disabled 상태라면 4개 클래스는 없음. (접근시 오류 발생)
class FMetalFXViewExtension;
class FMetalFXUpscalerCore;
class FMetalFXSpatialUpscalerCore;
class FMetalFXTemporalUpscalerCore;
class FOutputDevice;
struct IConsoleCommand;

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

	/** Returns the Core once the MetalFX runtime has been initialized. */
	FMetalFXUpscalerCore* GetMetalFXUpscaler() const;
	FMetalFXTemporalUpscalerCore* GetMetalFXTemporalUpscaler();
	FMetalFXSpatialUpscalerCore* GetMetalFXSpatialUpscaler();
	EMetalFXUpscalerType GetSelectedUpscalerType() const { return SelectedUpscalerType; }
	/** Game-thread eligibility before checking the current view family. */
	EMetalFXUpscalerType GetEligibleUpscalerType() const { return EligibleUpscalerType; }

	bool IsMetalFXSupported() const;
	bool GetIsSupportedByRHI() const { return IsMetalFXSupported(); }

private:
	FMetalFXUpscalerCore* CreateMetalFXUpscaler(EMetalFXUpscalerType RequestedType);
	void HandlePostRHIInitialized();
	void InitializeMetalFXRuntimeOnce();
	void HandleMetalFXInitializeCommand(const TArray<FString>& Args, FOutputDevice& Output);
	void HandleMetalFXEnabledChanged(IConsoleVariable* Enabled);
	void HandleMetalFXActivationSettingsChanged(IConsoleVariable*);
	FDelegateHandle OnPostRHIInitialized;
	FDelegateHandle OnMetalFXEnabledChanged;
	FDelegateHandle OnMetalFXUpscalerModeChanged;
	FDelegateHandle OnMetalFXEnableInEditorChanged;
	IConsoleCommand* InitializeConsoleCommands[2] = {};
	bool bRuntimeInitializationAttempted = false;
	EMetalFXUpscalerType EligibleUpscalerType = EMetalFXUpscalerType::None;

	// The module is the sole Core owner. Adapters keep non-owning typed pointers.
	TUniquePtr<FMetalFXUpscalerCore> MetalFXUpscaler;
	EMetalFXUpscalerType SelectedUpscalerType = EMetalFXUpscalerType::None;
	TSharedPtr<FMetalFXViewExtension, ESPMode::ThreadSafe> MetalFXViewExtension;

	EMetalSupportDevice MetalSupport = EMetalSupportDevice::NotSupported;
	EMetalFXSupportReason MetalFXSupport = EMetalFXSupportReason::NotSupported;
	EMetalFXUpscalerType SupportedUpscalerType = EMetalFXUpscalerType::None;
};
