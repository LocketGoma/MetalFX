# MetalFX For Unreal Plugin

> English section is automatically translated from the Korean README draft.

## Notice

### English

When packaging with **Xcode 26.6 or later**, Metal native shader library packaging may fail because `metal-pack` is no longer available in the Xcode toolchain.

If this occurs, disable shared native material shader libraries in your project's `DefaultGame.ini`:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
bShareMaterialShaderCode=True
bSharedMaterialNativeLibraries=False
```

Changing this setting may cause unknown issues depending on your project configuration.

### 한국어

**Xcode 26.6 이상**에서 패키징할 경우, Xcode toolchain에서 `metal-pack`을 찾을 수 없어 Metal native shader library 패키징이 실패할 수 있습니다.

이 문제가 발생하면 프로젝트의 `DefaultGame.ini`에서 shared native material shader library 설정을 비활성화하세요:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
bShareMaterialShaderCode=True
bSharedMaterialNativeLibraries=False
```

해당 설정 변경 시 개인 프로젝트 설정에 따라 알려지지 않은 문제가 발생할 수 있습니다.

---

## Overview

MetalFX For Unreal Plugin is an engine plugin for using Apple MetalFX in Unreal Engine.

The current version is an **Alpha / Proof of Concept** (PoC) version intended to verify whether MetalFX can be integrated into Unreal Engine’s rendering and upscaling flow.

This plugin is not a production-ready plugin intended for immediate use in commercial projects.  
Its purpose is to verify Unreal Engine and MetalFX SDK integration, required engine modification points, platform-specific limitations, and behavior in packaged builds.

---
## Latest Release

[![Latest Release](https://img.shields.io/github/v/release/LocketGoma/MetalFX?include_prereleases&label=Latest%20Release)](https://github.com/LocketGoma/MetalFX/releases/latest)

[Latest Release Notes](https://github.com/LocketGoma/MetalFX/releases/latest)

---
## Performance Profiling Results

[Performance Profiling Lists](Docs/PerformanceValidation.md)

---
## Table of Contents

- [English](#metalfx-for-unreal-plugin)
  - [Overview](#overview)
  - [Project Purpose](#project-purpose)
  - [Current Status](#current-status)
  - [Supported Engine Version](#supported-engine-version)
  - [Supported Runtime Environment](#supported-runtime-environment)
  - [Installation and Application Overview](#installation-and-application-overview)
  - [MetalCPP Notes](#metalcpp-notes)
  - [Engine Source Modification Script](#engine-source-modification-script)
  - [Console Variables](#console-variables)
    - [Quality Modes](#quality-modes)
  - [Current Implementation Scope](#current-implementation-scope)
  - [Work in Progress / Unverified Items](#work-in-progress--unverified-items)
  - [Limitations](#limitations)
  - [Release Information](#release-information)

- [한국어](#metalfx-for-unreal-plugin-1)
  - [개요](#개요)
  - [프로젝트 목적](#프로젝트-목적)
  - [현재 상태](#현재-상태)
  - [지원 엔진 버전](#지원-엔진-버전)
  - [지원 런타임 환경](#지원-런타임-환경)
  - [설치 및 적용 개요](#설치-및-적용-개요)
  - [MetalCPP 관련 주의 사항](#metalcpp-관련-주의-사항)
  - [엔진 소스 수정 스크립트](#엔진-소스-수정-스크립트)
  - [콘솔 변수](#콘솔-변수)
    - [품질 모드](#품질-모드)
  - [현재 구현 범위](#현재-구현-범위)
  - [작업 중 / 미확인 사항](#작업-중--미확인-사항)
  - [제한 사항](#제한-사항)
  - [릴리즈 정보](#릴리즈-정보)

---

## Project Purpose

The purpose of this project is to verify whether Apple MetalFX can be connected to Unreal Engine’s upscaling flow, and to document the engine / platform / SDK integration issues encountered during that process.

This project focuses on verifying the following areas:

- Unreal Engine plugin structure
- MetalFX SDK integration
- Connection with Unreal Engine rendering / upscaling flow
- Apple platform support conditions
- MetalCPP configuration
- Runtime behavior in packaged builds
- Engine source modification and script-based patch workflow

---

## Current Status

The current Alpha version has verified the following:

- MetalFX SDK integration
- MetalFX Temporal Upscaler startup
- Descriptor-based Temporal Scaler creation / recreation using actual render texture size and format
- Runtime MetalFX availability / enabled-state / active-state debug display
- Screen percentage based input / output size tracking
- Quality mode based screen percentage presets
- Jitter offset and motion vector scale parameter forwarding
- Packaged build testing
- Unreal Engine engine plugin structure
- MetalFX enable / disable and debug display testing through console variables

The current version is not recommended for direct use in live or shipping projects.

Additional validation and code modification are required before using this plugin in a production project.

---

## Supported Engine Version

This plugin targets **Unreal Engine 5.4 or later**.

The current development and validation baseline is **Unreal Engine 5.7.4**.

Unreal Engine versions earlier than 5.4 are not guaranteed to be supported.  
Depending on the engine version, MetalCPP availability, rendering structure, RHI/RDG code paths, and required engine modification points may differ.

---

## Supported Runtime Environment

MetalFX currently targets Apple platforms.

This plugin primarily targets the following environments:

- Apple Silicon Mac
- iPhone / iPad devices that support MetalFX
- A17 or later iPhone / iPad devices

This plugin may not function correctly on devices that do not support MetalFX.

---

## Installation and Application Overview

This plugin is intended to be used as an **Engine Plugin**, not as a Project Plugin.

Therefore, the plugin folder should be placed under:

```text
UnrealEngine/Engine/Plugins
```

Basic installation flow:

1. Place the plugin folder under `UnrealEngine/Engine/Plugins`.
2. Check whether the Unreal Engine version in use includes MetalCPP.
3. If MetalCPP is not available in the engine version or required code path, MetalCPP may need to be added manually.
4. If necessary, run the engine modification script included in the `Source/Thirdparty` folder.
5. Enable "MetalFX Plugin" (in Editor or YourProject.uproject)
6. Rebuild the entire engine.
7. Run the project in a Metal RHI environment.
8. Test MetalFX using console variables.

---

## MetalCPP Notes

This plugin may require MetalCPP configuration to integrate Apple MetalFX and Metal APIs.

In most Unreal Engine 5.4 or later code paths, MetalCPP is already included.

However, depending on the engine version or custom engine configuration, MetalCPP may be missing or unavailable in the required code path.

In that case, Apple MetalCPP may need to be added manually.

Reference:

```text
https://developer.apple.com/metal/cpp/
```

---

## Engine Source Modification Script

This plugin may require some engine source modifications in many cases.

A script is included in the `Source/Thirdparty` folder to apply the required changes.

Example script path:

```text
MetalFX/Source/Thirdparty/EngineEditScript.sh
```

A full engine rebuild is recommended after running the script.

Depending on the engine version or custom engine state, the script may not work as-is in every environment.  
It is recommended to review the changes before applying them.

---

## Console Variables

MetalFX testing can be controlled with the following console variables:

| Command | Parameters | Description |
| --- | --- | --- |
| `r.MetalFX.Enabled` | bool (0,1) | Enable / Disable MetalFX. |
| `r.MetalFX.EnableInEditor` | bool (0,1) | Allow MetalFX activation in Play In Editor (PIE). `r.MetalFX.Enabled` must also be enabled. |
| `r.MetalFX.DebugDisplay` | bool (0,1) | Show / Hide MetalFX on-screen debug status. |
| `r.MetalFX.UpscalerMode` | int | Select the MetalFX upscaler mode. `0:Off`, `1:Spatial (WIP)`, `2:Temporal`. |
| `r.MetalFX.Sharpness` | float | WIP sharpness control. Range: `0.0` to `1.0`. |
| `r.MetalFX.QualityMode` | int | Select the MetalFX quality preset. Range: `0` to `5`. Default: `1` (UltraQuality). |
| `r.MetalFX.AutoScalingFromEngine` | bool (0,1) | Use the engine-selected resolution as the MetalFX output target and apply the quality ratio to its input. Default: `1`. |
| `r.MetalFX.JitterMode` | int | Control temporal jitter forwarding. `1:normal`, `0:disabled`, `-1:inverted`. |
| `r.MetalFX.MotionVectorScaleX` | float | WIP horizontal motion vector scale passed to MetalFX. |
| `r.MetalFX.MotionVectorScaleY` | float | WIP vertical motion vector scale passed to MetalFX. |

### Quality Modes

The following table describes the default `r.MetalFX.AutoScalingFromEngine=1` behavior. **Engine Output Target** is the existing Secondary View Fraction composed with the Primary resolution fraction selected by Unreal Engine's Screen Percentage or Dynamic Resolution path. Except for NativeAA, each input ratio is relative to this output target rather than directly to the physical display.

| Mode | Input Ratio | Output Ratio | Console Command | Description |
| --- | --- | --- | --- | --- |
| NativeAA | 100% of native display | 100% of native display | `r.MetalFX.QualityMode 0` | Forces native-display-sized input and output. It bypasses Engine Base and Dynamic Resolution scaling. |
| UltraQuality (Default) | 100% of output target<br>Effective: Engine Output Target × 100% | Engine Output Target | `r.MetalFX.QualityMode 1` | Uses the full engine-selected output target. External monitors, DPI scaling, automatic Screen Percentage, or Dynamic Resolution can make this different from NativeAA. |
| Quality | 66.7% of output target<br>Effective: Engine Output Target × 66.7% | Engine Output Target | `r.MetalFX.QualityMode 2` | Approximately 1.5x upscaling per dimension. |
| Balanced | 50% of output target<br>Effective: Engine Output Target × 50% | Engine Output Target | `r.MetalFX.QualityMode 3` | 2.0x upscaling per dimension. |
| Performance | 42% of output target<br>Effective: Engine Output Target × 42% | Engine Output Target | `r.MetalFX.QualityMode 4` | Approximately 2.4x upscaling per dimension. |
| UltraPerformance | 34% of output target<br>Effective: Engine Output Target × 34% | Engine Output Target | `r.MetalFX.QualityMode 5` | Approximately 2.94x upscaling per dimension, retaining margin below MetalFX's 3.0x limit. |

When `r.MetalFX.AutoScalingFromEngine=0`, modes 1 through 5 use their listed percentages as absolute Primary Screen Percentage values and retain the existing Secondary output target.

`METALFX_DEBUG` controls Screen Percentage ownership (`MetalFX.Build.cs` defaults to `0`). With `METALFX_DEBUG=0`, MetalFX writes its selected Primary ratio to `r.ScreenPercentage` while active and restores the activation-time value and SetBy priority when disabled. With `METALFX_DEBUG=1`, MetalFX does not overwrite or restore `r.ScreenPercentage`; a positive runtime change is re-read as the engine output target on the next view family, and values above 100% are allowed for supersampling tests. In either mode, `0` continues to use Unreal Engine's automatic Screen Percentage path and Dynamic Resolution takes precedence while active.


---

## Current Implementation Scope

This README does not repeat every release-specific change.  
It only summarizes the current implementation scope of the plugin.

Currently implemented and verified scope:

- Unreal Engine engine plugin structure
- MetalFX SDK integration
- MetalFX Temporal Upscaler startup
- Packaged build testing
- Console variable based enable / disable control
- Console variable based on-screen debug display control
- MetalFX availability / enabled-state / active-state display through the view extension
- Debug display for input rect, output rect, expected input size, screen percentage, and actual screen percentage
- Quality mode presets connected to screen percentage values
- Jitter offset forwarding to MetalFX TemporalScaler
- Motion vector scale forwarding to MetalFX TemporalScaler
- Temporal Scaler lazy creation / recreation based on texture format, input texture size, input content size, and output size
- Runtime guard for MetalFX TemporalScaler's 3x maximum upscaling limitation
- Play In Editor (PIE) activation policy using `r.MetalFX.EnableInEditor`
- Partial engine source modification automation script
- Structure targeting Apple Silicon Mac and MetalFX-supported iOS / iPadOS devices

---

## Work in Progress / Unverified Items

The following items are currently in progress or require additional validation:

- Final validation in iOS runtime environment
- MetalFX Spatial Upscaler startup validation
- Final validation for MetalFX Quality Mode behavior across projects
- Motion vector / velocity texture conversion and visual quality validation
- Temporal History handling for production-level quality
- Validation across various resolutions / aspect ratios / graphics option combinations
- Production-level visual quality stabilization
- Compatibility validation across Unreal Engine versions

---

## Limitations

This plugin is currently in an **Alpha / Proof of Concept** (PoC) Stage.

Current limitations:

- Not recommended for direct use in live or shipping projects
- Additional modifications may be required depending on project rendering settings
- Build issues may occur depending on engine version and MetalCPP configuration
- Correct behavior is not guaranteed on Apple devices that do not support MetalFX
- Some engine source modifications may be required
- Behavior may differ between packaged builds and debug environments
- MetalFX TemporalScaler does not support upscaling greater than 3x per dimension
- UltraPerformance uses a 34% input ratio (approximately 2.94x) to remain below the MetalFX 3x upscaling limit
- Additional work is required for production use, including motion vector conversion, Temporal History, and broader resolution / aspect-ratio validation

---

## Release Information

Please refer to the Releases page for detailed changes for each release.

This README explains the project purpose, supported environment, installation flow, current implementation scope, and limitations.  
Detailed change logs are documented in each Release Note.

---

<br>

# MetalFX For Unreal Plugin

## 개요

MetalFX For Unreal Plugin은 Apple MetalFX를 Unreal Engine에서 사용하기 위한 엔진 플러그인입니다.

현재 버전은 MetalFX를 Unreal Engine의 렌더링/업스케일링 흐름에 통합할 수 있는지 검증하기 위한 **Alpha / Proof of Concept** (PoC) 버전입니다.

이 플러그인은 상용 프로젝트에 바로 투입하기 위한 완성 버전이 아니며, Unreal Engine과 MetalFX SDK 연동 가능성, 엔진 수정 지점, 플랫폼별 제약, 패키징 빌드에서의 동작 가능성을 검증하기 위한 프로젝트입니다.

---

## 프로젝트 목적

이 프로젝트의 목적은 Apple MetalFX를 Unreal Engine의 업스케일링 흐름에 연결할 수 있는지 검증하고, 그 과정에서 발생하는 엔진/플랫폼/SDK 연동 이슈를 정리하는 것입니다.

특히 다음 항목을 확인하는 데 초점을 둡니다.

- Unreal Engine 엔진 플러그인 구성
- MetalFX SDK 연동
- Unreal Engine 렌더링/업스케일러 흐름과의 연결
- Apple 플랫폼 지원 조건
- MetalCPP 구성
- 패키징 빌드에서의 동작 가능성
- 엔진 소스 수정 및 자동화 스크립트 기반 적용 방식

---

## 현재 상태

현재 Alpha 버전에서 확인된 사항은 다음과 같습니다.

- MetalFX SDK 통합
- MetalFX Temporal Upscaler 기동 확인
- 실제 렌더 텍스처 크기와 포맷 기반 Temporal Scaler 생성 / 재생성 처리
- 런타임 MetalFX 사용 가능 여부 / 사용 설정 / 실제 활성 상태 디버그 표시
- ScreenPercentage 기반 입력 / 출력 크기 추적
- Quality Mode 기반 screen percentage 프리셋
- Jitter Offset 및 Motion Vector Scale 파라미터 전달
- 패키징 빌드에서 테스트 가능
- Unreal Engine 엔진 플러그인 형태로 구성
- 콘솔 변수를 통한 MetalFX 활성화/비활성화 및 디버그 표시 테스트 가능

현재 버전만으로는 라이브 또는 출시용 프로젝트에 바로 사용하는 것을 권장하지 않습니다.

출시 프로젝트에 활용하려면 프로젝트 환경에 맞는 추가 검증과 코드 수정이 필요합니다.

---

## 지원 엔진 버전

이 플러그인은 **Unreal Engine 5.4 이상**을 대상으로 합니다.

현재 개발 및 검증 기준은 **Unreal Engine 5.7.4**입니다.

Unreal Engine 5.4 이전 버전은 지원을 보장하지 않습니다.  
엔진 버전에 따라 MetalCPP 포함 여부, 렌더링 구조, RHI/RDG 코드 경로, 필요한 엔진 수정 지점이 달라질 수 있습니다.

---

## 지원 런타임 환경

현재 MetalFX는 Apple 플랫폼을 대상으로 합니다.

현재 이 플러그인은 다음 환경을 주요 대상으로 합니다.

- Apple Silicon Mac
- MetalFX를 지원하는 iPhone / iPad
- A17 이상 iPhone / iPad 계열 기기

MetalFX를 지원하지 않는 기기에서는 이 플러그인이 정상적으로 동작하지 않을 수 있습니다.

---

## 설치 및 적용 개요

이 플러그인은 Project Plugin이 아니라 **Engine Plugin**으로 사용하는 것을 전제로 합니다.

따라서 플러그인 폴더를 다음 경로에 배치해야 합니다.

```text
UnrealEngine/Engine/Plugins
```

설치 흐름은 다음과 같습니다.

1. 플러그인 폴더를 `UnrealEngine/Engine/Plugins` 경로에 배치합니다.
2. 사용 중인 Unreal Engine에 MetalCPP가 포함되어 있는지 확인합니다.
3. MetalCPP가 없는 엔진 버전 또는 코드 경로에서는 MetalCPP를 추가해야 할 수 있습니다.
4. 필요한 경우 `Source/Thirdparty` 폴더의 엔진 수정 스크립트를 실행합니다.
5. MetalFX 플러그인을 활성화합니다. (에디터 환경 혹은 YourProject.uproject 파일)
6. 엔진 전체 재빌드를 수행합니다.
7. Metal RHI 환경에서 프로젝트를 실행합니다.
8. 콘솔 변수를 통해 MetalFX 기능을 테스트합니다.

---

## MetalCPP 관련 주의 사항

이 플러그인은 Apple MetalFX 및 Metal API 연동을 위해 MetalCPP 구성을 필요로 할 수 있습니다.

Unreal Engine 5.4 이후 버전에서는 대부분의 엔진 코드 경로에 MetalCPP가 포함되어 있습니다.

하지만 사용 중인 엔진 버전이나 커스텀 엔진 구성에 따라 MetalCPP가 없거나, 필요한 코드 경로에서 바로 사용할 수 없는 경우가 있을 수 있습니다.

이 경우 Apple MetalCPP를 별도로 추가해야 할 수 있습니다.

참고:

```text
https://developer.apple.com/metal/cpp/
```

---

## 엔진 소스 수정 스크립트

이 플러그인은 많은 경우 일부 엔진 소스 수정이 필요할 수 있습니다.

필요한 수정 사항을 적용하기 위해 `Source/Thirdparty` 폴더에 포함된 스크립트를 사용할 수 있습니다.

실행 대상 예시:

```text
MetalFX/Source/Thirdparty/EngineEditScript.sh
```

스크립트 실행 후에는 엔진 전체 재빌드를 권장합니다.

엔진 버전 또는 커스텀 엔진 상태에 따라 스크립트가 모든 환경에서 그대로 동작하지 않을 수 있으므로, 적용 전 변경 내용을 확인하는 것을 권장합니다.

---

## 콘솔 변수

MetalFX 테스트는 다음 콘솔 변수들을 통해 제어할 수 있습니다.

| Command | Parameters | Description |
| --- | --- | --- |
| `r.MetalFX.Enabled` | bool (0,1) | Enable / Disable MetalFX. |
| `r.MetalFX.EnableInEditor` | bool (0,1) | PIE 환경에서 MetalFX 활성화를 허용합니다. `r.MetalFX.Enabled`도 함께 켜야 합니다. |
| `r.MetalFX.DebugDisplay` | bool (0,1) | MetalFX 화면 디버그 상태 표시를 켜거나 끕니다. |
| `r.MetalFX.UpscalerMode` | int | MetalFX 업스케일러 모드를 선택합니다. `0:Off`, `1:Spatial (WIP)`, `2:Temporal`. |
| `r.MetalFX.Sharpness` | float | WIP sharpness 제어 값입니다. 범위: `0.0` ~ `1.0`. |
| `r.MetalFX.QualityMode` | int | MetalFX 품질 프리셋을 선택합니다. 범위: `0` ~ `5`. 기본값: `1` (UltraQuality). |
| `r.MetalFX.AutoScalingFromEngine` | bool (0,1) | 엔진이 선택한 해상도를 MetalFX 출력 목표로 사용하고 품질 비율을 입력에 적용합니다. 기본값: `1`. |
| `r.MetalFX.JitterMode` | int | Temporal jitter 전달 방식을 제어합니다. `1:normal`, `0:disabled`, `-1:inverted`. |
| `r.MetalFX.MotionVectorScaleX` | float | WIP horizontal motion vector scale 값입니다. |
| `r.MetalFX.MotionVectorScaleY` | float | WIP vertical motion vector scale 값입니다. |

### 품질 모드

다음 표는 기본 설정인 `r.MetalFX.AutoScalingFromEngine=1`을 기준으로 합니다. **Engine Output Target**은 기존 Secondary View Fraction과 Unreal Engine의 Screen Percentage 또는 Dynamic Resolution 경로가 선택한 Primary 비율을 합성한 출력 목표입니다. NativeAA를 제외한 입력 비율은 실제 디스플레이가 아니라 이 출력 목표를 기준으로 합니다.

| 모드명 | 인풋 비율 | 아웃풋 비율 | 콘솔 명령어 | 설명 |
| --- | --- | --- | --- | --- |
| NativeAA | 실제 디스플레이의 100% | 실제 디스플레이의 100% | `r.MetalFX.QualityMode 0` | Engine Base와 Dynamic Resolution을 우회하고 네이티브 디스플레이 크기의 입력과 출력을 강제합니다. |
| UltraQuality (기본값) | 출력 목표의 100%<br>실효값: Engine Output Target × 100% | Engine Output Target | `r.MetalFX.QualityMode 1` | 엔진이 선택한 출력 목표 전체를 사용합니다. 외부 모니터, DPI, Auto Screen Percentage 또는 Dynamic Resolution에 따라 NativeAA와 다른 값이 나올 수 있습니다. |
| Quality | 출력 목표의 66.7%<br>실효값: Engine Output Target × 66.7% | Engine Output Target | `r.MetalFX.QualityMode 2` | 축당 약 1.5배 업스케일링합니다. |
| Balanced | 출력 목표의 50%<br>실효값: Engine Output Target × 50% | Engine Output Target | `r.MetalFX.QualityMode 3` | 축당 2.0배 업스케일링합니다. |
| Performance | 출력 목표의 42%<br>실효값: Engine Output Target × 42% | Engine Output Target | `r.MetalFX.QualityMode 4` | 축당 약 2.4배 업스케일링합니다. |
| UltraPerformance | 출력 목표의 34%<br>실효값: Engine Output Target × 34% | Engine Output Target | `r.MetalFX.QualityMode 5` | 축당 약 2.94배 업스케일링하며 MetalFX의 3.0배 제한 아래에 여유를 둡니다. |

`r.MetalFX.AutoScalingFromEngine=0`이면 1~5번 모드는 표의 비율을 절대 Primary Screen Percentage로 사용하고 기존 Secondary 출력 목표를 유지합니다.

`METALFX_DEBUG`가 Screen Percentage의 소유 방식을 결정하며 `MetalFX.Build.cs`의 기본값은 `0`입니다. `METALFX_DEBUG=0`에서는 MetalFX 활성 중 선택된 Primary 비율을 `r.ScreenPercentage`에 적용하고, 비활성화할 때 활성화 시점의 값과 SetBy 우선순위를 복원합니다. `METALFX_DEBUG=1`에서는 `r.ScreenPercentage`를 덮어쓰거나 복원하지 않으며, 런타임에 입력한 양수 값을 다음 ViewFamily의 엔진 출력 목표로 다시 읽고 100% 초과 값도 슈퍼샘플링 테스트용으로 허용합니다. 두 모드 모두 `0`은 Unreal Engine의 자동 Screen Percentage 경로를 사용하며 Dynamic Resolution 활성 중에는 해당 값이 우선합니다.


---

## 현재 구현 범위

현재 구현 및 검증된 범위는 다음과 같습니다.

- Unreal Engine 엔진 플러그인 구조 구성
- MetalFX SDK 통합
- MetalFX Temporal Upscaler 기동 확인
- 패키징 빌드에서 테스트 가능
- 콘솔 변수 기반 활성화/비활성화
- 콘솔 변수 기반 화면 디버그 표시 제어
- View Extension을 통한 MetalFX 사용 가능 여부 / 사용 설정 / 실제 활성 상태 표시
- InputRect, OutputRect, ExpectedInput size, ScreenPercentage, ActualSP 디버그 표시
- Quality Mode 프리셋과 screen percentage 값 연동
- MetalFX TemporalScaler에 Jitter Offset 전달
- MetalFX TemporalScaler에 Motion Vector Scale 전달
- 텍스처 포맷, 입력 텍스처 크기, 입력 콘텐츠 크기, 출력 크기 기반 Temporal Scaler 지연 생성 / 재생성
- MetalFX TemporalScaler의 3x 최대 업스케일 제한에 대한 런타임 방어 처리
- `r.MetalFX.EnableInEditor` 기반 PIE 활성화 정책
- 일부 엔진 소스 수정 자동화 스크립트 제공
- Apple Silicon Mac 및 MetalFX 지원 iOS/iPadOS 기기 대상 구조 구성

---

## 작업 중 / 미확인 사항

현재 다음 항목은 작업 중이거나 추가 검증이 필요한 상태입니다.

- iOS 런타임 환경에서의 최종 검증
- MetalFX Spatial Upscaler 기동 확인
- 프로젝트별 MetalFX Quality Mode 동작 최종 검증
- Motion Vector / Velocity Texture 변환 및 시각 품질 검증
- 실사용 품질을 위한 Temporal History 처리
- 다양한 해상도 / 화면 비율 / 그래픽 옵션 조합 검증
- 실사용 수준의 품질 안정화
- 엔진 버전별 호환성 검증

---

## 제한 사항

이 플러그인은 현재 **Alpha / Proof of Concept** (PoC) 단계입니다.

현재 제한 사항은 다음과 같습니다.

- 라이브 또는 출시용 프로젝트에 바로 사용하는 것을 권장하지 않습니다.
- 프로젝트별 렌더링 설정에 따라 추가 수정이 필요할 수 있습니다.
- 엔진 버전과 MetalCPP 구성에 따라 빌드 문제가 발생할 수 있습니다.
- MetalFX를 지원하지 않는 Apple 기기에서는 정상 동작을 보장하지 않습니다.
- 일부 엔진 소스 수정이 필요할 수 있습니다.
- 패키징 빌드와 디버그 환경에서 동작 차이가 있을 수 있습니다.
- MetalFX TemporalScaler는 각 축 기준 3x를 초과하는 업스케일을 지원하지 않습니다.
- UltraPerformance는 MetalFX 3x 업스케일 제한 아래를 유지하기 위해 34% 입력 비율(약 2.94배)을 사용합니다.
- 실사용을 위해서는 Motion Vector 변환, Temporal History, 더 넓은 해상도 / 화면 비율 검증 등 추가 작업이 필요합니다.

---

## 릴리즈 정보

릴리즈별 자세한 변경 사항은 Releases 페이지를 참고 바랍니다.

README는 프로젝트의 목적, 지원 환경, 설치 흐름, 현재 구현 범위와 제한 사항을 설명하며, 각 Release 별 세부 변경 내역은 각 Release Note에 정리되어 있습니다.
