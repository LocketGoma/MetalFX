#include "MetalFXSettings.h"
#include "MetalFXHelper.h"

namespace
{
static void HandleEnabledChanged(IConsoleVariable* Variable)
{
	if (Variable && !Variable->GetBool())
	{
		RestoreMetalFXScreenPercentage();
	}
	// Enabling is intentionally deferred until the first view family that can
	// actually install MetalFX, so the engine's unmodified driver supplies Base.
}

static void HandleQualityModeChanged(IConsoleVariable* Variable)
{
	if (Variable)
	{
		ApplyMetalFXQualityModeToScreenPercentage(static_cast<EMetalFXQualityMode>(Variable->GetInt()));
	}
}

static void HandleAutoScalingChanged(IConsoleVariable*)
{
	ApplyMetalFXQualityModeToScreenPercentage(static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread()));
}
} // namespace

//-----------------------Console variables-----------------------
TAutoConsoleVariable<bool> CVarEnableMetalFX(
	TEXT("r.MetalFX.Enabled"),
	false,
	TEXT("Enable MetalFX upscaling"),
	FConsoleVariableDelegate::CreateStatic(&HandleEnabledChanged),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<bool> CVarEnableMetalFXInEditor(
	TEXT("r.MetalFX.EnableInEditor"),
	false,
	TEXT("Enable MetalFX in Play In Editor"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<bool> CVarMetalFXDebugDisplay(
	TEXT("r.MetalFX.DebugDisplay"),
	true,
	TEXT("Display MetalFX debug status on screen"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarMetalFXJitterMode(
	TEXT("r.MetalFX.JitterMode"),
	1,
	TEXT("Controls MetalFX temporal jitter. 1: normal, 0: disabled, -1: inverted."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarMetalFXExperimentalInputExtentMode(
	TEXT("r.MetalFX.Experimental.InputExtentMode"),
	1,
	TEXT("Experimental MetalFX descriptor input size source. 0: SceneColor.ViewRect, 1: SceneColor.Texture extent, 2: OutputViewRect. Default 1 matches MetalFX texture allocation size."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarMetalFXMotionVectorScaleX(
	TEXT("r.MetalFX.MotionVectorScaleX"),
	1.0f,
	TEXT("Horizontal scale applied by MetalFX to the pixel-space motion vector texture."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarMetalFXMotionVectorScaleY(
	TEXT("r.MetalFX.MotionVectorScaleY"),
	1.0f,
	TEXT("Vertical scale applied by MetalFX to the pixel-space motion vector texture."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarMetalFXSharpness(
	TEXT("r.MetalFX.Sharpness"),
	0.0f,
	TEXT("Range from 0.0 to 1.0, when greater than 0 this enables Robust Contrast Adaptive Sharpening Filter to sharpen the output image. Default is 0."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarMetalFXUpscalerMode(
	TEXT("r.MetalFX.UpscalerMode"),
	static_cast<int32>(EMetalFXUpscalerType::Temporal),
	TEXT("Upscaler mode to be used when upscaling with MetalFX"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarMetalFXQualityMode(
	TEXT("r.MetalFX.QualityMode"),
	static_cast<int32>(EMetalFXQualityMode::UltraQuality),
	TEXT("Quality mode. 0: NativeAA always 100%. With AutoScalingFromEngine=true: 1: UltraQuality base x 100% (1.0x), 2: Quality base x 66.7% (1.5x), 3: Balanced base x 50% (2.0x), 4: Performance base x 42% (~2.4x), 5: UltraPerformance base x 34% (~2.94x). With AutoScalingFromEngine=false, modes 1-5 use the corresponding absolute primary screen percentages."),
	FConsoleVariableDelegate::CreateStatic(&HandleQualityModeChanged),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<bool> CVarMetalFXAutoScalingFromEngine(
	TEXT("r.MetalFX.AutoScalingFromEngine"),
	true,
	TEXT("When true, use the engine-selected resolution as the MetalFX output target and apply the quality scale to its input. When false, retain the legacy output target and use the quality mode's absolute primary screen percentage."),
	FConsoleVariableDelegate::CreateStatic(&HandleAutoScalingChanged),
	ECVF_RenderThreadSafe);

//------------------------
UMetalFXSettings::UMetalFXSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEnabled = false;
	bEnableInEditor = false;
	bDebugDisplay = true;
	UpscalerMode = EMetalFXUpscalerType::Temporal;
	Sharpness = 0.0f;
	QualityMode = EMetalFXQualityMode::UltraQuality;
	bAutoScalingFromEngine = true;
	JitterMode = 1;
	MotionVectorScaleX = 1.0f;
	MotionVectorScaleY = 1.0f;
}

FName UMetalFXSettings::GetContainerName() const
{
	static const FName ContainerName("Project");
	return ContainerName;
}

FName UMetalFXSettings::GetCategoryName() const
{
	static const FName EditorCategoryName("Plugins");
	return EditorCategoryName;
}

FName UMetalFXSettings::GetSectionName() const
{
	static const FName EditorSectionName("MetalFX");
	return EditorSectionName;
}

void UMetalFXSettings::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	if (IsTemplate())
	{
		ImportConsoleVariableValues();
	}
#endif
}

#if WITH_EDITOR
void UMetalFXSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		ExportValuesToConsoleVariables(PropertyChangedEvent.Property);

		const FName PropertyName = PropertyChangedEvent.Property->GetFName();
		const bool bQualityModeChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UMetalFXSettings, QualityMode);
		const bool bAutoScalingModeChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UMetalFXSettings, bAutoScalingFromEngine);
		if (bQualityModeChanged || bAutoScalingModeChanged)
		{
			ApplyMetalFXQualityModeToScreenPercentage(QualityMode);
		}
	}
}
#endif
