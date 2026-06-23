#include "MetalFXSettings.h"
//-----------------------콘솔 명령어-----------------------
TAutoConsoleVariable<bool> CVarEnableMetalFX(
	TEXT("r.MetalFX.Enabled"),
	false,
	TEXT("Enable MetalFX for Temporal Upscale"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<bool> CvarEnableMetalFXInEditor(
	TEXT("r.MetalFX.EnableInEditor"),
	false,
	TEXT("Enable MetalFX in editor viewports"),
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
	1,
	TEXT("Upscaler mode to be used when upscaling with MetalFX"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarMetalFXQualityMode(
	TEXT("r.MetalFX.QualityMode"),
	1,
	TEXT("Quality mode to be used when upscaling with MetalFX"),
	ECVF_RenderThreadSafe);

//------------------------
UMetalFXSettings::UMetalFXSettings(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	bEnabled = false;
	Sharpness = 0.0f;
	QualityMode = EMetalFXQualityMode::Balanced;
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
}
#endif
