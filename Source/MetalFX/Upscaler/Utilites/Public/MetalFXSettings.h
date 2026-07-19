#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "HAL/IConsoleManager.h"


#include "MetalFXSettings.generated.h"

UENUM(BlueprintType)
enum class EMetalFXUpscalerMode : uint8
{
	None UMETA(DisplayName = "Off", ToolTip = "Disables MetalFX upscaling."),
	Spatial UMETA(DisplayName = "Spatial", ToolTip = "Uses the MetalFX Spatial upscaler."),
	Temporal UMETA(DisplayName = "Temporal", ToolTip = "Uses the MetalFX Temporal upscaler."),
	MAX
};

UENUM(BlueprintType)
enum class EMetalFXQualityMode : uint8
{
	NativeAA UMETA(DisplayName = "Native AA (100%)"),
	Quality UMETA(DisplayName = "Quality (66.7%)"),
	Balanced UMETA(DisplayName = "Balanced (50%)"),
	Performance UMETA(DisplayName = "Performance (35%)"),
	// MetalFX TemporalScaler does not support upscaling greater than 3x per dimension, so Ultra Performance (25%) is disabled for now.
	// UltraPerformance UMETA(DisplayName = "Ultra Performance (25%)"),
	// This value must be placed last. It resolves to 1% in non-shipping builds and to the value immediately before Min in shipping builds.
	Min UMETA(DisplayName = "Min"),
	// Enum value used to return the number of quality modes.
	MAX
};

extern TAutoConsoleVariable<bool> CVarEnableMetalFX;
extern TAutoConsoleVariable<bool> CvarEnableMetalFXInEditor;
extern TAutoConsoleVariable<bool> CVarMetalFXDebugDisplay;
extern TAutoConsoleVariable<int32> CVarMetalFXJitterMode;
extern TAutoConsoleVariable<int32> CVarMetalFXExperimentalInputExtentMode;
extern TAutoConsoleVariable<float> CVarMetalFXMotionVectorScaleX;
extern TAutoConsoleVariable<float> CVarMetalFXMotionVectorScaleY;
extern TAutoConsoleVariable<float> CVarMetalFXSharpness;
extern TAutoConsoleVariable<int32> CVarMetalFXUpscalerMode;
extern TAutoConsoleVariable<int32> CVarMetalFXQualityMode;

METALFX_API void ApplyMetalFXQualityModeToScreenPercentage(EMetalFXQualityMode QualityMode);

UCLASS(Config = Engine, DefaultConfig, DisplayName = "Apple MetalFX")
class METALFX_API UMetalFXSettings : public UDeveloperSettings
{
	GENERATED_BODY()

	UMetalFXSettings(const FObjectInitializer& ObjectInitializer);
	
public:
	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	UPROPERTY(Config, EditAnywhere, Category = "General Settings", meta = (ConsoleVariable = "r.MetalFX.Enabled", DisplayName = "Enabled"))
	bool bEnabled;

	UPROPERTY(Config, EditAnywhere, Category = "Editor Settings", meta = (ConsoleVariable = "r.MetalFX.EnableInEditor", DisplayName = "Enable In Editor", ToolTip = "Allows MetalFX to be used in PIE. (MetalFX must still be turned on with the MetalFX Enable command.)"))
	bool bEnableInEditor;

	UPROPERTY(Config, EditAnywhere, Category = "Debug Settings", meta = (ConsoleVariable = "r.MetalFX.DebugDisplay", DisplayName = "Debug Display", ToolTip = "Shows MetalFX runtime status and resolution information on screen."))
	bool bDebugDisplay;
	
	UPROPERTY(Config, EditAnywhere, Category = "General Settings", meta = (ConsoleVariable = "r.MetalFX.UpscalerMode", DisplayName = "Upscaler Mode", ToolTip = "Selects the MetalFX upscaler mode."))
	EMetalFXUpscalerMode UpscalerMode;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.MetalFX.Sharpness", DisplayName = "Sharpness", ClampMin = 0, ClampMax = 1, ToolTip = "(WIP) Applies the Robust Contrast Adaptive Sharpening Filter to sharpen the output image."))
	float Sharpness;
	
	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.MetalFX.QualityMode", DisplayName = "Quality Mode", ToolTip = "Selects the default quality mode to be used when upscaling with MetalFX."))
	EMetalFXQualityMode QualityMode;

	UPROPERTY(Config, EditAnywhere, Category = "Temporal Settings", meta = (ConsoleVariable = "r.MetalFX.JitterMode", DisplayName = "Jitter Mode", ToolTip = "Controls temporal jitter forwarding. 1: normal, 0: disabled, -1: inverted."))
	int32 JitterMode;

	UPROPERTY(Config, EditAnywhere, Category = "Temporal Settings", meta = (ConsoleVariable = "r.MetalFX.MotionVectorScaleX", DisplayName = "Motion Vector Scale X", ToolTip = "(WIP) Horizontal motion vector scale passed to MetalFX."))
	float MotionVectorScaleX;

	UPROPERTY(Config, EditAnywhere, Category = "Temporal Settings", meta = (ConsoleVariable = "r.MetalFX.MotionVectorScaleY", DisplayName = "Motion Vector Scale Y", ToolTip = "(WIP) Vertical motion vector scale passed to MetalFX."))
	float MotionVectorScaleY;
	
};
