# Coatroom Windows Release & Distribution Guide (Unsigned Build)

This guide covers packaging, publishing, verifying, and maintaining unsigned Windows releases of **Coatroom** (`v1.2.0+`), including GitHub Releases, OTA `release-manifest.json`, Microsoft Defender false-positive mitigation, and SmartScreen guidance.

---

## 1. Distribution Formats: Portable ZIP vs. `CoatroomSetup.exe`

Coatroom ships in two clean, packer-free formats (no UPX, no self-extracting archive stubs):

| Format | Target Audience | Contents | Installs To / Footprint |
| :--- | :--- | :--- | :--- |
| **Portable ZIP** (`Coatroom-Portable-x64.zip`) | Power users, USB/sandbox testing, studios without registry changes | `Coatroom.exe`, `CoatroomUpdater.exe`, `LICENSE_AND_TOU.txt`, `coatroom.ico`, `assets/` | Any folder chosen by user; zero registry writes, zero shortcuts. |
| **Installer Bundle** (`CoatroomSetup.exe` + loose folder) | Standard Windows users (Release 1 & fresh installs) | `CoatroomSetup.exe`, `CoatroomUpdater.exe`, `CoatroomUninstall.exe`, `Coatroom.exe`, `LICENSE_AND_TOU.txt`, `coatroom.ico`, `assets/` | Per-user `%LOCALAPPDATA%\Coatroom` (`asInvoker`, no UAC prompt). If Coatroom is already installed, `CoatroomSetup.exe` detects the existing install and switches to **Update** mode. |
| **In-Place OTA Updater** (`CoatroomUpdater.exe`) | Existing users receiving Release 2+ updates | Standalone Win32 updater bundled in install dir | Downloads and SHA-256-verifies `Coatroom.exe` (`exe_url` / `exe_sha256`), swaps it atomically in place without reinstalling, and relaunches cleanly. |

> **Why loose-file `CopyFileW` instead of a single packed EXE?**
> Self-extracting archives and UPX-packed binaries trigger far more heuristic false positives in Windows Defender and third-party AV engines than plain Zig/Clang PE binaries copied via standard Win32 `CopyFileW`.

---

## 2. Building Release Artifacts

1. **Regenerate app icon (if branding changed):**
   ```powershell
   python scripts/gen_coatroom_ico.py --out resources/coatroom.ico
   ```
2. **Compile Windows resource object (`coatroom.rc` -> `.res` / COFF `.o`):**
   ```powershell
   $zig = "C:\Users\MyHP _)\AppData\Local\Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe\zig-x86_64-windows-0.17.0\zig.exe"
   & $zig rc /:output-format coff /fo build/obj/coatroom_rc.o resources/coatroom.rc
   ```
3. **Build `Coatroom.exe`:**
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Force
   ```
4. **Build `CoatroomSetup.exe` and `CoatroomUninstall.exe`:**
   ```powershell
   $cflags = @("-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-DUNICODE", "-D_UNICODE")
   $libs   = @("-luser32", "-lgdi32", "-lshell32", "-lole32", "-luuid", "-ladvapi32", "-ldwmapi", "-lcomctl32")

   & $zig c++ @cflags -c installer/setup_main.cpp -o build/obj/setup_main.o
   & $zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" build/obj/setup_main.o build/obj/coatroom_rc.o -o dist/CoatroomSetup.exe @libs

   & $zig c++ @cflags -c installer/uninstall_main.cpp -o build/obj/uninstall_main.o
   & $zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" build/obj/uninstall_main.o build/obj/coatroom_rc.o -o dist/CoatroomUninstall.exe @libs
   ```
5. **Stage loose files in `dist/`:**
   - `dist/Coatroom.exe`
   - `dist/CoatroomSetup.exe`
   - `dist/CoatroomUninstall.exe`
   - `dist/LICENSE_AND_TOU.txt`
   - `dist/coatroom.ico` (from `resources/coatroom.ico`)
   - `dist/assets/` (any runtime shaders/templates/presets)

---

## 3. GitHub Release & `release-manifest.json` Schema

Coatroom's native WinHTTP OTA updater (`src/app/ota_updater.hpp/.cpp`) fetches `release-manifest.json` over **HTTPS** (`https://...` only; `http://127.0.0.1` is accepted exclusively for local tests) using `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` and `User-Agent: Coatroom/1.2.0`, then verifies downloaded binaries with **Windows CNG BCrypt SHA-256**.

### Schema

Publish `release-manifest.json` alongside your release assets on GitHub Releases (e.g. `https://github.com/IcecreamplayzYT/Coatroom/releases/latest/download/release-manifest.json`):

```json
{
  "version": "1.2.0",
  "download_url": "https://github.com/IcecreamplayzYT/Coatroom/releases/download/v1.2.0/CoatroomSetup.exe",
  "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "exe_url": "https://github.com/IcecreamplayzYT/Coatroom/releases/download/v1.2.0/Coatroom.exe",
  "exe_sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
  "release_notes": "Coatroom v1.2.0: Adobe squircle branding, .cr container projects, 200-step CoW undo, Pen/Rect UX polish, and native OTA updater.",
  "published_at": "2026-11-25T12:00:00Z"
}
```

### Field Reference

| Field | Required | Format / Notes |
| :--- | :--- | :--- |
| `version` | Yes | Semantic version (`MAJOR.MINOR.PATCH`, optional `-prerelease`). Leading `v` is tolerated. |
| `download_url` | Yes | Must be `https://` (`downloadUrl` alias also accepted). Points to `CoatroomSetup.exe` for fresh installs. |
| `sha256` | Yes | Exact 64-character hexadecimal SHA-256 digest of `download_url`. Case-insensitive. |
| `exe_url` | Recommended | Direct `https://` URL to `Coatroom.exe` (`exeUrl` alias accepted). Used by `CoatroomUpdater.exe` for fast in-place updates without reinstalling. |
| `exe_sha256` | Required if `exe_url` set | Exact 64-character hexadecimal SHA-256 digest of `Coatroom.exe` (`exeSha256` alias accepted). |
| `release_notes` | No | Plain-text summary displayed in Coatroom's Check for Updates modal & `CoatroomUpdater.exe` (`releaseNotes` alias accepted). |
| `published_at` | No | ISO-8601 UTC timestamp (`publishedAt` alias accepted). |

### Compute Exact SHA-256 Before Uploading

```powershell
(Get-FileHash -Algorithm SHA256 .\dist\CoatroomSetup.exe).Hash.ToLowerInvariant()
```

Never update `release-manifest.json` before uploading the release asset, and never modify an already-published binary without updating `sha256` in `release-manifest.json`—otherwise `downloadAndVerifyOtaInstaller()` will reject and delete the download.

---

## 4. Microsoft Security Intelligence (WDSI) False-Positive Workflow

Unsigned Windows executables compiled with MinGW/Clang/Zig can occasionally trigger generic heuristic flags (such as `Trojan:Win32/Wacatac` or `Bearfoos`) on fresh builds. Before announcing a release:

1. **Test on a clean Windows 10/11 VM** with Microsoft Defender Antivirus enabled and cloud-delivered protection active.
2. **Submit binaries proactively as a Software Developer** via Microsoft Security Intelligence:
   - Portal: `https://www.microsoft.com/en-us/wdsi/filesubmission`
   - Choose **Software developer** -> sign in with a Microsoft account.
   - Upload `Coatroom.exe`, `CoatroomSetup.exe`, and `CoatroomUninstall.exe` (one submission per binary, or packaged clean ZIP if needed).
   - Select **"Incorrectly detected as malware / false positive"** (or proactive clean submission) and state clearly: *"Native C++20 Direct3D 11 + Dear ImGui 3D automotive livery editor compiled with Zig/LLVM; per-user asInvoker installer with no packers or obfuscation."*
3. **Wait for Microsoft's analyst determination** (typically 24–72 hours for clean PE binaries). Once cleared, the specific SHA-256 definitions propagate via Defender Security Intelligence updates.
4. **If an end-user still sees a detection** after a new release, note the exact **Security intelligence version** and re-submit the exact released SHA-256 via WDSI.

---

## 5. Windows SmartScreen Reputation Guidance

Even when Defender confirms a file is clean, **Microsoft Defender SmartScreen** warns on newly released unsigned executables until enough users run them or an Authenticode certificate signs them:

- **What users will see:** *"Windows protected your PC — Microsoft Defender SmartScreen prevented an unrecognized app from starting."*
- **Recommended instructions for your GitHub Release notes:**
  1. Verify the downloaded file's SHA-256 matches the published checksum in `release-manifest.json`:
     ```powershell
     certutil -hashfile CoatroomSetup.exe SHA256
     ```
  2. In the SmartScreen prompt, click **More info** -> **Run anyway**.
- **Building reputation over time:**
  - Keep consistent `CompanyName` (`Coatroom Studio`), `ProductName` (`Coatroom`), and `OriginalFilename` in `resources/coatroom.rc`.
  - Avoid changing PE packers/obfuscators (none are used).
  - If you later adopt code signing (e.g., Azure Trusted Signing or an OV/EV Authenticode certificate), sign `Coatroom.exe`, `CoatroomSetup.exe`, and `CoatroomUninstall.exe` **before** computing `sha256` for `release-manifest.json`.
