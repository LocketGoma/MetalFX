#include "MetalFXHelper.h"

#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "LegacyScreenPercentageDriver.h"
#include "SceneView.h"

float GetMetalFXMaxUpscaleResolutionFraction()
{
#if METALFX_DEBUG
	// Temporarily expose Unreal's full Primary resolution range so values above
	// 100% can be exercised while validating MetalFX supersampling behavior.
	return ISceneViewFamilyScreenPercentage::kMaxResolutionFraction;
#else
	return ConvertMetalFXQualityModeToResolutionFraction(EMetalFXQualityMode::NativeAA);
#endif
}

namespace
{
struct FMetalFXScreenPercentageState
{
	float ActivationValue = 0.0f;
	EConsoleVariableFlags ActivationSetBy = ECVF_SetByConstructor;
	float EngineBaseResolutionFraction = 1.0f;
	EMetalFXQualityMode LastQualityMode = EMetalFXQualityMode::MAX;
	bool bLastAutoScalingFromEngine = true;
	bool bDynamicResolutionActive = false;
	bool bBaseFractionValid = false;
	bool bValid = false;
#if METALFX_DEBUG
	float LastObservedValue = 0.0f;
	EConsoleVariableFlags LastObservedSetBy = ECVF_SetByConstructor;
#else
	EConsoleVariableFlags MetalFXSetBy = ECVF_SetByConstructor;
	float StaticEngineBaseResolutionFraction = 1.0f;
	FStaticResolutionFractionHeuristic::FUserSettings StaticHeuristicSettings;
	bool bStaticBaseFractionValid = false;
	bool bStaticHeuristicSettingsCaptured = false;
	bool bStaticHeuristicValid = false;
#endif
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
	const bool bIsFinite = FMath::IsFinite(ResolutionFraction);
	const bool bMeetsMinimum = ResolutionFraction >= ISceneViewFamilyScreenPercentage::kMinResolutionFraction;
	const bool bMeetsMaximum = ResolutionFraction <= ISceneViewFamilyScreenPercentage::kMaxResolutionFraction;
	return bIsFinite && bMeetsMinimum && bMeetsMaximum;
}

static bool TryGetManualScreenPercentageFraction(const FSceneViewFamily& ViewFamily, float& OutResolutionFraction)
{
	if (!ViewFamily.EngineShowFlags.ScreenPercentage)
	{
		return false;
	}

	const IConsoleVariable* ScreenPercentage = GetScreenPercentageCVar();
	if (!ScreenPercentage)
	{
		return false;
	}

	const float ResolutionFraction = ScreenPercentage->GetFloat() / 100.0f;
	if (!IsValidResolutionFraction(ResolutionFraction))
	{
		return false;
	}

	OutResolutionFraction = ResolutionFraction;
	return true;
}

static bool TryGetDynamicResolutionFraction(const FSceneViewFamily& ViewFamily, float& OutResolutionFraction)
{
	if (!GEngine || !ViewFamily.EngineShowFlags.ScreenPercentage)
	{
		return false;
	}

	FDynamicResolutionStateInfos DynamicResolutionInfos;
	GEngine->GetDynamicResolutionCurrentStateInfos(DynamicResolutionInfos);
	const bool bIsEnabled = DynamicResolutionInfos.Status == EDynamicResolutionStatus::Enabled;
	const bool bIsDebugForceEnabled = DynamicResolutionInfos.Status == EDynamicResolutionStatus::DebugForceEnabled;
	if (!bIsEnabled && !bIsDebugForceEnabled)
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
	const bool bStereoRendering = ViewFamily.EngineShowFlags.StereoRendering;
	const bool bVREditing = ViewFamily.EngineShowFlags.VREditing;
	if (bStereoRendering || bVREditing)
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

static void PullStaticHeuristicSettings(const FSceneViewFamily& ViewFamily, FStaticResolutionFractionHeuristic::FUserSettings& OutSettings)
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

static bool ResolveStaticHeuristicFraction(const FSceneViewFamily& ViewFamily, const FStaticResolutionFractionHeuristic::FUserSettings& Settings, float& OutResolutionFraction)
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

static bool TryGetEngineBaseResolutionFraction(const FSceneViewFamily& ViewFamily, float& OutResolutionFraction, const TCHAR*& OutSource, bool* OutDynamicResolutionActive = nullptr)
{
	if (OutDynamicResolutionActive)
	{
		*OutDynamicResolutionActive = false;
	}

	if (TryGetDynamicResolutionFraction(ViewFamily, OutResolutionFraction))
	{
		OutSource = TEXT("DynamicResolutionApproximation");
		if (OutDynamicResolutionActive)
		{
			*OutDynamicResolutionActive = true;
		}
		return true;
	}

	if (TryGetManualScreenPercentageFraction(ViewFamily, OutResolutionFraction))
	{
		// A positive r.ScreenPercentage is Unreal's explicit manual fraction.
		// Read it directly so a runtime console change is visible immediately,
		// even if this frame's prebuilt ScreenPercentageInterface is stale.
		OutSource = TEXT("r.ScreenPercentage");
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

static float CalculateMetalFXOutputResolutionFraction(const FMetalFXQualitySettings& Quality, bool bAutoScalingFromEngine, float EngineBaseResolutionFraction, float PreviousSecondaryResolutionFraction)
{
	if (Quality.bForceNativeResolution)
	{
		return 1.0f;
	}

	// Absolute mode retains its legacy full-output contract. Moving a smaller
	// engine base to the output in this mode could make an absolute input larger
	// than the MetalFX output and request a downscale.
	const float OutputResolutionFraction = bAutoScalingFromEngine ? PreviousSecondaryResolutionFraction * EngineBaseResolutionFraction : PreviousSecondaryResolutionFraction;

	// METALFX_DEBUG exposes Unreal's supersampling range for validation;
	// production keeps the MetalFX output target at or below NativeAA.
	return FMath::Clamp(OutputResolutionFraction, ISceneViewFamilyScreenPercentage::kMinResolutionFraction, GetMetalFXMaxUpscaleResolutionFraction());
}

static FMetalFXResolutionDebugInfo BuildMetalFXResolutionInfo(EMetalFXQualityMode QualityMode, bool bAutoScalingFromEngine, bool bDynamicResolutionActive, float EngineBaseResolutionFraction, float PreviousSecondaryResolutionFraction)
{
	FMetalFXResolutionDebugInfo ResolutionInfo;
	const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(QualityMode);
	ResolutionInfo.QualityMode = QualityMode;
	ResolutionInfo.EngineBaseResolutionFraction = FMath::Clamp(EngineBaseResolutionFraction, ISceneViewFamilyScreenPercentage::kMinResolutionFraction, GetMetalFXMaxUpscaleResolutionFraction());
	ResolutionInfo.PrimaryResolutionFraction = Quality.GetPrimaryResolutionFraction();
	ResolutionInfo.OutputResolutionFraction = CalculateMetalFXOutputResolutionFraction(Quality, bAutoScalingFromEngine, ResolutionInfo.EngineBaseResolutionFraction, PreviousSecondaryResolutionFraction);
	ResolutionInfo.FinalInputResolutionFraction = ResolutionInfo.PrimaryResolutionFraction * ResolutionInfo.OutputResolutionFraction;
	ResolutionInfo.bAutoScalingFromEngine = bAutoScalingFromEngine;
	ResolutionInfo.bDynamicResolutionActive = bDynamicResolutionActive;
	ResolutionInfo.bIsValid = true;
	return ResolutionInfo;
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
		const bool bHasValidResolutionState = State.bValid && State.bBaseFractionValid;
		const bool bMetalFXEnabled = CVarEnableMetalFX.GetValueOnGameThread();
		if (!bHasValidResolutionState || !bMetalFXEnabled)
		{
			return;
		}

#if !METALFX_DEBUG
		ApplyToCVar(QualityMode, !State.bDynamicResolutionActive);
#endif
	}

	bool ApplyToViewFamily(FSceneViewFamily& ViewFamily, EMetalFXQualityMode QualityMode, FMetalFXResolutionDebugInfo* OutDebugInfo)
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

		CaptureActivationState(ViewFamily, *ScreenPercentage);
		const TCHAR* BaseSource = UpdateEngineBaseResolutionFraction(ViewFamily, *ScreenPercentage);
		const bool bDynamicResolutionActive = State.bDynamicResolutionActive;

		const bool bAutoScalingFromEngine = CVarMetalFXAutoScalingFromEngine.GetValueOnGameThread();
#if !METALFX_DEBUG
		ApplyToCVar(QualityMode, !State.bDynamicResolutionActive);
#endif

		const float PreviousSecondaryResolutionFraction = ViewFamily.SecondaryViewFraction;
		const FMetalFXResolutionDebugInfo ResolutionInfo = BuildMetalFXResolutionInfo(QualityMode, bAutoScalingFromEngine, bDynamicResolutionActive, State.EngineBaseResolutionFraction, PreviousSecondaryResolutionFraction);

		// Unreal sends a third-party temporal/spatial upscaler the Secondary view
		// rect as its output target. Move the engine-selected base fraction to the
		// Secondary stage, then express MetalFX quality as a Primary fraction of
		// that target. For example, Base=0.6 and Quality=0.667 becomes a 0.4
		// Primary input and a 0.6 MetalFX output instead of a 1.0 output.
		ViewFamily.SecondaryViewFraction = ResolutionInfo.OutputResolutionFraction;

		if (OutDebugInfo)
		{
			*OutDebugInfo = ResolutionInfo;
		}

		const bool bQualityModeChanged = State.LastQualityMode != QualityMode;
		const bool bAutoScalingModeChanged = State.bLastAutoScalingFromEngine != bAutoScalingFromEngine;
		if (bQualityModeChanged || bAutoScalingModeChanged)
		{
			const FMetalFXQualitySettings Quality = GetMetalFXQualitySettings(QualityMode);
			UE_LOG(LogMetalFX, Log, TEXT("MetalFX ScreenPercentage ON: ActivationValue=%.3f ActivationSetBy=%s ActivationAutoState=%s Base=%.3f BaseSource=%s AutoScalingFromEngine=%s QualityMode=%s InputFraction=%.3f InputPercentage=%.3f PreviousSecondary=%.3f Primary=%.3f Output=%.3f FinalInput=%.3f"), State.ActivationValue, GetConsoleVariableSetByName(State.ActivationSetBy), State.ActivationValue <= 0.0f ? TEXT("true") : TEXT("false"), State.EngineBaseResolutionFraction, BaseSource, bAutoScalingFromEngine ? TEXT("true") : TEXT("false"), Quality.Name, Quality.GetPrimaryResolutionFraction(), Quality.GetScreenPercentage(), PreviousSecondaryResolutionFraction, ResolutionInfo.PrimaryResolutionFraction, ResolutionInfo.OutputResolutionFraction, ResolutionInfo.FinalInputResolutionFraction);
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
			UE_LOG(LogMetalFX, Log, TEXT("MetalFX ScreenPercentage OFF: Preserved=%.3f PreservedSetBy=%s ActivationValue=%.3f ActivationSetBy=%s"), ScreenPercentage->GetFloat(), GetConsoleVariableSetByName(CurrentSetBy), State.ActivationValue, GetConsoleVariableSetByName(State.ActivationSetBy));
#else
			const bool bUsesMetalFXPriority = CurrentSetBy == State.MetalFXSetBy;
			const bool bUsesActivationPriority = CurrentSetBy == State.ActivationSetBy;
			if (!bUsesMetalFXPriority && !bUsesActivationPriority)
			{
				// MetalFX owns the active value in production. Remove a competing
				// priority layer before restoring the activation-time state.
				UE_LOG(LogMetalFX, Verbose, TEXT("r.ScreenPercentage SetBy changed while MetalFX was active (%s -> %s); restoring the activation-time state."), GetConsoleVariableSetByName(State.ActivationSetBy), GetConsoleVariableSetByName(CurrentSetBy));
				ScreenPercentage->Unset(CurrentSetBy);
				CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
			}

			const bool bMetalFXUsesSeparatePriority = State.MetalFXSetBy != State.ActivationSetBy;
			const bool bCurrentPriorityIsMetalFX = CurrentSetBy == State.MetalFXSetBy;
			if (bMetalFXUsesSeparatePriority && bCurrentPriorityIsMetalFX)
			{
				ScreenPercentage->Unset(State.MetalFXSetBy);
				CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
			}

			if (CurrentSetBy == State.ActivationSetBy)
			{
				const bool bHasReplaceableActivationPriority = State.ActivationSetBy != ECVF_SetByConstructor;
				const bool bActivationValueChanged = !FMath::IsNearlyEqual(ScreenPercentage->GetFloat(), State.ActivationValue);
				if (bHasReplaceableActivationPriority && bActivationValueChanged)
				{
					ScreenPercentage->ReplaceCurrentPriorityAndTag(State.ActivationValue);
				}
			}
			else
			{
				ScreenPercentage->Set(State.ActivationValue, State.ActivationSetBy);
			}

			const EConsoleVariableFlags RestoredSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
			const bool bActivationValueRestored = FMath::IsNearlyEqual(ScreenPercentage->GetFloat(), State.ActivationValue);
			const bool bActivationPriorityRestored = RestoredSetBy == State.ActivationSetBy;
			if (!bActivationValueRestored || !bActivationPriorityRestored)
			{
				UE_LOG(LogMetalFX, Warning, TEXT("MetalFX did not restore r.ScreenPercentage exactly. Expected=%.3f/%s Actual=%.3f/%s"), State.ActivationValue, GetConsoleVariableSetByName(State.ActivationSetBy), ScreenPercentage->GetFloat(), GetConsoleVariableSetByName(RestoredSetBy));
			}

			UE_LOG(LogMetalFX, Log, TEXT("MetalFX ScreenPercentage OFF: Restored=%.3f RestoredSetBy=%s AutoState=%s"), ScreenPercentage->GetFloat(), GetConsoleVariableSetByName(RestoredSetBy), State.ActivationValue <= 0.0f ? TEXT("true") : TEXT("false"));
#endif
		}

		State = FMetalFXScreenPercentageState();
	}

private:
	void CaptureActivationState(const FSceneViewFamily& ViewFamily, IConsoleVariable& ScreenPercentage)
	{
		if (State.bValid)
		{
			return;
		}

		State.ActivationValue = ScreenPercentage.GetFloat();
		State.ActivationSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage.GetFlags() & ECVF_SetByMask);

#if METALFX_DEBUG
		State.LastObservedValue = State.ActivationValue;
		State.LastObservedSetBy = State.ActivationSetBy;
#else
		State.MetalFXSetBy = State.ActivationSetBy == ECVF_SetByConstructor ? ECVF_SetByCode : State.ActivationSetBy;
		if (State.ActivationValue <= 0.0f)
		{
			// Preserve Auto-mode inputs before the production path takes ownership
			// of r.ScreenPercentage.
			PullStaticHeuristicSettings(ViewFamily, State.StaticHeuristicSettings);
			State.bStaticHeuristicSettingsCaptured = true;
		}
#endif
		State.bValid = true;
	}

	const TCHAR* UpdateEngineBaseResolutionFraction(const FSceneViewFamily& ViewFamily, IConsoleVariable& ScreenPercentage)
	{
		float DynamicResolutionFraction = 1.0f;
		State.bDynamicResolutionActive = TryGetDynamicResolutionFraction(ViewFamily, DynamicResolutionFraction);

#if METALFX_DEBUG
		const float CurrentScreenPercentage = ScreenPercentage.GetFloat();
		const EConsoleVariableFlags CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage.GetFlags() & ECVF_SetByMask);
		const bool bScreenPercentageValueChanged = !FMath::IsNearlyEqual(CurrentScreenPercentage, State.LastObservedValue);
		const bool bScreenPercentagePriorityChanged = CurrentSetBy != State.LastObservedSetBy;
		const bool bScreenPercentageChanged = bScreenPercentageValueChanged || bScreenPercentagePriorityChanged;
		State.LastObservedValue = CurrentScreenPercentage;
		State.LastObservedSetBy = CurrentSetBy;

		const TCHAR* BaseSource = TEXT("Unknown");
		if (State.bDynamicResolutionActive)
		{
			State.EngineBaseResolutionFraction = DynamicResolutionFraction;
			BaseSource = TEXT("DynamicResolutionApproximation");
		}
		else
		{
			// TryGetEngineBaseResolutionFraction reads a positive manual CVar before
			// the prebuilt interface, which can lag a runtime change by one family.
			TryGetEngineBaseResolutionFraction(ViewFamily, State.EngineBaseResolutionFraction, BaseSource);
		}

		State.bBaseFractionValid = true;
		if (bScreenPercentageChanged)
		{
			UE_LOG(LogMetalFX, Log, TEXT("MetalFX observed external r.ScreenPercentage change: Value=%.3f SetBy=%s ActiveBase=%.3f BaseSource=%s DynamicResolution=%s"), CurrentScreenPercentage, GetConsoleVariableSetByName(CurrentSetBy), State.EngineBaseResolutionFraction, BaseSource, State.bDynamicResolutionActive ? TEXT("true") : TEXT("false"));
		}
		return BaseSource;
#else
		const TCHAR* BaseSource = TEXT("StaticCached");
		if (!State.bDynamicResolutionActive && !State.bStaticBaseFractionValid)
		{
			CaptureStaticBaseResolutionFraction(ViewFamily, BaseSource);
		}

		if (State.bDynamicResolutionActive)
		{
			State.EngineBaseResolutionFraction = DynamicResolutionFraction;
			BaseSource = TEXT("DynamicResolutionApproximation");
		}
		else
		{
			bool bResolvedStaticHeuristic = false;
			if (State.bStaticHeuristicValid)
			{
				bResolvedStaticHeuristic = ResolveStaticHeuristicFraction(ViewFamily, State.StaticHeuristicSettings, State.EngineBaseResolutionFraction);
			}
			if (bResolvedStaticHeuristic)
			{
				State.StaticEngineBaseResolutionFraction = State.EngineBaseResolutionFraction;
				State.bStaticBaseFractionValid = true;
				BaseSource = TEXT("CapturedStaticResolutionHeuristic");
			}
			else if (State.bStaticBaseFractionValid)
			{
				State.EngineBaseResolutionFraction = State.StaticEngineBaseResolutionFraction;
			}
			else
			{
				TryGetEngineBaseResolutionFraction(ViewFamily, State.EngineBaseResolutionFraction, BaseSource);
				State.StaticEngineBaseResolutionFraction = State.EngineBaseResolutionFraction;
				State.bStaticBaseFractionValid = true;
				TryPromoteStaticHeuristic(ViewFamily, BaseSource);
			}
		}

		State.bBaseFractionValid = true;
		return BaseSource;
#endif
	}

#if !METALFX_DEBUG
	void CaptureStaticBaseResolutionFraction(const FSceneViewFamily& ViewFamily, const TCHAR*& OutBaseSource)
	{
		float InterfaceResolutionFraction = 1.0f;
		if (!TryGetScreenPercentageInterfaceFraction(ViewFamily, InterfaceResolutionFraction))
		{
			return;
		}

		State.StaticEngineBaseResolutionFraction = InterfaceResolutionFraction;
		State.bStaticBaseFractionValid = true;
		OutBaseSource = TEXT("ScreenPercentageInterface");

		float HeuristicResolutionFraction = 1.0f;
		bool bHeuristicResolved = false;
		if (State.bStaticHeuristicSettingsCaptured)
		{
			bHeuristicResolved = ResolveStaticHeuristicFraction(ViewFamily, State.StaticHeuristicSettings, HeuristicResolutionFraction);
		}
		bool bHeuristicMatchesInterface = false;
		if (bHeuristicResolved)
		{
			bHeuristicMatchesInterface = FMath::IsNearlyEqual(HeuristicResolutionFraction, InterfaceResolutionFraction, 0.001f);
		}
		if (bHeuristicMatchesInterface)
		{
			State.bStaticHeuristicValid = true;
			State.StaticEngineBaseResolutionFraction = HeuristicResolutionFraction;
		}
	}

	void TryPromoteStaticHeuristic(const FSceneViewFamily& ViewFamily, const TCHAR*& OutBaseSource)
	{
		float HeuristicResolutionFraction = 1.0f;
		bool bHeuristicResolved = false;
		if (State.bStaticHeuristicSettingsCaptured)
		{
			bHeuristicResolved = ResolveStaticHeuristicFraction(ViewFamily, State.StaticHeuristicSettings, HeuristicResolutionFraction);
		}
		bool bHeuristicMatchesBase = false;
		if (bHeuristicResolved)
		{
			bHeuristicMatchesBase = FMath::IsNearlyEqual(HeuristicResolutionFraction, State.EngineBaseResolutionFraction, 0.001f);
		}
		if (bHeuristicMatchesBase)
		{
			State.bStaticHeuristicValid = true;
			State.EngineBaseResolutionFraction = HeuristicResolutionFraction;
			State.StaticEngineBaseResolutionFraction = HeuristicResolutionFraction;
			OutBaseSource = TEXT("CapturedStaticResolutionHeuristic");
		}
	}

	void ApplyToCVar(EMetalFXQualityMode QualityMode, bool bWriteCVar)
	{
		// The production path owns r.ScreenPercentage while MetalFX is active.
		// Dynamic Resolution supplies its fraction independently, so do not write
		// the static CVar while that path is active.
		if (!bWriteCVar)
		{
			return;
		}

		if (IConsoleVariable* ScreenPercentage = GetScreenPercentageCVar())
		{
			const float FinalScreenPercentage = GetMetalFXQualitySettings(QualityMode).GetScreenPercentage();

			if (!FMath::IsNearlyEqual(ScreenPercentage->GetFloat(), FinalScreenPercentage))
			{
				EConsoleVariableFlags CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);

				const bool bUsesMetalFXPriority = CurrentSetBy == State.MetalFXSetBy;
				const bool bUsesActivationPriority = CurrentSetBy == State.ActivationSetBy;
				if (!bUsesMetalFXPriority && !bUsesActivationPriority)
				{
					UE_LOG(LogMetalFX, Verbose, TEXT("Overriding external r.ScreenPercentage change while MetalFX is active: Value=%.3f SetBy=%s"), ScreenPercentage->GetFloat(), GetConsoleVariableSetByName(CurrentSetBy));
					ScreenPercentage->Unset(CurrentSetBy);
					CurrentSetBy = static_cast<EConsoleVariableFlags>(ScreenPercentage->GetFlags() & ECVF_SetByMask);
				}

				if (State.ActivationSetBy == ECVF_SetByConstructor)
				{
					ScreenPercentage->Set(FinalScreenPercentage, ECVF_SetByCode);
				}
				else if (CurrentSetBy == State.ActivationSetBy)
				{
					ScreenPercentage->ReplaceCurrentPriorityAndTag(FinalScreenPercentage);
				}
				else
				{
					ScreenPercentage->Set(FinalScreenPercentage, State.ActivationSetBy);
				}
			}
		}
	}
#endif

	FMetalFXScreenPercentageState State;
};

} // namespace

void ApplyMetalFXQualityModeToScreenPercentage(EMetalFXQualityMode QualityMode)
{
	FMetalFXScreenPercentageController::Get().ApplyQualityMode(QualityMode);
}

bool ApplyMetalFXScreenPercentageToViewFamily(FSceneViewFamily& ViewFamily, EMetalFXQualityMode QualityMode, FMetalFXResolutionDebugInfo* OutDebugInfo)
{
	return FMetalFXScreenPercentageController::Get().ApplyToViewFamily(ViewFamily, QualityMode, OutDebugInfo);
}

FMetalFXResolutionDebugInfo GetConfiguredMetalFXResolutionDebugInfo(const FSceneViewFamily& ViewFamily, EMetalFXQualityMode QualityMode)
{
	float EngineBaseResolutionFraction = 1.0f;
	bool bDynamicResolutionActive = false;
	const TCHAR* BaseSource = TEXT("Unknown");
	TryGetEngineBaseResolutionFraction(ViewFamily, EngineBaseResolutionFraction, BaseSource, &bDynamicResolutionActive);

	return BuildMetalFXResolutionInfo(QualityMode, CVarMetalFXAutoScalingFromEngine.GetValueOnGameThread(), bDynamicResolutionActive, EngineBaseResolutionFraction, ViewFamily.SecondaryViewFraction);
}

void RestoreMetalFXScreenPercentage()
{
	FMetalFXScreenPercentageController::Get().Restore();
}
