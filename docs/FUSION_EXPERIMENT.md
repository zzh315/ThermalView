# Experiment: visible-camera detail on the thermal image (not started; owner decision)

The owner suggested it (2026-09-25) as an experiment for spare time: use the tablet's rear camera to add detail to the thermal image, MSX-style (FLIR's "Multi-Spectral Dynamic Imaging": the visible image's edges drawn into the thermal one). CLAUDE.md lists "tablet-camera overlay" as out of scope for v1. So this stays a note until the owner decides whether to try it, and whether it would ever ship.

## Facts (verified 2026-09-25, `adb shell dumpsys media.camera`, read-only)

| Camera | Focal length | Sensor | Output | Field of view | Focus |
|---|---|---|---|---|---|
| Rear main | 4.25 mm | 5.22 × 3.92 mm | 4080 × 3060 | 63.1° × 49.5° | Autofocus, from 10 cm (10 dioptres) |
| Rear depth | 2.02 mm | 2.80 × 2.10 mm | 1600 × 1200 | 69.4° × 55.0° | Fixed |
| Thermal (S0H) | 4.0 mm | 3.07 × 2.30 mm (256 × 192 × 12 µm) | 256 × 192 | 42.0° × 32.1° | Manual ring |

- The main camera's view covers the thermal view with margin. Across the thermal field it has ~2550 pixels to the thermal camera's 256, so there is enough visible detail for an edge overlay at the Full preset's 2133 px.
- Neither camera's `lens.poseTranslation` is filled in (all zeros), so the geometry between them has to be measured.

## The hard part: parallax

The two cameras see the scene from different places. The thermal camera sits in its housing on the USB-C port; the rear camera is in a corner of the back. If they are ~20 cm apart, a point's position differs between the two images by atan(baseline / distance):

| Distance | Baseline 20 cm | Baseline 2 cm (thermal camera clipped next to the rear lens) |
|---|---|---|
| 0.3 m (PCB work) | 34° (the views barely overlap) | 3.8° (≈ 23 thermal pixels) |
| 1 m | 11° | 1.1° (≈ 7 px) |
| 3 m (underfloor, room) | 3.8° | 0.4° (≈ 2 px) |
| 10 m (outdoor night) | 1.1° | 0.1° |

- **Where the camera goes:** with the thermal camera where it is now, only far scenes can line up. For PCB work, the thermal camera would need to sit next to the rear lens on a short USB-C extension and a clip.
- **Distance matters:** even then the alignment depends on distance. The overlay needs either a distance setting (a slider, or the rear camera's autofocus distance) or automatic registration each frame, by correlating the thermal image's edges with the visible ones.

## Sketch, if the owner wants to try it

1. **Calibration (needs the owner, the camera and a target):**
   - a checkerboard that shows in both bands (printed on paper, with a warm lamp behind it, or cut from foil on a warm surface);
   - intrinsics for each camera, and the rotation and translation between them (OpenCV stereo calibration, in the Mac harness, from captured pairs).
2. **Capture:** CameraX preview at 1080p to a GPU texture, beside the thermal stream. It needs the CAMERA permission, and the extra load on the tablet (and battery, with the camera already drawing 500 mA) has to be measured.
3. **Registration:** warp the visible frame into the thermal view with the homography for the current distance, which comes from the autofocus distance or a slider, refined by edge correlation.
4. **Blend:** take the visible luminance's high-pass (edges only, no visible colors or shading), scale it by a strength setting and add it to the thermal display's lightness after the palette. Readouts never change: they come from raw values (CLAUDE.md rule 2).
5. **Checks:**
   - no misregistration ghosts, at each distance;
   - the overlay mustn't pretend a temperature edge exists where only a visible one does. Flag it as a display aid.
   - Benchmark captures would need both cameras recorded together.

## Questions for the owner

- Try it at all, given v1's scope?
- Would the thermal camera move next to the rear lens, on a cable and clip? Without that, it only works for far scenes.
- Which use case first: outdoor night (far, forgiving) or PCB work (close, needs the camera moved)?
