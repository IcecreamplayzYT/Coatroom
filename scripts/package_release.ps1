param(
    [Parameter(Mandatory = $false)]
    [string]$Owner = "IcecreamplayzYT",
    [Parameter(Mandatory = $false)]
    [string]$Repo = "Coatroom",
    [Parameter(Mandatory = $false)]
    [string]$Version = "1.2.0",
    [Parameter(Mandatory = $false)]
    [string]$ReleaseNotes = "Coatroom release",
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

if (-not $SkipAppBuild) {
    Write-Host "[1/6] Building Coatroom.exe..."
    powershell -ExecutionPolicy Bypass -File (Join-Path $Root "build.ps1") -Force
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed!" }
}

$AppExe = Join-Path $Root "build\Coatroom.exe"
if (-not (Test-Path $AppExe)) {
    throw "Missing build\Coatroom.exe (run without -SkipAppBuild or run build.ps1 first)"
}

$ObjDir = Join-Path $Root "build\obj"
$DistDir = Join-Path $Root "dist"
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Write-Host "[2/6] Compiling Windows resources (coatroom.rc)..."
$RcObj = Join-Path $ObjDir "coatroom_rc.o"
& $Zig rc "/:output-format" coff /fo $RcObj (Join-Path $Root "resources\coatroom.rc")
if ($LASTEXITCODE -ne 0) { throw "zig rc failed!" }

Write-Host "[3/6] Compiling CoatroomSetup.exe, CoatroomUpdater.exe & CoatroomUninstall.exe..."
$CFlags = @("-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-DUNICODE", "-D_UNICODE", "-Isrc")
$WinLibs = @("-luser32", "-lgdi32", "-lshell32", "-lole32", "-luuid", "-ladvapi32", "-ldwmapi", "-lcomctl32")

$SetupObj = Join-Path $ObjDir "setup_main.o"
$UninstObj = Join-Path $ObjDir "uninstall_main.o"
$UpdaterObj = Join-Path $ObjDir "updater_main.o"
$OtaObj = Join-Path $ObjDir "ota_updater_pkg.o"

$SetupExe = Join-Path $DistDir "CoatroomSetup.exe"
$UninstExe = Join-Path $DistDir "CoatroomUninstall.exe"
$UpdaterExe = Join-Path $DistDir "CoatroomUpdater.exe"

& $Zig c++ @CFlags -c (Join-Path $Root "installer\setup_main.cpp") -o $SetupObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling setup_main.cpp" }
& $Zig c++ -std=c++20 -O2 "-Wl,--subsystem,windows" $SetupObj $RcObj -o $SetupExe @WinLibs
if ($LASTEXITCODE -ne 0) { throw "Failed linking CoatroomSetup.exe" }

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

Write-Host "[4/6] Staging loose distribution files in dist/..."
$DistAppExe = Join-Path $DistDir "Coatroom.exe"
Copy-Item -Force $AppExe $DistAppExe
Copy-Item -Force (Join-Path $Root "LICENSE_AND_TOU.txt") (Join-Path $DistDir "LICENSE_AND_TOU.txt")
Copy-Item -Force (Join-Path $Root "resources\coatroom.ico") (Join-Path $DistDir "coatroom.ico")

Write-Host "[5/6] Creating Coatroom-Portable-x64.zip..."
$PortableStage = Join-Path $DistDir "_portable_stage"
$PortableZip = Join-Path $DistDir "Coatroom-Portable-x64.zip"
if (Test-Path $PortableStage) { Remove-Item -Recurse -Force $PortableStage }
if (Test-Path $PortableZip) { Remove-Item -Force $PortableZip }
New-Item -ItemType Directory -Force -Path $PortableStage | Out-Null
Copy-Item -Force $DistAppExe (Join-Path $PortableStage "Coatroom.exe")
Copy-Item -Force $UpdaterExe (Join-Path $PortableStage "CoatroomUpdater.exe")
Copy-Item -Force (Join-Path $Root "LICENSE_AND_TOU.txt") (Join-Path $PortableStage "LICENSE_AND_TOU.txt")
Copy-Item -Force (Join-Path $Root "resources\coatroom.ico") (Join-Path $PortableStage "coatroom.ico")
Compress-Archive -Path (Join-Path $PortableStage "*") -DestinationPath $PortableZip -CompressionLevel Optimal
Remove-Item -Recurse -Force $PortableStage

Write-Host "[6/6] Generating dist/release-manifest.json..."
$SetupSha = (Get-FileHash -Algorithm SHA256 $SetupExe).Hash.ToLowerInvariant()
$ExeSha = (Get-FileHash -Algorithm SHA256 $DistAppExe).Hash.ToLowerInvariant()
$CleanVersion = $Version.TrimStart('v', 'V')
$ManifestObj = [ordered]@{
    version       = $CleanVersion
    download_url  = "https://github.com/$Owner/$Repo/releases/download/v$CleanVersion/CoatroomSetup.exe"
    sha256        = $SetupSha
    exe_url       = "https://github.com/$Owner/$Repo/releases/download/v$CleanVersion/Coatroom.exe"
    exe_sha256    = $ExeSha
    release_notes = $ReleaseNotes
    published_at  = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
}
$ManifestJson = $ManifestObj | ConvertTo-Json -Depth 4
$ManifestPath = Join-Path $DistDir "release-manifest.json"
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Host ""
Write-Host "[OK] Packaged Coatroom v$CleanVersion in $DistDir"
Write-Host "     CoatroomSetup.exe SHA-256: $SetupSha"
Write-Host "     Coatroom.exe      SHA-256: $ExeSha"
Write-Host "     Manifest: $ManifestPath"
