# MPC-HC Ambilight

This fork adds a GPU ambient background to the D3D11 renderer. The processed
video is captured before subtitles and OSD. The original video rectangle stays
sharp; glow is composited only into surrounding player-generated bars.
There is no CPU frame readback in the effect.

## Controls

Use the paired MPC-HC fork and click **Ambilight**, immediately left of the
volume controls on the bottom toolbar, while a video is loaded. The modeless
panel previews every valid change immediately, including while paused, and
saves automatically. Closing the panel keeps the current values.

Controls: Enabled, Intensity, Blur, Edge size, Spread, Spread fade start,
Spread fade curve, Brightness, Colors (vibrance), Saturation, Contrast,
Debanding (noise), and four independent directions. Reset restores numeric
defaults without changing Enabled. The renderer property page also exposes
Enabled, Intensity and Blur with immediate preview.

First-run defaults follow the user's preset: Enabled, Intensity 53, Blur 55,
Edge size 27, Spread 55, fade start 15, fade curve 35, Brightness/Contrast/
Colors/Saturation 100, Debanding 0, all four directions enabled. Existing
saved settings survive updates; Reset restores these numeric defaults.

Blur uses a five-level FP16 pyramid (256x144 through 16x9), linear-light
prefiltering, and dense separable Gaussian passes. Adjacent levels are blended
continuously across the 0-100 slider. High blur mixes broad color regions
instead of spacing a few samples far apart, avoiding visible block patterns.

## Integration and limits

Existing COM IDs and public IVideoRenderer/Settings_t ABI are retained.
Controls use IExFilterConfig: ambilightEnabled is a bool; integer keys,
ranges and defaults are defined in Source/AmbilightSettings.h (mirrored in
player include directory). Setters redraw the current instance;
Flt_SetBool("ambilightSave", true) persists only Ambilight settings.
IVideoRenderer::SaveSettings also persists settings.

This fork uses its own HKCU key:
Software\MPC-BE Filters\MPC Video Renderer Ambilight.
Place MpcVideoRenderer64.ax in the portable player's MPCVR folder.
No COM registration is needed.

SDR and player-generated letterbox/pillarbox areas are supported. HDR sources,
Windows HDR output, RTX Video HDR, D3D9 and stereo-interlaced output bypass the
effect. Encoded black bars are not detected. Exact-fit video has no glow area.
GPU resources are cached and recreated on size/format changes. Resource or
shader failure skips the effect for that media/device session.

## Build and validation

Requires Visual Studio 2022 C++ tools, ATL/MFC and a Windows 10/11 SDK.
Initialize submodules recursively, then run build-ambilight.cmd.
It builds Release x64, compiles all three HLSL shaders and runs D3D11 WARP
pixel checks plus configuration range/roundtrip checks against the built DLL.

With FFmpeg available, run tests/run-playback.ps1 for real decoder/renderer
checks on wide SDR, portrait, 4K SDR, exact-fit and HDR fixtures. These verify
unchanged sharp-video pixels, SDR glow, HDR bypass, paused live changes, seek,
resize and resume. Wide and 4K tests also assert smooth gradients at Blur 100.
Tests use a hidden render owner and never save renderer settings.

The blur fix passed all five scenarios. Maximum adjacent bar-channel steps
fell from 94 to 1 (wide) and 84 to 1 (4K) between Blur 0 and 100. Sharp-video
pixels were identical with the effect off and on. Long-film performance,
subtitle and fullscreen use still need everyday playback validation.
