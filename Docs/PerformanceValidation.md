# Performance Validation

> [!NOTE]
> ### Performance Validation (Apple M5 Pro)
>
> | Screen Percentage | Upscaler Mode | Input Resolution | Output Resolution | CPU (FPS) | GPU (Frame Time) | MetalFX |
> |:-----------------:|:-------------:|:----------------:|:-----------------:|----------:|-----------------:|:--------:|
> | **100%** | **Off** | 1512 × 950 | 1512 × 950 | **65.60 FPS** | **15.26 ms** | ❌ Disabled |
> | **100%** | **Temporal** | 1512 × 950 | 1512 × 950 | **80.00 FPS** | **12.43 ms** | ✅ Active |
> | **50%** | **Off** | 756 × 475 | 1512 × 950* | **74.61 FPS** | **13.43 ms** | ❌ Disabled |
> | **50%** | **Temporal** | 756 × 475 | 1512 × 950 | **80.90 FPS** | **12.36 ms** | ✅ Active |
>
> \* Unreal Engine `ScreenPercentage` reduces the internal rendering resolution while preserving the display resolution.
>
> **Validation Summary**
>
> - Verified correct switching between **Native Rendering** and **MetalFX Temporal Upscaling**.
> - Confirmed `ScreenPercentage` correctly affects the internal rendering resolution.
> - Verified that the **Plugin Debug Overlay** and **Apple Metal Debug HUD** report matching input/output resolutions.
> - Confirmed stable Temporal Upscaling at both **100%** and **50%** internal rendering resolutions.
