param(
    [switch]$Clean,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
Set-Location $Root

$ZigCandidates = @(
    "C:\Users\MyHP _)\AppData\Local\Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe\zig-x86_64-windows-0.17.0\zig.exe",
    (Get-Command zig -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue)
)
$Zig = $ZigCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $Zig) {
    throw "zig.exe not found!"
}

$BuildDir = Join-Path $Root "build"
$ObjDir = Join-Path $BuildDir "obj"
if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

$CommonFlags = @(
    "c++",
    "-std=c++20",
    "-O2",
    "-DNOMINMAX",
    "-DWIN32_LEAN_AND_MEAN",
    "-DUNICODE",
    "-D_UNICODE",
    "-Isrc",
    "-Ithird_party",
    "-Ithird_party/imgui",
    "-Ithird_party/imgui/backends"
)

$ImGuiSources = @(
    "third_party/imgui/imgui.cpp",
    "third_party/imgui/imgui_draw.cpp",
    "third_party/imgui/imgui_tables.cpp",
    "third_party/imgui/imgui_widgets.cpp",
    "third_party/imgui/backends/imgui_impl_win32.cpp",
    "third_party/imgui/backends/imgui_impl_dx11.cpp"
)

$AppSources = @(
    "src/mesh/obj_mesh.cpp",
    "src/mesh/fbx_loader.cpp",
    "src/paint/canvas_engine.cpp",
    "src/render/d3d11_renderer.cpp",
    "src/ui/studio_theme.cpp",
    "src/ui/uv_viewport.cpp",
    "src/ui/panels.cpp",
    "src/app/automation_mcp.cpp",
    "src/app/ota_updater.cpp",
    "src/app/app_main.cpp"
)

$Objects = New-Object System.Collections.Generic.List[string]

foreach ($src in $ImGuiSources) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $obj = Join-Path $ObjDir ("imgui_" + $base + ".o")
    $Objects.Add($obj)
    $needsBuild = (-not (Test-Path $obj)) -or ((Get-Item $src).LastWriteTimeUtc -gt (Get-Item $obj).LastWriteTimeUtc)
    if ($needsBuild) {
        Write-Host "[CC] $src"
        & $Zig @CommonFlags -c $src -o $obj
        if ($LASTEXITCODE -ne 0) { throw "Failed compiling $src" }
    }
}

$NewestHeaderUtc = (Get-ChildItem "src" -Filter "*.hpp" -Recurse | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc

foreach ($src in $AppSources) {
    if (-not (Test-Path $src)) {
        throw "Missing source file: $src"
    }
    $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $obj = Join-Path $ObjDir ($base + ".o")
    $Objects.Add($obj)
    $needsBuild = $Force -or (-not (Test-Path $obj)) -or ((Get-Item $src).LastWriteTimeUtc -gt (Get-Item $obj).LastWriteTimeUtc) -or ($NewestHeaderUtc -and ($NewestHeaderUtc -gt (Get-Item $obj).LastWriteTimeUtc))
    if ($needsBuild) {
        Write-Host "[CC] $src"
        & $Zig @CommonFlags -c $src -o $obj
        if ($LASTEXITCODE -ne 0) { throw "Failed compiling $src" }
    }
}

$RcSrc = Join-Path $Root "resources\coatroom.rc"
$RcObj = Join-Path $ObjDir "coatroom_rc.o"
if (Test-Path $RcSrc) {
    $NewestRcUtc = (@($RcSrc, (Join-Path $Root "resources\coatroom.ico"), (Join-Path $Root "resources\coatroom.manifest")) |
        Where-Object { Test-Path $_ } |
        ForEach-Object { (Get-Item $_).LastWriteTimeUtc } |
        Sort-Object -Descending | Select-Object -First 1)
    if ($Force -or (-not (Test-Path $RcObj)) -or ($NewestRcUtc -gt (Get-Item $RcObj).LastWriteTimeUtc)) {
        Write-Host "[RC] resources/coatroom.rc"
        & $Zig rc "/:output-format" coff /fo $RcObj $RcSrc
        if ($LASTEXITCODE -ne 0) { throw "Failed compiling resources/coatroom.rc" }
    }
    $Objects.Add($RcObj)
}

$OutExe = Join-Path $BuildDir "Coatroom.exe"
$LinkLibs = @(
    "-ld3d11",
    "-ldxgi",
    "-ld3dcompiler_47",
    "-lcomdlg32",
    "-ldwmapi",
    "-lgdi32",
    "-lole32",
    "-luuid",
    "-luser32",
    "-lshell32",
    "-lwinhttp",
    "-lbcrypt"
)

Write-Host "[LINK] $OutExe"
& $Zig c++ -std=c++20 -O2 @Objects -o $OutExe @LinkLibs
if ($LASTEXITCODE -ne 0) { throw "Link failed!" }

if (Test-Path (Join-Path $Root "LICENSE_AND_TOU.txt")) {
    Copy-Item -Force (Join-Path $Root "LICENSE_AND_TOU.txt") (Join-Path $BuildDir "LICENSE_AND_TOU.txt")
}

Write-Host "[OK] Built $OutExe ($([math]::Round((Get-Item $OutExe).Length / 1MB, 2)) MB)"

# Build Self-Contained Installer, Updater, Uninstaller & Portable Zip via package_release.ps1
$PackageScript = Join-Path $Root "scripts\package_release.ps1"
if (Test-Path $PackageScript) {
    & $PackageScript -Owner "IcecreamplayzYT" -Repo "Coatroom" -Version "1.3.0" -SkipAppBuild
    if ($LASTEXITCODE -ne 0) { throw "package_release.ps1 failed!" }
    $DistUpdater = Join-Path $Root "dist\CoatroomUpdater.exe"
    if (Test-Path $DistUpdater) {
        Copy-Item -Force $DistUpdater (Join-Path $BuildDir "CoatroomUpdater.exe")
    }
}
