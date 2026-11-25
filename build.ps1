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

# Build Installer, Uninstaller & Portable Distribution in dist/
$DistDir = Join-Path $Root "dist"
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$SetupSrc = Join-Path $Root "installer\setup_main.cpp"
$UninstSrc = Join-Path $Root "installer\uninstall_main.cpp"
if ((Test-Path $SetupSrc) -and (Test-Path $UninstSrc)) {
    $InstCFlags = @("c++", "-std=c++20", "-O2", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-DUNICODE", "-D_UNICODE")
    $InstLibs = @("-luser32", "-lgdi32", "-lshell32", "-lole32", "-luuid", "-ladvapi32", "-ldwmapi", "-lcomctl32")

    $SetupObj = Join-Path $ObjDir "setup_main.o"
    if ($Force -or (-not (Test-Path $SetupObj)) -or ((Get-Item $SetupSrc).LastWriteTimeUtc -gt (Get-Item $SetupObj).LastWriteTimeUtc)) {
        Write-Host "[CC] installer/setup_main.cpp"
        & $Zig @InstCFlags -c $SetupSrc -o $SetupObj
        if ($LASTEXITCODE -ne 0) { throw "Failed compiling setup_main.cpp" }
    }
    $SetupExe = Join-Path $DistDir "CoatroomSetup.exe"
    Write-Host "[LINK] $SetupExe"
    $SetupInputs = @($SetupObj)
    if (Test-Path $RcObj) { $SetupInputs += $RcObj }
    & $Zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" @SetupInputs -o $SetupExe @InstLibs
    if ($LASTEXITCODE -ne 0) { throw "Failed linking CoatroomSetup.exe" }

    $UninstObj = Join-Path $ObjDir "uninstall_main.o"
    if ($Force -or (-not (Test-Path $UninstObj)) -or ((Get-Item $UninstSrc).LastWriteTimeUtc -gt (Get-Item $UninstObj).LastWriteTimeUtc)) {
        Write-Host "[CC] installer/uninstall_main.cpp"
        & $Zig @InstCFlags -c $UninstSrc -o $UninstObj
        if ($LASTEXITCODE -ne 0) { throw "Failed compiling uninstall_main.cpp" }
    }
    $UninstExe = Join-Path $DistDir "CoatroomUninstall.exe"
    Write-Host "[LINK] $UninstExe"
    $UninstInputs = @($UninstObj)
    if (Test-Path $RcObj) { $UninstInputs += $RcObj }
    & $Zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" @UninstInputs -o $UninstExe @InstLibs
    if ($LASTEXITCODE -ne 0) { throw "Failed linking CoatroomUninstall.exe" }

    $UpdaterSrc = Join-Path $Root "installer\updater_main.cpp"
    $UpdaterExe = Join-Path $DistDir "CoatroomUpdater.exe"
    if (Test-Path $UpdaterSrc) {
        $UpdaterObj = Join-Path $ObjDir "updater_main.o"
        $OtaObj = Join-Path $ObjDir "ota_updater.o"
        if ($Force -or (-not (Test-Path $UpdaterObj)) -or ((Get-Item $UpdaterSrc).LastWriteTimeUtc -gt (Get-Item $UpdaterObj).LastWriteTimeUtc) -or ($NewestHeaderUtc -and ($NewestHeaderUtc -gt (Get-Item $UpdaterObj).LastWriteTimeUtc))) {
            Write-Host "[CC] installer/updater_main.cpp"
            & $Zig @InstCFlags -Isrc -c $UpdaterSrc -o $UpdaterObj
            if ($LASTEXITCODE -ne 0) { throw "Failed compiling updater_main.cpp" }
        }
        Write-Host "[LINK] $UpdaterExe"
        $UpdInputs = @($UpdaterObj, $OtaObj)
        if (Test-Path $RcObj) { $UpdInputs += $RcObj }
        & $Zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" @UpdInputs -o $UpdaterExe @InstLibs "-lwinhttp" "-lbcrypt"
        if ($LASTEXITCODE -ne 0) { throw "Failed linking CoatroomUpdater.exe" }
        Copy-Item -Force $UpdaterExe (Join-Path $BuildDir "CoatroomUpdater.exe")
    }

    Copy-Item -Force $OutExe (Join-Path $DistDir "Coatroom.exe")
    if (Test-Path (Join-Path $Root "LICENSE_AND_TOU.txt")) { Copy-Item -Force (Join-Path $Root "LICENSE_AND_TOU.txt") (Join-Path $DistDir "LICENSE_AND_TOU.txt") }
    if (Test-Path (Join-Path $Root "README.md")) { Copy-Item -Force (Join-Path $Root "README.md") (Join-Path $DistDir "README.md") }
    if (Test-Path (Join-Path $Root "resources\coatroom.ico")) {
        $DistRes = Join-Path $DistDir "resources"
        New-Item -ItemType Directory -Force -Path $DistRes | Out-Null
        Copy-Item -Force (Join-Path $Root "resources\coatroom.ico") (Join-Path $DistRes "coatroom.ico")
        Copy-Item -Force (Join-Path $Root "resources\coatroom.ico") (Join-Path $DistDir "coatroom.ico")
    }

    $PortableStage = Join-Path $DistDir "_portable_stage"
    $PortableZip = Join-Path $DistDir "Coatroom-Portable-x64.zip"
    if (Test-Path $PortableStage) { Remove-Item -Recurse -Force $PortableStage }
    if (Test-Path $PortableZip) { Remove-Item -Force $PortableZip }
    New-Item -ItemType Directory -Force -Path $PortableStage | Out-Null
    Copy-Item -Force $OutExe (Join-Path $PortableStage "Coatroom.exe")
    if (Test-Path $UpdaterExe) { Copy-Item -Force $UpdaterExe (Join-Path $PortableStage "CoatroomUpdater.exe") }
    if (Test-Path (Join-Path $Root "LICENSE_AND_TOU.txt")) { Copy-Item -Force (Join-Path $Root "LICENSE_AND_TOU.txt") (Join-Path $PortableStage "LICENSE_AND_TOU.txt") }
    if (Test-Path (Join-Path $Root "README.md")) { Copy-Item -Force (Join-Path $Root "README.md") (Join-Path $PortableStage "README.md") }
    if (Test-Path (Join-Path $Root "resources\coatroom.ico")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $PortableStage "resources") | Out-Null
        Copy-Item -Force (Join-Path $Root "resources\coatroom.ico") (Join-Path $PortableStage "resources\coatroom.ico")
    }
    $AssetsSrc = Join-Path $Root "examples\safety_car\assets"
    if (Test-Path $AssetsSrc) {
        $AssetsDst = Join-Path $PortableStage "examples\safety_car\assets"
        New-Item -ItemType Directory -Force -Path $AssetsDst | Out-Null
        Copy-Item -Force -Recurse (Join-Path $AssetsSrc "*") $AssetsDst
    }
    Compress-Archive -Path (Join-Path $PortableStage "*") -DestinationPath $PortableZip -CompressionLevel Optimal
    Remove-Item -Recurse -Force $PortableStage
    Write-Host "[OK] Packaged $SetupExe, $UninstExe, and $PortableZip"
}
