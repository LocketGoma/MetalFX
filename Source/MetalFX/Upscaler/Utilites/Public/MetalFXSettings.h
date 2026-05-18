#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "HAL/IConsoleManager.h"


#include "MetalFXSettings.generated.h"

UENUM(BlueprintType)
enum class EMetalFXUpscalerMode : uint8
{
	None UMETA(DisplayName = "Disabled"),
	Spatial UMETA(DisplayName = "Spatial Upscaler"),
	Temporal UMETA(DisplayName = "Temporal Upscaler"),
	MAX
};

UENUM(BlueprintType)
enum class EMetalFXQualityMode : uint8
{
	NativeAA UMETA(DisplayName = "Native AA"),
	Quality UMETA(DisplayName = "Quality"),
	Balanced UMETA(DisplayName = "Balanced"),
	Performance UMETA(DisplayName = "Performance"),
	UltraPerformance UMETA(DisplayName = "Ultra Performance"),
	MAX
};

extern TAutoConsoleVariable<bool> CvarEnableMetalFX;
extern TAutoConsoleVariable<bool> CvarEnableMetalFXInEditor;
extern TAutoConsoleVariable<float> CVarMetalFXSharpness;
extern TAutoConsoleVariable<int32> CVarMetalFXQualityMode;

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
	
	UPROPERTY(Config, EditAnywhere, Category = "General Settings", meta = (ConsoleVariable = "r.MetalFX.UpscalerMode", DisplayName = "UpscalerMode"))
	int32 UpscalerMode;

	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.MetalFX.Sharpness", DisplayName = "Sharpness", ClampMin = 0, ClampMax = 1, ToolTip = "When greater than 0.0 this enables Robust Contrast Adaptive Sharpening Filter to sharpen the output image."))
	float Sharpness;
	
	UPROPERTY(Config, EditAnywhere, Category = "Quality Settings", meta = (ConsoleVariable = "r.MetalFX.QualityMode", DisplayName = "Quality Mode", ToolTip = "Selects the default quality mode to be used when upscaling with MetalFX."))
	EMetalFXQualityMode QualityMode;
	
};
