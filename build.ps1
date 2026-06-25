# Build Timp (raylib edition) — produces build\timp.exe plus the MinGW runtime
# DLLs it loads. The MSYS2 raylib package links glfw as a DLL (and pulls in
# winpthread), so timp.exe is NOT a single static binary: it needs libraylib.dll,
# glfw3.dll and libwinpthread-1.dll beside it. This script auto-bundles whatever
# non-system DLLs the exe imports (transitively) into build\ so it runs on
# machines without MSYS2.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

# Fetch the vendored single-header deps on first build (miniaudio, stb_image).
if (-not (Test-Path "$root\vendor\miniaudio.h") -or -not (Test-Path "$root\vendor\stb_image.h")) {
    Write-Output 'fetching deps'
    & "$root\setup.ps1"
}

$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $gcc) { throw "gcc not found on PATH (install MSYS2 mingw64: 'pacman -S mingw-w64-x86_64-gcc')" }
$pkg = (Get-Command pkg-config -ErrorAction SilentlyContinue).Source
if (-not $pkg) { throw "pkg-config not found on PATH ('pacman -S mingw-w64-x86_64-pkgconf')" }

$cf = (& cmd /c "pkg-config --cflags raylib") -split '\s+' | Where-Object { $_ }
$lf = (& cmd /c "pkg-config --libs raylib")   -split '\s+' | Where-Object { $_ }
if (-not $lf) { throw "raylib not found via pkg-config (install: 'pacman -S mingw-w64-x86_64-raylib')" }

$flags   = @('-O2','-Wall','-Wextra','-Wno-unused-parameter','-std=c11','-Isrc') + $cf
# -mwindows → GUI subsystem, so launching timp.exe never opens a console window.
$winlibs = '-mwindows','-lopengl32','-lgdi32','-lwinmm','-lcomdlg32','-lole32','-luser32','-ldwmapi','-lwinhttp','-lshell32','-lm'

New-Item -ItemType Directory -Force -Path build | Out-Null
$srcs = 'rl_main','audio','art','osdialog','tags','lyrics','rlconfig','mediakeys','singleinst','fft','eq','playlist','playlistio','vendor_ma'

# Incremental: any .h change invalidates every .o (avoids stale-struct corruption).
$newestHeader = (Get-ChildItem src\*.h | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
$objs = @()
foreach ($s in $srcs) {
    $src = "src\$s.c"; $obj = "build\$s.o"
    $upToDate = (Test-Path $obj) -and
                ((Get-Item $obj).LastWriteTime -gt (Get-Item $src).LastWriteTime) -and
                ((Get-Item $obj).LastWriteTime -gt $newestHeader)
    if ($upToDate) { Write-Output "up-to-date $s"; $objs += $obj; continue }
    Write-Output "compiling $s"
    & $gcc @flags -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $s" }
    $objs += $obj
}

# Embed the Windows .exe icon (optional — needs windres + assets\timp.ico).
$rcObj = $null
$windres = (Get-Command windres -ErrorAction SilentlyContinue).Source
if ($windres -and (Test-Path 'src\app.rc')) {
    Write-Output 'compiling resource app.rc'
    & $windres 'src\app.rc' -o 'build\app_rc.o'
    if ($LASTEXITCODE -eq 0) { $rcObj = 'build\app_rc.o' }
}

Write-Output 'linking timp.exe'
$linkObjs = if ($rcObj) { $objs + $rcObj } else { $objs }
& $gcc @linkObjs -o build\timp.exe @lf @winlibs
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
Write-Output ('built ' + (Resolve-Path build\timp.exe))

# Bundle the MinGW runtime DLLs timp.exe loads (libraylib.dll, glfw3.dll,
# libwinpthread-1.dll, and anything they pull in). We walk the PE imports with
# objdump and copy only DLLs that live in the MinGW bin dir — Windows system DLLs
# (kernel32, user32, ...) resolve from System32 on every machine and are skipped.
$mingwBin = Split-Path $gcc -Parent
$objdump  = Join-Path $mingwBin 'objdump.exe'
if (-not (Test-Path $objdump)) { $objdump = (Get-Command objdump -ErrorAction SilentlyContinue).Source }

function Copy-RuntimeDlls {
    param([string]$Binary, [string]$DllDir, [string]$DestDir, [string]$Objdump, [hashtable]$Seen)
    $deps = & $Objdump -p $Binary 2>$null |
            Select-String -Pattern 'DLL Name:\s*(\S+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value }
    foreach ($dll in $deps) {
        $key = $dll.ToLowerInvariant()
        if ($Seen.ContainsKey($key)) { continue }
        $Seen[$key] = $true
        $candidate = Join-Path $DllDir $dll
        if (Test-Path $candidate) {
            Copy-Item $candidate $DestDir -Force
            Write-Output "bundling $dll"
            Copy-RuntimeDlls -Binary $candidate -DllDir $DllDir -DestDir $DestDir -Objdump $Objdump -Seen $Seen
        }
    }
}

if ($objdump) {
    Copy-RuntimeDlls -Binary 'build\timp.exe' -DllDir $mingwBin -DestDir 'build' -Objdump $objdump -Seen @{}
} else {
    Write-Warning 'objdump not found - cannot auto-bundle runtime DLLs; timp.exe will fail on machines without MSYS2.'
}
