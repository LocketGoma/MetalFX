# Performance Validation

## Main Benchmark — Unreal Engine Lyra Sample

> [!NOTE]
> This dataset was captured on an **Apple M5 Pro** using the same Lyra scene and action sequence.
> Metal HUD values were sampled from the aligned recordings. The comparison is action-aligned rather than frame-exact, and timing differences within 0.2 seconds are treated as equivalent.

### Full-Sequence Average — 260726

Each recording produced 35 valid Metal HUD samples at 0.25-second intervals. No OCR samples were rejected.

| Engine Base | Upscaler Mode | Input → Output Resolution | FPS Avg | FPS Range | GPU Avg | GPU Range | Frame Interval Avg |
|:-----------:|:-------------:|:-------------------------:|--------:|:---------:|--------:|:---------:|-------------------:|
| **100.00%** | **Off** | 1512 × 949 → 1512 × 949 | **45.010** | 43.77–47.92 | **21.887 ms** | 20.45–22.51 ms | **22.228 ms** |
| **100.00%** | **Temporal NativeAA** | 1512 × 949 → 1512 × 949 | **45.055** | 43.90–46.15 | **21.537 ms** | 21.00–22.08 ms | **22.200 ms** |
| **83.52%** | **Off** | ≈1263 × 794* → 1512 × 949 | **55.540** | 54.34–58.78 | **17.644 ms** | 16.65–18.00 ms | **18.009 ms** |
| **83.52%** | **Temporal UltraQuality** | 1263 × 794 → 1512 × 950 | **62.248** | 60.50–65.16 | **15.684 ms** | 14.92–16.15 ms | **16.068 ms** |

\* MetalFX was disabled and did not report an active input rectangle. The input is derived from the configured 83.52% engine base and the matching active Temporal capture.

### Full-Sequence Paired Comparison

| Engine Base | Comparison | FPS Difference | GPU Difference | Frame Interval Difference |
|:-----------:|:-----------|---------------:|---------------:|--------------------------:|
| **100.00%** | Temporal NativeAA vs. Off | **+0.045 (+0.10%)** | **-0.350 ms (-1.60%)** | **-0.028 ms (-0.13%)** |
| **83.52%** | Temporal UltraQuality vs. Off | **+6.708 (+12.08%)** | **-1.960 ms (-11.11%)** | **-1.941 ms (-10.78%)** |

The 100% NativeAA result is effectively performance-neutral, with a small reduction in reported GPU time. At 83.52%, Temporal UltraQuality increased average FPS by 12.08% while reducing average GPU time by 11.11%.

An earlier 83.52% Temporal recording was excluded because screen capture was suspected to be running twice. The replacement capture is used above; its full-sequence average and grenade-scene result are consistent with each other.

Metal HUD values are rolling on-screen counters rather than an exported GPU trace. Repeated fixed-path captures or an offline profiler trace are required before treating differences near 1% as statistically stable.

### Specific Scene — Grenade Throw

This comparison uses one action-matched frame from the grenade-throw sequence.

| Engine Base | Upscaler Mode | FPS | GPU | Frame Interval |
|:-----------:|:-------------:|----:|----:|---------------:|
| **100.00%** | **Off** | **44.44** | **22.24 ms** | **22.50 ms** |
| **100.00%** | **Temporal NativeAA** | **44.58** | **21.85 ms** | **22.43 ms** |
| **83.52%** | **Off** | **55.17** | **17.81 ms** | **18.12 ms** |
| **83.52%** | **Temporal UltraQuality** | **62.34** | **15.73 ms** | **16.04 ms** |

### Grenade-Throw Paired Comparison

| Engine Base | Comparison | FPS Difference | GPU Difference | Frame Interval Difference |
|:-----------:|:-----------|---------------:|---------------:|--------------------------:|
| **100.00%** | Temporal NativeAA vs. Off | **+0.14 (+0.32%)** | **-0.39 ms (-1.75%)** | **-0.07 ms (-0.31%)** |
| **83.52%** | Temporal UltraQuality vs. Off | **+7.17 (+13.00%)** | **-2.08 ms (-11.68%)** | **-2.08 ms (-11.48%)** |

The specific frame agrees with the full-sequence result at both engine-base settings. A single frame is retained as scene evidence, while the 35-sample full-sequence average remains the primary performance conclusion.

### Grenade-Throw Evidence

Select an image to view the resized 2560 × 1662 capture.

Replay playback may display some scene colors differently due to a replay-system bug. These color differences are not treated as upscaler quality differences.

#### 100% Engine Base

| MetalFX Off | MetalFX Temporal NativeAA |
|:---:|:---:|
| [![Lyra motion sample with MetalFX disabled at 100% engine base](ProfilingResults/260726_motion_100percent_off_grenade.jpg)](ProfilingResults/260726_motion_100percent_off_grenade.jpg) | [![Lyra motion sample with MetalFX Temporal NativeAA at 100% engine base](ProfilingResults/260726_motion_100percent_nativeaa_grenade.jpg)](ProfilingResults/260726_motion_100percent_nativeaa_grenade.jpg) |

#### 83.52% Engine Base

| MetalFX Off | MetalFX Temporal UltraQuality |
|:---:|:---:|
| [![Lyra motion sample with MetalFX disabled at 83.52% engine base](ProfilingResults/260726_motion_83percent_off_grenade.jpg)](ProfilingResults/260726_motion_83percent_off_grenade.jpg) | [![Lyra motion sample with MetalFX Temporal UltraQuality at 83.52% engine base](ProfilingResults/260726_motion_83percent_temporal_grenade.jpg)](ProfilingResults/260726_motion_83percent_temporal_grenade.jpg) |

### Video Quality Assessment

| Inspection Area | 100% Off vs. Temporal NativeAA | 83.52% Off vs. Temporal UltraQuality |
|:----------------|:--------------------------------|:-------------------------------------|
| Static and slow geometry | Wall grids, stairs, and portal outlines remain comparable. No clear NativeAA detail loss is visible. | Temporal reconstruction retains the main grid and portal structure without visible distortion. |
| Character silhouette | No persistent double contour or trail is visible behind the moving character. | No sustained character ghost trail is visible. Fast limb motion is blurred in both captures. |
| High-contrast and emissive edges | Portal and pad edges remain continuous. No repeated edge breakup is visible. | Emissive portal edges remain continuous through movement, with no obvious reconstruction holes. |
| Camera pan and rotation | Broad motion blur is present with MetalFX both disabled and active. No additional NativeAA-only failure is isolated. | Some rapidly moving frames appear soft, but comparable blur also occurs with MetalFX disabled and the captures are not frame-locked. |
| Disocclusion | Newly revealed wall and doorway regions do not leave a persistent history residue. | No persistent residue is visible around the portal, columns, or character silhouette. |
| Temporal stability | No black frame, flash, history collapse, or obvious frame-to-frame jitter is visible in the sampled sequence. | No black frame, flash, severe ghosting, or obvious history instability is visible in the sampled sequence. |
| Fine AA shimmer | The recording is sufficient to reject gross instability, but not to measure subpixel shimmer conclusively. | The recording is sufficient to reject gross instability, but not to prove an AA advantage over a frame-locked reference. |

The macOS screen-recording control bar obscures part of the lower HUD in every capture. It does not cover the main character and scene edges used above, but it limits analysis of the lower-screen UI.

#### Quality Conclusion

- **Temporal NativeAA:** No visible quality regression or temporal-history failure was identified against 100% Off. Performance is effectively neutral in this capture.
- **Temporal UltraQuality:** The output remains structurally stable and shows no severe ghosting or distortion. The replacement capture demonstrates a clear Lyra performance advantage, although it does not prove a consistent moving-image sharpness advantage.
- **Motion softness:** Strong blur exists in MetalFX Off frames as well. The current recordings cannot separate Unreal Engine motion blur from additional upscaler softness with confidence.
- **Next quality validation:** A frame-locked camera path with Unreal Engine motion blur disabled is required for a decisive AA shimmer, thin-edge stability, and moving-detail comparison.

## Stress Test — Unreal Engine CassiniSample

> [!IMPORTANT]
> This is an additional stress-test dataset captured in Unreal Engine's **CassiniSample** on an **Apple M5 Pro**.
> `AI Logging` and `GC Verify` were enabled during profiling, so these results represent relative behavior under the captured workload and are not directly comparable with the Lyra benchmark above.

### Captured Results — 260725

| Engine Base | Upscaler Mode | Input Resolution | Output Resolution | FPS | GPU | Queue Total Avg | Queue Total Range | MetalFX |
|:-----------:|:-------------:|:----------------:|:-----------------:|----:|----:|----------------:|:-----------------:|:-------:|
| **100.00%** | **Off** | 1512 × 950 | 1512 × 950 | **8.72** | **114.20 ms** | **113.72 ms** | 113.21–114.58 ms | ❌ Disabled |
| **100.00%** | **Temporal NativeAA** | 1512 × 950 | 1512 × 950 | **8.92** | **111.65 ms** | **110.67 ms** | 110.10–111.50 ms | ✅ Active |
| **83.52%** | **Off** | ≈1263 × 794* | 1512 × 950 | **9.05** | **108.35 ms** | **108.02 ms** | 107.45–111.51 ms | ❌ Disabled |
| **83.52%** | **Temporal UltraQuality** | 1263 × 794 | 1512 × 950 | **9.28** | **107.40 ms** | **106.65 ms** | 106.00–107.57 ms | ✅ Active |

\* MetalFX was disabled and did not report an active input rectangle. The value is derived from the configured 83.52% engine base and the matching active UltraQuality capture.

The Apple Metal HUD reported a 1512 × 949 display while the renderer and MetalFX reported a 1512 × 950 render target. The one-pixel difference is retained in the source captures; render-target dimensions are used in the table.

### Paired Capture Comparison

| Engine Base | Comparison | FPS Difference | GPU Difference | Queue Total Avg Difference |
|:-----------:|:-----------|---------------:|---------------:|---------------------------:|
| **100.00%** | Temporal NativeAA vs. Off | **+0.20 (+2.29%)** | **-2.55 ms (-2.23%)** | **-3.05 ms (-2.68%)** |
| **83.52%** | Temporal UltraQuality vs. Off | **+0.23 (+2.54%)** | **-0.95 ms (-0.88%)** | **-1.37 ms (-1.27%)** |

These differences describe the four captured samples only. Repeated timed runs are required before treating them as statistically stable performance gains.

### Stress-Test Profiling Evidence

Select an image to view the resized profiling capture.

#### 100% Engine Base

| MetalFX Off | MetalFX Temporal NativeAA |
|:---:|:---:|
| [![CassiniSample stress test with MetalFX disabled at 100% engine base](ProfilingResults/260725_cassini_100percent_off.jpg)](ProfilingResults/260725_cassini_100percent_off.jpg) | [![CassiniSample stress test with MetalFX Temporal NativeAA at 100% engine base](ProfilingResults/260725_cassini_100percent_nativeaa.jpg)](ProfilingResults/260725_cassini_100percent_nativeaa.jpg) |

#### 83.52% Engine Base

| MetalFX Off | MetalFX Temporal UltraQuality |
|:---:|:---:|
| [![CassiniSample stress test with MetalFX disabled at 83.52% engine base](ProfilingResults/260725_cassini_83percent_off.jpg)](ProfilingResults/260725_cassini_83percent_off.jpg) | [![CassiniSample stress test with MetalFX Temporal UltraQuality at 83.52% engine base](ProfilingResults/260725_cassini_83percent_temporal.jpg)](ProfilingResults/260725_cassini_83percent_temporal.jpg) |
