#include "MetalFXSettings.h"
#include "MetalFXHelper.h"

static void ResetScreenPercentageCurrentSetBy(IConsoleVariable* CVarScreenPercentage)
{
	CVarScreenPercentage->Unset(static_cast<EConsoleVariableFlags>(CVarScreenPercentage->GetFlags() & ECVF_SetByMask));
}

void ApplyMetalFXQualityModeToScreenPercentage(EMetalFXQualityMode QualityMode)
{
	if (!CVarEnableMetalFX.GetValueOnGameThread())
	{
		return;
	}

	if (IConsoleVariable* CVarScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ResetScreenPercentageCurrentSetBy(CVarScreenPercentage);
		CVarScreenPercentage->Set(ConvertMetalFXQualityModeToScreenPercentage(QualityMode), ECVF_SetByCode);
	}
}

static bool NeedRestoreScreenPercentageOnDisable()
{
#if UE_BUILD_SHIPPING
	return true;
#endif
//if Debug Mode On -> Disable Screenpercentage Auto Restore
  return (!METALFX_DEBUG);
}

static void RestoreScreenPercentage(EConsoleVariableFlags SetBy = ECVF_SetByCode)
{
	if (IConsoleVariable* CVarScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ResetScreenPercentageCurrentSetBy(CVarScreenPercentage);
		CVarScreenPercentage->Set(100.0f, SetBy);
	}
}

// Apply the last selected QualityMode when MetalFX is enabled at runtime.
static void HandleEnabledChanged(IConsoleVariable* Variable)
{
	if (!Variable)
	{
		return;
	}

	if (Variable->GetBool())
	{
		ApplyMetalFXQualityModeToScreenPercentage(static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread()));
	}
	else if (NeedRestoreScreenPercentageOnDisable())
	{
		RestoreScreenPercentage();
	}
}

// For runtime QualityMode changes from CVar.
static void HandleQualityModeChanged(IConsoleVariable* Variable)
{
	if (!Variable)
	{
		return;
	}

	ApplyMetalFXQualityModeToScreenPercentage(static_cast<EMetalFXQualityMode>(Variable->GetInt()));
}

//-----------------------콘솔 명령어-----------------------
TAutoConsoleVariable<bool> CVarEnableMetalFX(
	TEXT("r.MetalFX.Enabled"),
	false,
	TEXT("Enable MetalFX for Temporal Upscale"),
	FConsoleVariableDelegate::CreateStatic(&HandleEnabledChanged),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<bool> CvarEnableMetalFXInEditor(
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
	0.0f,
	TEXT("Horizontal scale applied by MetalFX to the motion vector texture. Keep 0 until the Unreal velocity conversion pass is enabled."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarMetalFXMotionVectorScaleY(
	TEXT("r.MetalFX.MotionVectorScaleY"),
	0.0f,
	TEXT("Vertical scale applied by MetalFX to the motion vector texture. Keep 0 until the Unreal velocity conversion pass is enabled."),
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
	static_cast<int32>(EMetalFXQualityMode::NativeAA),
	TEXT("Quality mode to be used when upscaling with MetalFX. 0: 100%, 1: 66.7%, 2: 50%, 3: 35%. MetalFX TemporalScaler does not support greater than 3x upscaling, so 25% Ultra Performance is disabled."),
	FConsoleVariableDelegate::CreateStatic(&HandleQualityModeChanged),
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
	QualityMode = EMetalFXQualityMode::NativeAA;
	JitterMode = 1;
	MotionVectorScaleX = 0.0f;
	MotionVectorScaleY = 0.0f;
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
	if(IsTemplate())
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

		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMetalFXSettings, QualityMode))
		{
			ApplyMetalFXQualityModeToScreenPercentage(QualityMode);
		}
	}
}
#endif
