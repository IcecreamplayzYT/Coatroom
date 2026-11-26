# Coatroom (`[Cr]`) v1.3.0

**Coatroom** is a native Windows C++20 / Direct3D 11 / Dear ImGui 3D automotive livery design and studio beauty-rendering application.

## Key Features

- **3D Car Mesh Support (`.obj` & `.fbx`):** Native Binary (`6100..7700+` incl. zlib) and ASCII `.fbx` loader alongside fast `.obj` / `.mtl` loading with automatic Y-up conversion and scale normalization.
- **Synchronized 3D Viewport & 4K 2D UV Canvas:** Paint and place vector graphics directly in 3D or on a `4096×4096` UV artboard with live PBR rendering, `X/Y/Z` camera gizmo, pixel rulers (`Ctrl+R`), cyan snap guides, and mipmapped premultiplied-alpha anisotropic image downscaling.
- **Vector & Raster Toolset:**
  - Cubic **Bezier Pen (`P`)** & **Direct Select** with smooth/corner toggles, right-click anchor removal, and live **Mirror** preview (`1.0 - uv.x`) across 2D and 3D
  - **Line (`\`)**, **Rectangle (`M`)**, **Type (`T`)**, **Image (`D`)**, **Brush (`B`)**, **Eraser (`E`)**, **Fill (`G`)**, and **Eyedropper (`I`)**
- **Container Layers & 200-Step CoW Undo:** Non-destructive `VectorItem` containers + lazy raster paint buffers, drag-and-drop layer reordering, `Multiply` AO baking, and 200-step Copy-on-Write undo/redo.
- **Project & Export Formats:** Native `.cr` (`CRM1`) project files (backward-compatible with `.lps`), 4K PNG livery export, and 1080p **Render Photo (`F12`)** studio shots.
- **Automation & MCP:** Built-in JSON `--script` runner and `--mcp-stdio` server (`mcp/coatroom_mcp.py`).
- **Installer, Standalone Updater & GitHub Releases OTA:** Per-user (`asInvoker`) dark Win32 GUI installer/uninstaller (`CoatroomSetup.exe` detects existing installs and updates cleanly; `CoatroomUninstall.exe`), plus standalone `CoatroomUpdater.exe` for in-place `Coatroom.exe` updates over HTTPS + Windows BCrypt SHA-256 verification.

## Building from Source (Windows)

Coatroom builds with **Zig 0.17.0** (`zig c++`, `-std=c++20`) via `build.ps1`:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Force
```

Verify headless smoke tests:

```powershell
.\build\Coatroom.exe --smoke-test --smoke-out build\smoke_test
```

Generate/refresh the multi-resolution app icon (`16..256` RGBA PNG-in-ICO):

```powershell
python scripts/gen_coatroom_ico.py --out resources/coatroom.ico
```

## Packaging & GitHub Releases OTA

Bundle `dist/CoatroomSetup.exe`, `dist/CoatroomUpdater.exe`, `dist/CoatroomUninstall.exe`, `dist/Coatroom.exe`, `dist/Coatroom-Portable-x64.zip`, and `dist/release-manifest.json` (with exact SHA-256 for both `CoatroomSetup.exe` and `Coatroom.exe`):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_release.ps1 -Owner IcecreamplayzYT -Repo Coatroom -Version 1.3.0
```

See [DISTRIBUTION.md](DISTRIBUTION.md) for the GitHub Releases upload checklist, `release-manifest.json` schema, Windows Defender WDSI submission workflow, and SmartScreen guidance.

## Terms of Use & Third-Party Credits

Coatroom is offline by default with zero telemetry; update checks are user-initiated over HTTPS with SHA-256 verification. See [LICENSE_AND_TOU.txt](LICENSE_AND_TOU.txt) for full terms and third-party notices (Dear ImGui, stb, DirectX 11, WinHTTP/BCrypt).
