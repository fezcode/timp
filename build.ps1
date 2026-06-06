# Build Timp (raylib edition) — produces a standalone build\timp.exe.
# raylib is linked statically, so no DLLs are shipped alongside the binary.
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
$srcs = 'rl_main','audio','art','osdialog','tags','lyrics','rlconfig','mediakeys','singleinst','fft','eq','playlist','vendor_ma'

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
