# installer.ps1 - build a Windows Setup.exe with Forge.
# Runs build.ps1, stages a clean single-exe payload into dist\win-x64, then runs
# Forge (expected at ..\Forge) against forge.toml to emit
# dist\installer\Timp-Setup-<version>.exe and verifies it is a GUI binary.
# Pass -SkipBuild to package an existing build\ without recompiling.
param(
  [string]$ForgeDir = "..\Forge",
  [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$forgeRoot   = Resolve-Path $ForgeDir
$forgeGui    = Join-Path $forgeRoot "build\forge.exe"
$forgeSrc    = Join-Path $forgeRoot "cmd\forge"
$uninstaller = Join-Path $forgeRoot "build\uninstall.exe"
$buildDir    = Join-Path $PSScriptRoot "build"
$stageDir    = Join-Path $PSScriptRoot "dist\win-x64"
$outDir      = Join-Path $PSScriptRoot "dist\installer"

if (-not $SkipBuild) {
    Write-Host "[1/4] Building Timp..." -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot "build.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[1/4] Skipping Timp rebuild (-SkipBuild)" -ForegroundColor DarkGray
}

Write-Host "`n[2/4] Staging clean payload in $stageDir..." -ForegroundColor Cyan
# build/ is dirty (.o files, a local config.ini) — copy only the shippable
# artifacts into a fresh dist\win-x64 that forge.toml's [[dirs]] points at.
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

# The payload is timp.exe plus the MinGW runtime DLLs build.ps1 bundled
# (libraylib.dll, glfw3.dll, libwinpthread-1.dll) — raylib links glfw as a DLL,
# so the exe is not standalone and these must ship with it.
$src = Join-Path $buildDir "timp.exe"
if (-not (Test-Path $src)) { throw "Missing timp.exe in $buildDir - run build.ps1 first." }
Copy-Item $src $stageDir -Force
Write-Host "  staged timp.exe" -ForegroundColor DarkGray

$dlls = @(Get-ChildItem (Join-Path $buildDir "*.dll") -ErrorAction SilentlyContinue)
if ($dlls.Count -eq 0) {
    throw "No runtime DLLs found in $buildDir. build.ps1 should have bundled libraylib.dll/glfw3.dll/libwinpthread-1.dll - rebuild with build.ps1 (not -SkipBuild on a stale build dir)."
}
foreach ($d in $dlls) {
    Copy-Item $d.FullName $stageDir -Force
    Write-Host "  staged $($d.Name)" -ForegroundColor DarkGray
}

Write-Host "`n[3/4] Ensuring GUI-subsystem forge.exe..." -ForegroundColor Cyan
if (-not (Test-Path $uninstaller)) {
    throw "Missing $uninstaller. Run 'gobake build' in $forgeRoot first."
}
$needBuild = -not (Test-Path $forgeGui)
if (-not $needBuild) {
    $srcLatest = (Get-ChildItem $forgeSrc -Recurse -Filter *.go |
                  Sort-Object LastWriteTime -Descending |
                  Select-Object -First 1).LastWriteTime
    if ((Get-Item $forgeGui).LastWriteTime -lt $srcLatest) { $needBuild = $true }
}
if ($needBuild) {
    Write-Host "  building $forgeGui..." -ForegroundColor DarkGray
    Push-Location $forgeRoot
    try {
        go build -tags "desktop,production" -ldflags "-H windowsgui -X main.Version=local-gui" -o build\forge.exe ./cmd/forge/
        if ($LASTEXITCODE -ne 0) { throw "go build forge.exe failed (exit $LASTEXITCODE)" }
    } finally { Pop-Location }
} else {
    Write-Host "  up to date: $forgeGui" -ForegroundColor DarkGray
}

Write-Host "`n[4/4] Building Setup.exe..." -ForegroundColor Cyan

# forge.exe is a GUI-subsystem binary, so $LASTEXITCODE is not propagated
# through PowerShell's call operator. Use Start-Process -Wait -PassThru instead.
function Invoke-Forge {
    param([string[]]$ForgeArgs)
    $p = Start-Process -FilePath $forgeGui -ArgumentList $ForgeArgs -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) { throw "forge $($ForgeArgs -join ' ') failed (exit $($p.ExitCode))" }
}

Invoke-Forge @("validate", "forge.toml")
Invoke-Forge @("build", "--out", $outDir)

$setup = Get-ChildItem $outDir -Filter "Timp-Setup-*.exe" |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $setup) { throw "Setup.exe not found in $outDir after build." }

# Verify PE subsystem is GUI (2) — guards against a console-window regression.
$bytes    = [System.IO.File]::ReadAllBytes($setup.FullName)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
$subsys   = [BitConverter]::ToUInt16($bytes, $peOffset + 24 + 68)
$subName  = switch ($subsys) { 2 { "GUI" } 3 { "CONSOLE" } default { "OTHER($subsys)" } }

Write-Host ""
Write-Host "Done. Output: $($setup.FullName)" -ForegroundColor Green
Write-Host ("  size:      {0} MB" -f [math]::Round($setup.Length/1MB,1))
Write-Host ("  subsystem: {0}" -f $subName)
if ($subsys -ne 2) {
    Write-Warning "Subsystem is not GUI - a console window will appear when users run Setup.exe."
}
