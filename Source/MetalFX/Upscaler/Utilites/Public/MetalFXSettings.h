#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "HAL/IConsoleManager.h"

#include "MetalFXSettings.generated.h"

//= Metal이 지원되는 Apple 기기인지 아닌지
enum class EMetalSupportDevice : uint8
{
	Supported,
	NotSupported
};

//= MetalFX 가능한 환경인지 여부
enum class EMetalFXSupportReason : uint8
{
	Supported,
	NotSupported,
	NotSupportedOldDeviceType,
	NotSupportedOSVersionOutOfDate,
	NotSupportedMetalFXFrameworkMissing,
	NotSupportedMetalFXCreationFailed,
};

UENUM(BlueprintType)
enum class EMetalFXUpscalerType : uint8
{
	None UMETA(DisplayName = "Off", ToolTip = "Disables MetalFX upscaling."),
	Spatial UMETA(DisplayName = "Spatial", ToolTip = "Uses the MetalFX Spatial upscaler."),
	Temporal UMETA(DisplayName = "Temporal", ToolTip = "Uses the MetalFX Temporal upscaler."),
	MAX UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EMetalFXQualityMode : uint8
{
	// NativeAA (Actual 100%)
	// Forces both the MetalFX Primary input and Secondary output to 100% of the actual display resolution.
	// This mode bypasses the engine-selected base fraction and Dynamic Resolution,
	// so it is the only preset that guarantees a native-display-sized MetalFX output.
	NativeAA UMETA(DisplayName = "NativeAA (Always 100%)"),

	// UltraQuality (Primary Input 100%)
	// Uses 100% of the engine-selected MetalFX output target as its Primary input.
	// The target may be smaller than the native display because it includes the engine Screen Percentage,
	// Dynamic Resolution, DPI scale, or an external monitor's resolution.
	// Therefore UltraQuality can differ from NativeAA even though both modes use a 100% Primary ratio.
	UltraQuality UMETA(DisplayName = "UltraQuality (100%)"),

	// Quality (Primary Input 66.7%)
	// Renders the MetalFX input at 66.7% of the engine-selected output target.
	// Upscales it by approximately 1.5x per dimension.
	Quality UMETA(DisplayName = "Quality (66.7%)"),

	// Balanced (Primary Input 50%)
	// Renders the MetalFX input at 50% of the engine-selected output target.
	// Upscales it by 2.0x per dimension.
	Balanced UMETA(DisplayName = "Balanced (50%)"),

	// Performance (Primary Input 42%)
	// Renders the MetalFX input at 42% of the engine-selected output target.
	// Upscales it by approximately 2.4x per dimension.
	Performance UMETA(DisplayName = "Performance (42%)"),

	// UltraPerformance (Primary Input 34%)
	// Renders the MetalFX input at 34% of the engine-selected output target. MetalFX Upscales it by approximately 2.94x per dimension.
	// The safety margin below 3.0x avoids exceeding MetalFX TemporalScaler's maximum supported scale after integer resolution rounding.
	UltraPerformance UMETA(DisplayName = "UltraPerformance (34%)"),

	// This value must follow all selectable modes. It resolves to 1% in non-shipping builds and to the preceding mode in shipping builds.
	Min UMETA(Hidden),

	// Enum sentinel used to represent an invalid mode and to return the number of quality-mode entries.
	MAX UMETA(Hidden)
};

struct FMetalFXQualitySettings
{
	// Human-readable preset name used by logs and the on-screen debug display.
	const TCHAR* Name = TEXT("Balanced");

	// Primary input fraction relative to the MetalFX Secondary output target.
	// In absolute mode the same number is used directly as Primary Screen Percentage.
	float InputResolutionFraction = 0.5f;

	// Forces native-display input/output instead of composing with the engine base fraction.
	bool bForceNativeResolution = false;

	// A returned value of 1.0 means 100% of the Secondary output target. It means
	// 100% of the physical display only when NativeAA also forces that target to
	// native resolution.
	float GetPrimaryResolutionFraction() const
	{
		return InputResolutionFraction;
	}

	float GetScreenPercentage() const
	{
		return InputResolutionFraction * 100.0f;
	}
};

struct FMetalFXResolutionDebugInfo
{
	EMetalFXQualityMode QualityMode = EMetalFXQualityMode::MAX;
	float EngineBaseResolutionFraction = 1.0f;
	float PrimaryResolutionFraction = 1.0f;
	float OutputResolutionFraction = 1.0f;
	float FinalInputResolutionFraction = 1.0f;
	bool bAutoScalingFromEngine = true;
	bool bDynamicResolutionActive = false;
	bool bIsValid = false;
};

extern TAutoConsoleVariable<bool> CVarEnableMetalFX;
extern TAutoConsoleVariable<bool> CVarEnableMetalFXInEditor;
extern TAutoConsoleVariable<bool> CVarMetalFXDebugDisplay;
extern TAutoConsoleVariable<int32> CVarMetalFXJitterMode;
extern TAutoConsoleVariable<int32> CVarMetalFXExperimentalInputExtentMode;
extern TAutoConsoleVariable<float> CVarMetalFXMotionVectorScaleX;
extern TAutoConsoleVariable<float> CVarMetalFXMotionVectorScaleY;
extern TAutoConsoleVariable<float> CVarMetalFXSharpness;
extern TAutoConsoleVariable<int32> CVarMetalFXUpscalerMode;
extern TAutoConsoleVariable<int32> CVarMetalFXQualityMode;
extern TAutoConsoleVariable<bool> CVarMetalFXAutoScalingFromEngine;

UCLASS(Config = Engine, DefaultConfig, DisplayName = "Apple MetalFX")
class METALFX_API UMetalFXSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetalFXSettings(const FObjectInitializer& ObjectInitializer);

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
	EMetalFXUpscalerType UpscalerMode;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.MetalFX.Sharpness", DisplayName = "Sharpness", ClampMin = 0, ClampMax = 1, ToolTip = "Applies direct linear RCAS strength after MetalFX. 0 disables sharpening and 1 applies the maximum supported strength."))
	float Sharpness;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.MetalFX.QualityMode", DisplayName = "Quality Mode", ToolTip = "Selects the default quality mode to be used when upscaling with MetalFX."))
	EMetalFXQualityMode QualityMode;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.MetalFX.AutoScalingFromEngine", DisplayName = "Scale From Engine Resolution", ToolTip = "Uses the engine-selected resolution as the MetalFX output target and applies the quality scale to its input. Disable to retain the legacy output target and absolute primary screen percentage."))
	bool bAutoScalingFromEngine;

	UPROPERTY(Config, EditAnywhere, Category = "Temporal Settings", meta = (ConsoleVariable = "r.MetalFX.JitterMode", DisplayName = "Jitter Mode", ToolTip = "Controls temporal jitter forwarding. 1: normal, 0: disabled, -1: inverted."))
	int32 JitterMode;

	UPROPERTY(Config, EditAnywhere, Category = "Temporal Settings", meta = (ConsoleVariable = "r.MetalFX.MotionVectorScaleX", DisplayName = "Motion Vector Scale X", ToolTip = "Scales the horizontal pixel-space motion vector passed to MetalFX. The normal value is 1."))
	float MotionVectorScaleX;

	UPROPERTY(Config, EditAnywhere, Category = "Temporal Settings", meta = (ConsoleVariable = "r.MetalFX.MotionVectorScaleY", DisplayName = "Motion Vector Scale Y", ToolTip = "Scales the vertical pixel-space motion vector passed to MetalFX. The normal value is 1."))
	float MotionVectorScaleY;

};
