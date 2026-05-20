# MetalFX For Unreal Plugin

> English section is automatically translated from the Korean README draft.

## Overview

MetalFX For Unreal Plugin is an engine plugin for using Apple MetalFX in Unreal Engine.

The current version is an **Alpha / Proof of Concept** (PoC) version intended to verify whether MetalFX can be integrated into Unreal Engine’s rendering and upscaling flow.

This plugin is not a production-ready plugin intended for immediate use in commercial projects.  
Its purpose is to verify Unreal Engine and MetalFX SDK integration, required engine modification points, platform-specific limitations, and behavior in packaged builds.

---
## Latest Release

**Alpha Release 0.11**  [![Latest Release](https://img.shields.io/github/v/release/LocketGoma/MetalFX?label=Latest%20Release)](https://github.com/LocketGoma/MetalFX/releases/latest)

[Latest Release Notes](https://github.com/LocketGoma/MetalFX/releases/latest)

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
  - [Console Variable](#console-variable)
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
- Packaged build testing
- Unreal Engine engine plugin structure
- MetalFX enable / disable testing through console variables

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
5. Rebuild the entire engine.
6. Run the project in a Metal RHI environment.
7. Test MetalFX using console variables.

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

## Console Variable

MetalFX testing can be controlled with the following console variable:

| Command | Parameters | Description |
| --- | --- | --- |
| `r.MetalFX.Enabled` | bool (0,1) | Enable / Disable MetalFX. |


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
- Partial engine source modification automation script
- Structure targeting Apple Silicon Mac and MetalFX-supported iOS / iPadOS devices

---

## Work in Progress / Unverified Items

The following items are currently in progress or require additional validation:

- Final validation in iOS runtime environment
- MetalFX Spatial Upscaler startup validation
- MetalFX Quality Option support
- Jitter Offset and other Temporal Upscaling correction values
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
- Additional work is required for production use, including quality options, Jitter Offset, Temporal History, and resolution correction

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
- 패키징 빌드에서 테스트 가능
- Unreal Engine 엔진 플러그인 형태로 구성
- 콘솔 변수를 통한 MetalFX 활성화/비활성화 테스트 가능

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
5. 엔진 전체 재빌드를 수행합니다.
6. Metal RHI 환경에서 프로젝트를 실행합니다.
7. 콘솔 변수를 통해 MetalFX 기능을 테스트합니다.

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

MetalFX 테스트는 다음 콘솔 변수를 통해 제어할 수 있습니다.

| Command | Parameters | Description |
| --- | --- | --- |
| `r.MetalFX.Enabled` | bool (0,1) | Enable / Disable MetalFX. |


---

## 현재 구현 범위

현재 구현 및 검증된 범위는 다음과 같습니다.

- Unreal Engine 엔진 플러그인 구조 구성
- MetalFX SDK 통합
- MetalFX Temporal Upscaler 기동 확인
- 패키징 빌드에서 테스트 가능
- 콘솔 변수 기반 활성화/비활성화
- 일부 엔진 소스 수정 자동화 스크립트 제공
- Apple Silicon Mac 및 MetalFX 지원 iOS/iPadOS 기기 대상 구조 구성

---

## 작업 중 / 미확인 사항

현재 다음 항목은 작업 중이거나 추가 검증이 필요한 상태입니다.

- iOS 런타임 환경에서의 최종 검증
- MetalFX Spatial Upscaler 기동 확인
- MetalFX Quality Option 지원
- Jitter Offset 및 Temporal Upscaling 관련 보정 수치 적용
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
- 실사용을 위해서는 품질 옵션, Jitter Offset, Temporal History, 해상도 보정 등 추가 작업이 필요합니다.

---

## 릴리즈 정보

릴리즈별 자세한 변경 사항은 Releases 페이지를 참고 바랍니다.

README는 프로젝트의 목적, 지원 환경, 설치 흐름, 현재 구현 범위와 제한 사항을 설명하며, 각 Release 별 세부 변경 내역은 각 Release Note에 정리되어 있습니다.

