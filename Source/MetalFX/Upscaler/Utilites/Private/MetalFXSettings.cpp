#include "MetalFXSettings.h"
#include "MetalFXHelper.h"

#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "LegacyScreenPercentageDriver.h"
#include "SceneView.h"

namespace
{
struct FMetalFXScreenPercentageState
{
	float Value = 0.0f;
	EConsoleVariableFlags SetBy = ECVF_SetByConstructor;
	EConsoleVariableFlags AppliedSetBy = ECVF_SetByConstructor;
	float LastObservedValue = 0.0f;
	EConsoleVariableFlags LastObservedSetBy = ECVF_SetByConstructor;
	float EngineBaseResolutionFraction = 1.0f;
	float StaticEngineBaseResolutionFraction = 1.0f;
	float LastAppliedResolutionFraction = 1.0f;
	FStaticResolutionFractionHeuristic::FUserSettings StaticHeuristicSettings;
	EMetalFXQualityMode LastQualityMode = EMetalFXQualityMode::MAX;
	bool bLastAutoScalingFromEngine = true;
	bool bDynamicResolutionActive = false;
	bool bStaticBaseFractionValid = false;
	bool bStaticHeuristicSettingsCaptured = false;
	bool bStaticHeuristicValid = false;
	bool bBaseFractionValid = false;
	bool bValid = false;
};

static IConsoleVariable* GetScreenPercentageCVar()
{
	// r.ScreenPercentage is owned by Engine and outlives this plugin module, so
	// this cached pointer remains valid across MetalFX hot reloads.
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	return CVar;
}

static bool IsValidResolutionFraction(float ResolutionFraction)
{
	return FMath::IsFinite(ResolutionFraction)
		&& ResolutionFraction >= ISceneViewFamilyScreenPercentage::kMinResolutionFraction
		&& ResolutionFraction <= ISceneViewFamilyScreenPercentage::kMaxResolutionFraction;
}

static bool TryGetDynamicResolutionFraction(float& OutResolutionFraction)
{
	if (!GEngine)
	{
		return false;
	}

	FDynamicResolutionStateInfos DynamicResolutionInfos;
	GEngine->GetDynamicResolutionCurrentStateInfos(DynamicResolutionInfos);
	if (DynamicResolutionInfos.Status != EDynamicResolutionStatus::Enabled
		&& DynamicResolutionInfos.Status != EDynamicResolutionStatus::DebugForceEnabled)
	{
		return false;
	}

	const float DynamicResolutionFraction = DynamicResolutionInfos.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction];
	if (!IsValidResolutionFraction(DynamicResolutionFraction))
	{
		return false;
	}

	OutResolutionFraction = DynamicResolutionFraction;
	return true;
}

static bool TryGetScreenPercentageInterfaceFraction(const FSceneViewFamily& ViewFamily, float& OutResolutionFraction)
{
	const ISceneViewFamilyScreenPercentage* ScreenPercentageInterface = ViewFamily.GetScreenPercentageInterface();
	if (!ScreenPercentageInterface)
	{
		return false;
	}

	const DynamicRenderScaling::TMap<float> UpperBounds = ScreenPercentageInterface->GetResolutionFractionsUpperBound();
	const float PrimaryUpperBound = UpperBounds[GDynamicPrimaryResolutionFraction];
	if (!IsValidResolutionFraction(PrimaryUpperBound))
	{
		return false;
	}

	OutResolutionFraction = PrimaryUpperBound;
	return true;
}

static EViewStatusForScreenPercentage GetViewStatusForScreenPercentage(const FSceneViewFamily& ViewFamily)
{
	if (ViewFamily.EngineShowFlags.PathTracing)
	{
		return EViewStatusForScreenPercentage::PathTracer;
	}
	if (ViewFamily.EngineShowFlags.StereoRendering || ViewFamily.EngineShowFlags.VREditing)
	{
		return EViewStatusForScreenPercentage::VR;
	}
	if (!ViewFamily.bRealtimeUpdate)
	{
		return EViewStatusForScreenPercentage::NonRealtime;
	}
	if (ViewFamily.Scene && ViewFamily.Scene->GetShadingPath() == EShadingPath::Mobile)
	{
		return EViewStatusForScreenPercentage::Mobile;
	}
	return EViewStatusForScreenPercentage::Desktop;
}

static void PullStaticHeuristicSettings(
	const FSceneViewFamily& ViewFamily,
	FStaticResolutionFractionHeuristic::FUserSettings& OutSettings)
{
	const EViewStatusForScreenPercentage ViewStatus = GetViewStatusForScreenPercentage(ViewFamily);
#if WITH_EDITOR
	if (GIsEditor && !ViewFamily.EngineShowFlags.Game)
	{
		OutSettings.PullEditorRenderingSettings(ViewStatus);
		return;
	}
#endif
	OutSettings.PullRunTimeRenderingSettings(ViewStatus);
}

static bool ResolveStaticHeuristicFraction(
	const FSceneViewFamily& ViewFamily,
	const FStaticResolutionFractionHeuristic::FUserSettings& Settings,
	float& OutResolutionFraction)
{
	if (ViewFamily.Views.Num() == 0)
	{
		return false;
	}

	FStaticResolutionFractionHeuristic StaticHeuristic;
	StaticHeuristic.Settings = Settings;
	StaticHeuristic.PullViewFamilyRenderingSettings(ViewFamily);
	StaticHeuristic.DPIScale = ViewFamily.DebugDPIScale > 0.0f ? ViewFamily.DebugDPIScale : 1.0f;
	const float ResolutionFraction = StaticHeuristic.ResolveResolutionFraction();
	if (!IsValidResolutionFraction(ResolutionFraction))
	{
		return false;
	}

	OutResolutionFraction = ResolutionFraction;
	return true;
}

static bool TryGetEngineBaseResolutionFraction(const FSceneViewFamily& ViewFamily, float& OutResolutionFraction, const TCHAR*& OutSource)
{
	if (TryGetDynamicResolutionFraction(OutResolutionFraction))
	{
		OutSource = TEXT("DynamicResolutionApproximation");
		return true;
	}

	if (TryGetScreenPercentageInterfaceFraction(ViewFamily, OutResolutionFraction))
	{
		// FLegacyScreenPercentageDriver's upper bound is its exact static value.
		// Custom drivers expose only an upper bound on the game thread; using it is
		// the safest non-invasive approximation without replacing the interface.
		OutSource = TEXT("ScreenPercentageInterface");
		return true;
	}

	if (ViewFamily.Views.Num() > 0)
	{
		FStaticResolutionFractionHeuristic::FUserSettings Settings;
		PullStaticHeuristicSettings(ViewFamily, Settings);
		if (ResolveStaticHeuristicFraction(ViewFamily, Settings, OutResolutionFraction))
		{
			OutSource = TEXT("StaticResolutionFractionHeuristic");
			return true;
		}
	}

	OutResolutionFraction = 1.0f;
	OutSource = TEXT("Fallback");
	UE_LOG(LogMetalFX, Warning, TEXT("MetalFX could not resolve the engine primary resolution fraction; falling back to 1.0."));
	return false;
}

class FMetalFXScreenPercentageController
{
public:
	static FMetalFXScreenPercentageController& Get()
	{
		static FMetalFXScreenPercentageController Controller;
		return Controller;
	}

	void ApplyQualityMode(EMetalFXQualityMode QualityMode)
	{
		if (!State.bValid || !State.bBaseFractionValid || !CVarEnableMetalFX.GetValueOnGameThread())
		{
			return;
		}

		const bool bAutoScalingFromEngine = CVarMetalFXAutoScalingFromEngine.GetValueOnGameThread();
#if METALFX_DEBUG
		UpdateAppliedResolutionFraction(QualityMode, bAutoScalingFromEngine);
#else
		ApplyToCVar(QualityMode, bAutoScalingFromEngine, !State.bDynamicResolutionActive);
#endif
	}

	bool ApplyToViewFamily(
		FSceneViewFamily& ViewFamily,
		EMetalFXQualityMode QualityMode,
		FMetalFXResolutionDebugInfo* OutDebugInfo)
	{
		if (OutDebugInfo)
		{
			*OutDebugInfo = FMetalFXResolutionDebugInfo();
		}

		if (!CVarEnableMetalFX.GetValueOnGameThread())
		{
			return false;
		}

		IConsoleVariable* ScreenPercentage = GetScreenPercentageCVar();
		if (!ScreenPercentage)
		{
			UE_LOG(LogMetalFX, Warning, TEXT("MetalFX could not find r.ScreenPercentage; leaving the engine resolution unchanged."));
			return false;
		}

		if (!State.bValid)
		{
			State.Value = ScreenPercentage->GetFloat();
			State.SetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
			State.AppliedSetBy = State.SetBy == ECVF_SetByConstructor ? ECVF_SetByCode : State.SetBy;

#if METALFX_DEBUG
			State.LastObservedValue = State.Value;
			State.LastObservedSetBy = State.SetBy;
#else
			if (State.Value <= 0.0f)
			{
				// Preserve the Auto-mode inputs before the production path takes
				// ownership of r.ScreenPercentage.
				PullStaticHeuristicSettings(ViewFamily, State.StaticHeuristicSettings);
				State.bStaticHeuristicSettingsCaptured = true;
			}
#endif
			State.bValid = true;
		}

#if METALFX_DEBUG
		const float CurrentScreenPercentage = ScreenPercentage->GetFloat();
		const EConsoleVariableFlags CurrentScreenPercentageSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
		const bool bScreenPercentageChanged = !FMath::IsNearlyEqual(CurrentScreenPercentage, State.LastObservedValue)
			|| CurrentScreenPercentageSetBy != State.LastObservedSetBy;
		State.LastObservedValue = CurrentScreenPercentage;
		State.LastObservedSetBy = CurrentScreenPercentageSetBy;
#endif

#if METALFX_DEBUG
		const TCHAR* BaseSource = TEXT("Unknown");
#else
		const TCHAR* BaseSource = TEXT("StaticCached");
#endif
		float DynamicResolutionFraction = 1.0f;
		const bool bDynamicResolutionActive = TryGetDynamicResolutionFraction(DynamicResolutionFraction);

#if METALFX_DEBUG
		if (bDynamicResolutionActive)
		{
			State.EngineBaseResolutionFraction = DynamicResolutionFraction;
			State.bBaseFractionValid = true;
			BaseSource = TEXT("DynamicResolutionApproximation");
		}
		else if (CurrentScreenPercentage > 0.0f
			&& IsValidResolutionFraction(CurrentScreenPercentage / 100.0f))
		{
			// A positive r.ScreenPercentage is Unreal's explicit manual fraction.
			// Read it directly so a runtime console change is visible immediately,
			// even if this frame's prebuilt ScreenPercentageInterface is stale.
			State.EngineBaseResolutionFraction = CurrentScreenPercentage / 100.0f;
			State.bBaseFractionValid = true;
			BaseSource = TEXT("r.ScreenPercentage");
		}
		else
		{
			TryGetEngineBaseResolutionFraction(ViewFamily, State.EngineBaseResolutionFraction, BaseSource);
			State.bBaseFractionValid = true;
		}
#else
		if (!bDynamicResolutionActive && !State.bStaticBaseFractionValid)
		{
			float InterfaceResolutionFraction = 1.0f;
			if (TryGetScreenPercentageInterfaceFraction(ViewFamily, InterfaceResolutionFraction))
			{
				State.StaticEngineBaseResolutionFraction = InterfaceResolutionFraction;
				State.bStaticBaseFractionValid = true;
				BaseSource = TEXT("ScreenPercentageInterface");

				float HeuristicResolutionFraction = 1.0f;
				if (State.bStaticHeuristicSettingsCaptured
					&& ResolveStaticHeuristicFraction(ViewFamily, State.StaticHeuristicSettings, HeuristicResolutionFraction)
					&& FMath::IsNearlyEqual(HeuristicResolutionFraction, InterfaceResolutionFraction, 0.001f))
				{
					State.bStaticHeuristicValid = true;
					State.StaticEngineBaseResolutionFraction = HeuristicResolutionFraction;
				}
			}
		}

		if (bDynamicResolutionActive)
		{
			State.EngineBaseResolutionFraction = DynamicResolutionFraction;
			State.bBaseFractionValid = true;
			BaseSource = TEXT("DynamicResolutionApproximation");
		}
		else if (State.bStaticHeuristicValid
			&& ResolveStaticHeuristicFraction(ViewFamily, State.StaticHeuristicSettings, State.EngineBaseResolutionFraction))
		{
			State.StaticEngineBaseResolutionFraction = State.EngineBaseResolutionFraction;
			State.bStaticBaseFractionValid = true;
			State.bBaseFractionValid = true;
			BaseSource = TEXT("CapturedStaticResolutionHeuristic");
		}
		else if (State.bStaticBaseFractionValid)
		{
			State.EngineBaseResolutionFraction = State.StaticEngineBaseResolutionFraction;
			State.bBaseFractionValid = true;
		}
		else
		{
			TryGetEngineBaseResolutionFraction(ViewFamily, State.EngineBaseResolutionFraction, BaseSource);
			State.bBaseFractionValid = true;
			State.StaticEngineBaseResolutionFraction = State.EngineBaseResolutionFraction;
			State.bStaticBaseFractionValid = true;

			float HeuristicResolutionFraction = 1.0f;
			if (State.bStaticHeuristicSettingsCaptured
				&& ResolveStaticHeuristicFraction(ViewFamily, State.StaticHeuristicSettings, HeuristicResolutionFraction)
				&& FMath::IsNearlyEqual(HeuristicResolutionFraction, State.EngineBaseResolutionFraction, 0.001f))
			{
				State.bStaticHeuristicValid = true;
				State.EngineBaseResolutionFraction = HeuristicResolutionFraction;
				State.StaticEngineBaseResolutionFraction = HeuristicResolutionFraction;
				BaseSource = TEXT("CapturedStaticResolutionHeuristic");
			}
		}
#endif
		State.bDynamicResolutionActive = bDynamicResolutionActive;

		const bool bAutoScalingFromEngine = CVarMetalFXAutoScalingFromEngine.GetValueOnGameThread();
#if METALFX_DEBUG
		UpdateAppliedResolutionFraction(QualityMode, bAutoScalingFromEngine);
#else
		ApplyToCVar(QualityMode, bAutoScalingFromEngine, !State.bDynamicResolutionActive);
#endif

#if METALFX_DEBUG
		if (bScreenPercentageChanged)
		{
			UE_LOG(LogMetalFX, Log,
				TEXT("MetalFX observed external r.ScreenPercentage change: Value=%.3f SetBy=%s ActiveBase=%.3f BaseSource=%s DynamicResolution=%s"),
				CurrentScreenPercentage,
				GetConsoleVariableSetByName(CurrentScreenPercentageSetBy),
				State.EngineBaseResolutionFraction,
				BaseSource,
				bDynamicResolutionActive ? TEXT("true") : TEXT("false"));
		}
#endif

		const float PreviousSecondaryResolutionFraction = ViewFamily.SecondaryViewFraction;
		const float OutputResolutionFraction = CalculateOutputResolutionFraction(
			QualityMode,
			bAutoScalingFromEngine,
			State.EngineBaseResolutionFraction,
			PreviousSecondaryResolutionFraction);
		const float PrimaryResolutionFraction = GetMetalFXQualitySettings(QualityMode)
			.GetPrimaryResolutionFraction(bAutoScalingFromEngine);

		// Unreal sends a third-party temporal/spatial upscaler the Secondary view
		// rect as its output target. Move the engine-selected base fraction to the
		// Secondary stage, then express MetalFX quality as a Primary fraction of
		// that target. For example, Base=0.6 and Quality=0.667 becomes a 0.4
		// Primary input and a 0.6 MetalFX output instead of a 1.0 output.
		ViewFamily.SecondaryViewFraction = OutputResolutionFraction;

		if (OutDebugInfo)
		{
			OutDebugInfo->QualityMode = QualityMode;
			OutDebugInfo->EngineBaseResolutionFraction = State.EngineBaseResolutionFraction;
			OutDebugInfo->PrimaryResolutionFraction = PrimaryResolutionFraction;
			OutDebugInfo->OutputResolutionFraction = OutputResolutionFraction;
			OutDebugInfo->FinalInputResolutionFraction = PrimaryResolutionFraction * OutputResolutionFraction;
			OutDebugInfo->bAutoScalingFromEngine = bAutoScalingFromEngine;
			OutDebugInfo->bDynamicResolutionActive = bDynamicResolutionActive;
			OutDebugInfo->bIsValid = true;
		}

		if (State.LastQualityMode != QualityMode || State.bLastAutoScalingFromEngine != bAutoScalingFromEngine)
		{
			const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(QualityMode);
			UE_LOG(LogMetalFX, Log,
				TEXT("MetalFX ScreenPercentage ON: ActivationValue=%.3f ActivationSetBy=%s ActivationAutoState=%s Base=%.3f BaseSource=%s AutoScalingFromEngine=%s QualityMode=%s RelativeScale=%.3f Absolute=%.3f PreviousSecondary=%.3f Primary=%.3f Output=%.3f FinalInput=%.3f"),
				State.Value,
				GetConsoleVariableSetByName(State.SetBy),
				State.Value <= 0.0f ? TEXT("true") : TEXT("false"),
				State.EngineBaseResolutionFraction,
				BaseSource,
				bAutoScalingFromEngine ? TEXT("true") : TEXT("false"),
				Quality.Name,
				Quality.RelativeScale,
				Quality.AbsoluteScreenPercentage,
				PreviousSecondaryResolutionFraction,
				PrimaryResolutionFraction,
				OutputResolutionFraction,
				PrimaryResolutionFraction * OutputResolutionFraction);
		}

		State.LastQualityMode = QualityMode;
		State.bLastAutoScalingFromEngine = bAutoScalingFromEngine;
		return true;
	}

	void Restore()
	{
		if (!State.bValid)
		{
			return;
		}

		if (IConsoleVariable* ScreenPercentage = GetScreenPercentageCVar())
		{
			EConsoleVariableFlags CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
#if METALFX_DEBUG
			UE_LOG(LogMetalFX, Log,
				TEXT("MetalFX ScreenPercentage OFF: Preserved=%.3f PreservedSetBy=%s ActivationValue=%.3f ActivationSetBy=%s"),
				ScreenPercentage->GetFloat(),
				GetConsoleVariableSetByName(CurrentSetBy),
				State.Value,
				GetConsoleVariableSetByName(State.SetBy));
#else
			if (CurrentSetBy != State.AppliedSetBy && CurrentSetBy != State.SetBy)
			{
				// MetalFX owns the active value in production. Remove a competing
				// priority layer before restoring the activation-time state.
				UE_LOG(LogMetalFX, Verbose,
					TEXT("r.ScreenPercentage SetBy changed while MetalFX was active (%s -> %s); restoring the activation-time state."),
					GetConsoleVariableSetByName(State.SetBy),
					GetConsoleVariableSetByName(CurrentSetBy));
				ScreenPercentage->Unset(CurrentSetBy);
				CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
			}

			if (State.AppliedSetBy != State.SetBy && CurrentSetBy == State.AppliedSetBy)
			{
				ScreenPercentage->Unset(State.AppliedSetBy);
				CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
			}

			if (CurrentSetBy == State.SetBy)
			{
				if (State.SetBy != ECVF_SetByConstructor && !FMath::IsNearlyEqual(ScreenPercentage->GetFloat(), State.Value))
				{
					ScreenPercentage->ReplaceCurrentPriorityAndTag(State.Value);
				}
			}
			else
			{
				ScreenPercentage->Set(State.Value, State.SetBy);
			}

			const EConsoleVariableFlags RestoredSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
			if (!FMath::IsNearlyEqual(ScreenPercentage->GetFloat(), State.Value) || RestoredSetBy != State.SetBy)
			{
				UE_LOG(LogMetalFX, Warning,
					TEXT("MetalFX did not restore r.ScreenPercentage exactly. Expected=%.3f/%s Actual=%.3f/%s"),
					State.Value,
					GetConsoleVariableSetByName(State.SetBy),
					ScreenPercentage->GetFloat(),
					GetConsoleVariableSetByName(RestoredSetBy));
			}

			UE_LOG(LogMetalFX, Log,
				TEXT("MetalFX ScreenPercentage OFF: Restored=%.3f RestoredSetBy=%s AutoState=%s"),
				ScreenPercentage->GetFloat(),
				GetConsoleVariableSetByName(RestoredSetBy),
				State.Value <= 0.0f ? TEXT("true") : TEXT("false"));
#endif
		}

		State = FMetalFXScreenPercentageState();
	}

private:
	static float CalculateOutputResolutionFraction(
		EMetalFXQualityMode QualityMode,
		bool bAutoScalingFromEngine,
		float EngineBaseResolutionFraction,
		float PreviousSecondaryResolutionFraction)
	{
		const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(QualityMode);
		if (Quality.bForceNativeResolution)
		{
			return 1.0f;
		}

		// Absolute mode retains its legacy full-output contract. Moving a smaller
		// engine base to the output in this mode could make an absolute input (for
		// example 100%) larger than the MetalFX output and request a downscale.
		const float OutputResolutionFraction = bAutoScalingFromEngine
			? PreviousSecondaryResolutionFraction * EngineBaseResolutionFraction
			: PreviousSecondaryResolutionFraction;
		// METALFX_DEBUG exposes Unreal's supersampling range for validation;
		// production keeps the MetalFX output target at or below NativeAA.
		return FMath::Clamp(
			OutputResolutionFraction,
			ISceneViewFamilyScreenPercentage::kMinResolutionFraction,
			GetMetalFXMaxUpscaleResolutionFraction());
	}

	static float CalculateEffectiveInputResolutionFraction(
		EMetalFXQualityMode QualityMode,
		bool bAutoScalingFromEngine,
		float EngineBaseResolutionFraction)
	{
		const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(QualityMode);
		if (Quality.bForceNativeResolution)
		{
			return 1.0f;
		}

		const float FinalResolutionFraction = bAutoScalingFromEngine
			? EngineBaseResolutionFraction * Quality.RelativeScale
			: Quality.AbsoluteScreenPercentage / 100.0f;
		return FMath::Clamp(
			FinalResolutionFraction,
			ISceneViewFamilyScreenPercentage::kMinResolutionFraction,
			ISceneViewFamilyScreenPercentage::kMaxResolutionFraction);
	}

	void UpdateAppliedResolutionFraction(EMetalFXQualityMode QualityMode, bool bAutoScalingFromEngine)
	{
		State.LastAppliedResolutionFraction = CalculateEffectiveInputResolutionFraction(
			QualityMode,
			bAutoScalingFromEngine,
			State.EngineBaseResolutionFraction);
	}

#if !METALFX_DEBUG
	void ApplyToCVar(EMetalFXQualityMode QualityMode, bool bAutoScalingFromEngine, bool bWriteCVar)
	{
		UpdateAppliedResolutionFraction(QualityMode, bAutoScalingFromEngine);

		// The production path owns r.ScreenPercentage while MetalFX is active.
		// Dynamic Resolution supplies its fraction independently, so do not write
		// the static CVar while that path is active.
		if (!bWriteCVar)
		{
			return;
		}

		if (IConsoleVariable* ScreenPercentage = GetScreenPercentageCVar())
		{
			const float FinalScreenPercentage = GetMetalFXQualitySettings(QualityMode)
				.GetPrimaryResolutionFraction(bAutoScalingFromEngine) * 100.0f;

			if (!FMath::IsNearlyEqual(ScreenPercentage->GetFloat(), FinalScreenPercentage))
			{
				EConsoleVariableFlags CurrentSetBy = static_cast<EConsoleVariableFlags>(
					ScreenPercentage->GetFlags() & ECVF_SetByMask);

				if (CurrentSetBy != State.AppliedSetBy && CurrentSetBy != State.SetBy)
				{
					UE_LOG(LogMetalFX, Verbose,
						TEXT("Overriding external r.ScreenPercentage change while MetalFX is active: Value=%.3f SetBy=%s"),
						ScreenPercentage->GetFloat(),
						GetConsoleVariableSetByName(CurrentSetBy));
					ScreenPercentage->Unset(CurrentSetBy);
					CurrentSetBy = static_cast<EConsoleVariableFlags>(
						ScreenPercentage->GetFlags() & ECVF_SetByMask);
				}

				if (State.SetBy == ECVF_SetByConstructor)
				{
					ScreenPercentage->Set(FinalScreenPercentage, ECVF_SetByCode);
				}
				else if (CurrentSetBy == State.SetBy)
				{
					ScreenPercentage->ReplaceCurrentPriorityAndTag(FinalScreenPercentage);
				}
				else
				{
					ScreenPercentage->Set(FinalScreenPercentage, State.SetBy);
				}
			}
		}
	}
#endif

	FMetalFXScreenPercentageState State;
};

static void HandleEnabledChanged(IConsoleVariable* Variable)
{
	if (Variable && !Variable->GetBool())
	{
		FMetalFXScreenPercentageController::Get().Restore();
	}
	// Enabling is intentionally deferred until the first view family that can
	// actually install MetalFX, so the engine's unmodified driver supplies Base.
}

static void HandleQualityModeChanged(IConsoleVariable* Variable)
{
	if (Variable)
	{
		FMetalFXScreenPercentageController::Get().ApplyQualityMode(static_cast<EMetalFXQualityMode>(Variable->GetInt()));
	}
}

static void HandleAutoScalingChanged(IConsoleVariable*)
{
	FMetalFXScreenPercentageController::Get().ApplyQualityMode(
		static_cast<EMetalFXQualityMode>(CVarMetalFXQualityMode.GetValueOnGameThread()));
}
} // namespace

void ApplyMetalFXQualityModeToScreenPercentage(EMetalFXQualityMode QualityMode)
{
	FMetalFXScreenPercentageController::Get().ApplyQualityMode(QualityMode);
}

bool ApplyMetalFXScreenPercentageToViewFamily(
	FSceneViewFamily& ViewFamily,
	EMetalFXQualityMode QualityMode,
	FMetalFXResolutionDebugInfo* OutDebugInfo)
{
	return FMetalFXScreenPercentageController::Get().ApplyToViewFamily(ViewFamily, QualityMode, OutDebugInfo);
}

void RestoreMetalFXScreenPercentage()
{
	FMetalFXScreenPercentageController::Get().Restore();
}

//-----------------------Console variables-----------------------
TAutoConsoleVariable<bool> CVarEnableMetalFX(
	TEXT("r.MetalFX.Enabled"),
	false,
	TEXT("Enable MetalFX upscaling"),
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
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetalFXSettings, QualityMode)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetalFXSettings, bAutoScalingFromEngine))
		{
			ApplyMetalFXQualityModeToScreenPercentage(QualityMode);
		}
	}
}
#endif
