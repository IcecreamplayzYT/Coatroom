# Coatroom (`[Cr]`) v1.3 — Architecture & Development Notes

**Native Windows 3D + 2D 4K Automotive Livery & Photo-Rendering Studio** built in C++20, Direct3D 11, Dear ImGui, Win32, WinHTTP, and Windows CNG BCrypt.

## Project Directory Layout

- `src/math/geom.hpp`: Vec2/Vec3/Vec4, Mat4, Ray, RayHit, Camera, and color utilities.
- `src/mesh/obj_mesh.hpp`, `src/mesh/obj_mesh.cpp`, `src/mesh/fbx_loader.hpp`, `src/mesh/fbx_loader.cpp`:
  - Custom Wavefront `.obj` loader + native **Binary & ASCII `.fbx` loader** (`6100..7700+`, `stbi_zlib_decode_buffer` deflate streams, `Connections` hierarchy, TRS + `PreRotation` + `Geometric*`, `UpAxis` Z-up -> Y-up, fan triangulation, `LayerElementUV`/`LayerElementNormal`), BVH raycaster, 3D mirror (`centerSymmetryX`), vertex/face editing & sub-selection, and AO approximation baking.
- `src/paint/canvas_engine.hpp`, `src/paint/canvas_engine.cpp`:
  - **Premultiplied Mipmapped Anisotropic Filtering (`MipChainRgba` + `sampleMipAniso`):** Box/tap premultiplied-alpha mip pyramids (`DecalLayerProps::mipChain`), 4x anisotropic trilinear filtering on minification (`lod > 0`) and premultiplied Catmull-Rom bicubic on magnification (`lod <= 0`), eliminating jagged 2x–16x downscale aliasing and dark border fringes across both 2D UV and 3D projection.
  - **Container `Layer` + `VectorItem` Model:** Lazy copy-on-write raster `paintBuffer` (`ensureUniquePaintBuffer`) alongside non-destructive `VectorItem`s (`PenPath`, `RectShape`, `Text`, `Image`).
  - **Grouped Human-Readable Fonts, Area Type & Touch Type:** `scanSystemFonts()` parses TTF/OTF/TTC `name` tables into `fontFamilies` + `styles`, paired with word-wrapped `isAreaBox` layout and per-character `CharStyle` styling.
  - **200-Step CoW Undo/Redo & Native `.cr` Format (`CRM1`):** Self-contained project format (backward-compatible with `.lps` `LPS2`/`LPS1`).
  - **`renderBrandLogoRgba(int sizePx)`:** Anti-aliased squircle (`#052228 -> #041B20`) with crisp `#2FE6CE` `Cr` monogram.
- `src/render/d3d11_renderer.hpp`, `src/render/d3d11_renderer.cpp`: Direct3D 11 PBR showroom renderer (3 softboxes, clearcoat, shadow map, wireframe/selection overlays, offscreen `1920x1080` Render Photo & `320x180` Front 3/4th thumbnail export).
- `src/ui/ui_types.hpp`, `src/ui/studio_theme.hpp/.cpp`, `src/ui/panels.hpp/.cpp`, `src/ui/uv_viewport.hpp/.cpp`:
  - Flat Adobe/DaVinci/Blender neutral studio theme, Illustrator-style Home screen (`.obj`/`.fbx`/`.cr`/`.png` actions, Front 3/4th Recent thumbnails, Crash Recovery banner), `20x20` `[Cr]` badge, single-column left rail, animated `262x208px` Rich Tooltips, grouped Font Family/Style + Touch Type (`Shift+T`) Character panel, per-panel context menus (none on `Properties`), and Cubic Bezier Pen UX.
- `src/app/app_main.cpp`, `src/app/ota_updater.hpp/.cpp`, `src/app/automation_mcp.hpp/.cpp`: Win32 DX11 host, crash recovery, clipboard Copy/Cut/Paste, `--smoke-test`, `--script`, `--mcp-stdio`, and WinHTTP + BCrypt SHA-256 GitHub Releases OTA updater (`IcecreamplayzYT/Coatroom`).
- `installer/setup_main.cpp`, `installer/updater_main.cpp`, `installer/uninstall_main.cpp`: Self-contained per-user (`asInvoker`) `CoatroomSetup.exe` bundling `RT_RCDATA` (`102..106`: `Coatroom.exe`, `CoatroomUninstall.exe`, `CoatroomUpdater.exe`, `coatroom.ico`, `LICENSE_AND_TOU.txt`), in-place `CoatroomUpdater.exe` extracting `RCDATA 102` directly from `CoatroomSetup.exe`, and clean uninstaller.

## Build & Release Commands

```powershell
# Compile Coatroom.exe + installer/updater/uninstaller + portable ZIP
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Force

# Package self-contained v1.3.0 dist/ + release-manifest.json for GitHub Releases
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_release.ps1 -Owner "IcecreamplayzYT" -Repo "Coatroom" -Version "1.3.0"

# Run full headless 15-check verification suite
.\build\Coatroom.exe --smoke-test --smoke-out build\smoke_test

# Upload clean 4-asset GitHub Release via gh CLI
gh release create v1.3.0 dist\CoatroomSetup.exe dist\CoatroomUpdater.exe dist\Coatroom-Portable-x64.zip dist\release-manifest.json
```

## Workspace Isolation Rule
Never modify files under `C:\Users\MyHP _)\.gemini\antigravity\scratch\Porsche_992_GT3R_Livery_Template\4K\`. All Coatroom app work stays isolated inside `C:\Users\MyHP _)\.gemini\antigravity\scratch\LiveryPaintStudio\`.
