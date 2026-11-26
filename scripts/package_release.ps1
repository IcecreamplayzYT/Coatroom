param(
    [Parameter(Mandatory = $false)]
    [string]$Owner = "IcecreamplayzYT",
    [Parameter(Mandatory = $false)]
    [string]$Repo = "Coatroom",
    [Parameter(Mandatory = $false)]
    [string]$Version = "1.3.0",
    [Parameter(Mandatory = $false)]
    [string]$ReleaseNotes = "Coatroom release",
    [Parameter(Mandatory = $false)]
    [string]$AppExeOverride = "",
    [Parameter(Mandatory = $false)]
    [string]$OutDir = "",
    [switch]$SkipAppBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$ZigCandidates = @(
    "C:\Users\MyHP _)\AppData\Local\Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe\zig-x86_64-windows-0.17.0\zig.exe",
    (Get-Command zig -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue)
)
$Zig = $ZigCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $Zig) {
    throw "zig.exe not found!"
}

if (-not $SkipAppBuild -and [string]::IsNullOrWhiteSpace($AppExeOverride)) {
    Write-Host "[1/6] Building Coatroom.exe..."
    powershell -ExecutionPolicy Bypass -File (Join-Path $Root "build.ps1") -Force
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed!" }
}

$AppExe = if ([string]::IsNullOrWhiteSpace($AppExeOverride)) {
    Join-Path $Root "build\Coatroom.exe"
} else {
    $AppExeOverride
}
if (-not (Test-Path $AppExe)) {
    throw "Missing Coatroom.exe at $AppExe"
}

$ObjDir = Join-Path $Root "build\obj"
$DistDir = if ([string]::IsNullOrWhiteSpace($OutDir)) { Join-Path $Root "dist" } else { $OutDir }
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Write-Host "[2/6] Compiling Windows resources (coatroom.rc)..."
$RcObj = Join-Path $ObjDir "coatroom_rc.o"
& $Zig rc "/:output-format" coff /fo $RcObj (Join-Path $Root "resources\coatroom.rc")
if ($LASTEXITCODE -ne 0) { throw "zig rc failed!" }

Write-Host "[3/6] Compiling CoatroomUninstall.exe & CoatroomUpdater.exe..."
$CFlags = @("-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-DUNICODE", "-D_UNICODE", "-Isrc")
$WinLibs = @("-luser32", "-lgdi32", "-lshell32", "-lole32", "-luuid", "-ladvapi32", "-ldwmapi", "-lcomctl32")

$UninstObj = Join-Path $ObjDir "uninstall_main.o"
$UpdaterObj = Join-Path $ObjDir "updater_main.o"
$OtaObj = Join-Path $ObjDir "ota_updater_pkg.o"

$UninstExe = Join-Path $DistDir "CoatroomUninstall.exe"
$UpdaterExe = Join-Path $DistDir "CoatroomUpdater.exe"

& $Zig c++ @CFlags -c (Join-Path $Root "installer\uninstall_main.cpp") -o $UninstObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling uninstall_main.cpp" }
& $Zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" $UninstObj $RcObj -o $UninstExe @WinLibs
if ($LASTEXITCODE -ne 0) { throw "Failed linking CoatroomUninstall.exe" }

& $Zig c++ @CFlags -c (Join-Path $Root "src\app\ota_updater.cpp") -o $OtaObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling ota_updater.cpp" }
& $Zig c++ @CFlags -c (Join-Path $Root "installer\updater_main.cpp") -o $UpdaterObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling updater_main.cpp" }
& $Zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" $UpdaterObj $OtaObj $RcObj -o $UpdaterExe @WinLibs -lwinhttp -lbcrypt
if ($LASTEXITCODE -ne 0) { throw "Failed linking CoatroomUpdater.exe" }

Write-Host "[4/6] Bundling self-contained CoatroomSetup.exe (RT_RCDATA 102..106)..."
$IcoPath = Join-Path $Root "resources\coatroom.ico"
$TouPath = Join-Path $Root "LICENSE_AND_TOU.txt"
$BaseRcPath = Join-Path $Root "resources\coatroom.rc"

function Format-RcPath([string]$p) {
    return ((Resolve-Path $p).Path -replace '\\', '/')
}

$SetupBundleRc = Join-Path $ObjDir "setup_bundle.rc"
$SetupBundleObj = Join-Path $ObjDir "setup_bundle.o"
$RcContent = @"
#include "$(Format-RcPath $BaseRcPath)"
102 RCDATA "$(Format-RcPath $AppExe)"
103 RCDATA "$(Format-RcPath $UninstExe)"
104 RCDATA "$(Format-RcPath $UpdaterExe)"
105 RCDATA "$(Format-RcPath $IcoPath)"
106 RCDATA "$(Format-RcPath $TouPath)"
"@
[System.IO.File]::WriteAllText($SetupBundleRc, $RcContent, [System.Text.UTF8Encoding]::new($false))

& $Zig rc "/:output-format" coff /I (Join-Path $Root "resources") /fo $SetupBundleObj $SetupBundleRc
if ($LASTEXITCODE -ne 0) { throw "zig rc failed on setup_bundle.rc!" }

$SetupObj = Join-Path $ObjDir "setup_main.o"
$SetupExe = Join-Path $DistDir "CoatroomSetup.exe"
& $Zig c++ @CFlags -c (Join-Path $Root "installer\setup_main.cpp") -o $SetupObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling setup_main.cpp" }
& $Zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" $SetupObj $SetupBundleObj -o $SetupExe @WinLibs
if ($LASTEXITCODE -ne 0) { throw "Failed linking CoatroomSetup.exe" }

Write-Host "[5/6] Creating Coatroom-Portable-x64.zip..."
$PortableStage = Join-Path $DistDir "_portable_stage"
$PortableZip = Join-Path $DistDir "Coatroom-Portable-x64.zip"
if (Test-Path -LiteralPath $PortableStage) { [System.IO.Directory]::Delete($PortableStage, $true) }
if (Test-Path -LiteralPath $PortableZip) { [System.IO.File]::Delete($PortableZip) }
New-Item -ItemType Directory -Force -Path $PortableStage | Out-Null
Copy-Item -Force $AppExe (Join-Path $PortableStage "Coatroom.exe")
Copy-Item -Force $UpdaterExe (Join-Path $PortableStage "CoatroomUpdater.exe")
Copy-Item -Force $TouPath (Join-Path $PortableStage "LICENSE_AND_TOU.txt")
Copy-Item -Force $IcoPath (Join-Path $PortableStage "coatroom.ico")
Compress-Archive -Path (Join-Path $PortableStage "*") -DestinationPath $PortableZip -CompressionLevel Optimal
[System.IO.Directory]::Delete($PortableStage, $true)

Write-Host "[6/6] Generating release-manifest.json..."
[System.IO.Directory]::GetFiles($DistDir, "*.pdb") | ForEach-Object { [System.IO.File]::Delete($_) }
$SetupSha = (Get-FileHash -Algorithm SHA256 $SetupExe).Hash.ToLowerInvariant()
$CleanVersion = $Version.TrimStart('v', 'V')
$ManifestObj = [ordered]@{
    version       = $CleanVersion
    download_url  = "https://github.com/$Owner/$Repo/releases/download/v$CleanVersion/CoatroomSetup.exe"
    sha256        = $SetupSha
    release_notes = $ReleaseNotes
    published_at  = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
}
$ManifestJson = $ManifestObj | ConvertTo-Json -Depth 4
$ManifestPath = Join-Path $DistDir "release-manifest.json"
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Host ""
Write-Host "[OK] Packaged Coatroom v$CleanVersion in $DistDir"
Write-Host "     CoatroomSetup.exe SHA-256: $SetupSha"
Write-Host "     Manifest: $ManifestPath"
