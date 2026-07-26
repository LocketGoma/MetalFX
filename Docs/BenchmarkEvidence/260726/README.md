# MetalFX 50% Balanced Benchmark Evidence — 260726

This package retains the compact, shareable evidence needed to explain the
50% MetalFX Temporal Balanced comparison without keeping the complete screen
recordings in the repository.

## Retained Assets

| File | Description |
|:-----|:------------|
| `50percent_temporal_balanced_aligned_t7.png` | Full-resolution frame extracted from the 50% Temporal Balanced recording |
| `50percent_off_aligned_t7.png` | Full-resolution frame extracted from the final 50% MetalFX Off recording |
| `100percent_off_reference_aligned_t7.png` | Full-resolution frame extracted from the 100% MetalFX Off reference recording |
| `50percent_aligned_keyframe_comparison.jpg` | Three-column, full-size aligned comparison |
| `50percent_aligned_sequence_comparison_large.jpg` | Five-timepoint overview at 0, 2, 4, 6, and 8 seconds |

Each retained PNG frame is 3024 × 1964. The three-column keyframe comparison
uses the source frames without spatial downscaling and is saved as a
high-quality JPEG.

## Source Mapping

| Mode | Temporary source | Size | SHA-256 |
|:-----|:-----------------|-----:|:--------|
| 50% Temporal Balanced | `video_70231.mov` | 169,715,856 bytes | `6d87d2b89fa8f01eb433ecbeae83074bdd88aa133170c81f5ff4f9716a035eb6` |
| 50% MetalFX Off | `video_70531.mov` | 146,390,602 bytes | `74a4d159eae7df70e8f778fba1567b13678b458620bde7c38a2da01da9b15e45` |
| 100% MetalFX Off reference | `reference_100off.mov` | 75,718,153 bytes | `5795b538b415bda245940c49610d4baad9fc430a31eb968891c04dc3f6509bd6` |

The original screen recordings remain temporary working data and are not part
of this evidence package.

## Alignment

Edge-pattern matching over frames sampled every 0.2 seconds produced these
offsets relative to the 100% Off reference:

- 50% Temporal Balanced: +11.2 seconds
- 50% MetalFX Off: +8.2 seconds

The retained keyframe corresponds to reference time 7.0 seconds:

| Mode | Requested source time | Actual decoded time |
|:-----|----------------------:|--------------------:|
| 50% Temporal Balanced | 18.200 s | 18.192 s |
| 50% MetalFX Off | 15.200 s | 15.200 s |
| 100% MetalFX Off reference | 7.000 s | 6.983 s |

## Interpretation Limits

- The replay system can change scene colors between runs. Color differences
  are not treated as upscaler-quality differences.
- The recordings contain benchmark HUD and macOS screen-capture controls.
- The aligned runs are close but are not deterministic pixel-identical
  renders.
- These assets support qualitative inspection of edge reconstruction,
  silhouette stability, and visible large-scale artifacts. They are not valid
  ground truth for PSNR or SSIM measurement.

