# Coatroom (`[Cr]`) v1.2 — Architecture & Development Notes

**Native Windows 3D + 2D 4K Automotive Livery & Photo-Rendering Studio** built in C++20, Direct3D 11, Dear ImGui, Win32, WinHTTP, and Windows CNG BCrypt.

## Project Directory Layout

- `src/math/geom.hpp`: Vec2/Vec3/Vec4, Mat4, Ray, RayHit, Camera, and color utilities.
- `src/mesh/obj_mesh.hpp`, `src/mesh/obj_mesh.cpp`: Custom Wavefront `.obj` loader, BVH/spatial raycaster, 3D mirror (`centerSymmetryX`), vertex/face editing & sub-selection, and AO approximation baking.
- `src/paint/canvas_engine.hpp`, `src/paint/canvas_engine.cpp`:
  - **Container `Layer` + `VectorItem` Model:** Every layer holds a lazy copy-on-write raster `paintBuffer` (`ensureUniquePaintBuffer`) alongside an ordered list of non-destructive `VectorItem`s (`PenPath`, `RectShape`, `Text`, `Image`), eliminating unwanted layer spawning.
  - **200-Step CoW Undo/Redo (`kMaxUndoSteps = 200`):** Lightweight `DocumentSnapshot` history covering brush/eraser/fill strokes, Pen/Rect/Text/Image edits, `Copy`/`Cut`/`Paste`, `Mirror` toggles, layer deletion (`removeLayer`), and drag-and-drop layer reordering (`reorderLayer`).
  - **Native `.cr` Format (`CRM1`):** Self-contained binary project format preserving layers, raster paint buffers, vector items, and embedded image pixel blobs (backward-compatible with `.lps` `LPS2`/`LPS1`).
  - **`renderBrandLogoRgba(int sizePx)`:** Anti-aliased Adobe-style squircle (`#052228 -> #041B20`) with bold `#2FE6CE` `Cr` monogram via `stb_truetype` (`segoeuib.ttf`).
- `src/render/d3d11_renderer.hpp`, `src/render/d3d11_renderer.cpp`: Direct3D 11 PBR showroom renderer (3 softboxes, clearcoat, shadow map, wireframe/selection overlays, offscreen `1920x1080` Render Photo export, ImGui DX11 viewport hosting).
- `src/ui/ui_types.hpp`, `src/ui/studio_theme.hpp/.cpp`, `src/ui/panels.hpp/.cpp`, `src/ui/uv_viewport.hpp/.cpp`:
  - Flat Adobe/DaVinci/Blender neutral studio theme (`#1E1E1E` / `#262626` / `#2E2E2E`).
  - Illustrator-style Home screen (presets, filterable/sortable Recent projects, Crash Recovery banner).
  - Top bar with House (`Home`) toggle, `[Cr]` squircle badge, Adobe-style menus, `.cr` document indicator, circled `!` OTA update indicator, and `Render Photo` trigger.
  - Single-column `46px` left tool rail (`32x32` buttons, corner flyout menus for `Pen`/`Line` & `Hand`/`Zoom`, large `22x22` swatches), animated `262x208px` Rich Tooltips (`View > Rich Tooltips`), container Layers panel (drag-reorder, nested items, no `+` toolbar), per-panel context menus (`Layers`, `2D UV Canvas`, `3D Viewport`, `Color`, `Outliner` — none on `Properties`), and upgraded Cubic Bezier Pen UX (RMB anchor deletion, RMB handle collapse, segment bending, Alt/double-click smooth/corner toggle, live Mirror preview).
- `src/app/app_main.cpp`: Win32 DX11 host, Home Screen & `%LOCALAPPDATA%\Coatroom\recent.json`, Crash Protection (45s dirty autosave + `SetUnhandledExceptionFilter` emergency `.cr` snapshot in `%LOCALAPPDATA%\Coatroom\recovery\`), `CF_DIB`/PNG external + internal clipboard Copy/Cut/Paste, `--smoke-test`, `--script`, `--mcp-stdio`, `--screenshot`, `--home`, `--screenshot-tooltip <tool>`.
- `src/app/ota_updater.hpp/.cpp`: Native WinHTTP (`WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`) + BCrypt SHA-256 updater (`checkForOtaUpdates`, `downloadAndVerifyOtaInstaller`, `runOtaSelfTest`) supporting GitHub Releases `release-manifest.json`.
- `src/app/automation_mcp.hpp/.cpp` & `mcp/coatroom_mcp.py`: Headless JSON script runner & stdio MCP bridge (`coatroom_*` tools).
- `installer/setup_main.cpp`, `installer/updater_main.cpp`, `installer/uninstall_main.cpp`: Per-user (`asInvoker`) dark Win32 setup wizard (`HKCU` `.cr` association, ToU gate), standalone in-place `CoatroomUpdater.exe`, and clean uninstaller.
- `resources/coatroom.ico`, `coatroom.manifest`, `coatroom.rc` & `scripts/`: Branding icon generator (`gen_coatroom_ico.py`) and release bundler (`package_release.ps1`).
- `examples/safety_car/`: 40-command FIA Porsche 992 GT3 R Safety Car showcase (`build_safety_car.json`, `safety_car_v12.cr`, `SafetyCar_992_GT3R_4K.png`, 4x 1080p renders, UI capture).

## Build & Verification Commands

```powershell
# Compile Coatroom.exe + installer/updater/uninstaller + portable ZIP
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Force

# Package dist/ + release-manifest.json for GitHub Releases (IcecreamplayzYT/Coatroom)
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_release.ps1 -Owner "IcecreamplayzYT" -Repo "Coatroom" -Version "1.2.0"

# Run full headless 12-check verification suite
.\build\Coatroom.exe --smoke-test --smoke-out build\smoke_test

# Rebuild Safety Car showcase & renders
.\build\Coatroom.exe --script examples\safety_car\build_safety_car.json

# Capture UI states cleanly
.\build\Coatroom.exe --home --screenshot build\home_ui.png
.\build\Coatroom.exe --screenshot-tooltip Pen --screenshot build\tooltip_pen.png
```

## Workspace Isolation Rule
Never modify files under `C:\Users\MyHP _)\.gemini\antigravity\scratch\Porsche_992_GT3R_Livery_Template\4K\`. All Coatroom app work stays isolated inside `C:\Users\MyHP _)\.gemini\antigravity\scratch\LiveryPaintStudio\`.
