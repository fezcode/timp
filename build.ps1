# Don't use 'Stop' here — gcc writes warnings to stderr which PowerShell would treat as terminating errors.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

if (-not (Test-Path "$root\vendor\miniaudio.h") -or -not (Test-Path "$root\vendor\stb_image.h")) {
    Write-Output 'fetching deps'
    & "$root\setup.ps1"
}

# Resolve gcc and SDL2 via MSYS2 if available
$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
$pkg = (Get-Command pkg-config -ErrorAction SilentlyContinue).Source
if (-not $gcc) { throw "gcc not found on PATH (install MSYS2 mingw64: 'pacman -S mingw-w64-x86_64-gcc')" }
if (-not $pkg) { throw "pkg-config not found on PATH (install: 'pacman -S mingw-w64-x86_64-pkgconf')" }

$sdlCflags = (& cmd /c "pkg-config --cflags sdl2") -split '\s+' | Where-Object { $_ }
$sdlLibs   = (& cmd /c "pkg-config --libs sdl2")   -split '\s+' | Where-Object { $_ }
if (-not $sdlLibs) { throw "pkg-config returned no SDL2 libs (install: 'pacman -S mingw-w64-x86_64-SDL2')" }

$flags = @('-O2','-Wall','-Wextra','-Wno-unused-parameter','-std=c11','-DICON_WITH_SDL','-Ivendor','-Isrc') + $sdlCflags
$winLibs = '-lole32','-lwinmm','-lm'

New-Item -ItemType Directory -Force -Path build | Out-Null

$srcs = 'main','audio','skin','ui','ini','font','playlist','filebrowser','fft','eq','theme','settings','config','icon','vendor'

# Newest-header timestamp — any .h change invalidates every .o.
# Without this, edits to (e.g.) skin.h leave stale .o files compiled against
# the old struct layout, which produces silent memory corruption at runtime.
$newestHeader = (Get-ChildItem src\*.h | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
foreach ($s in $srcs) {
    $src = "src\$s.c"
    $obj = "build\$s.o"
    $upToDate = (Test-Path $obj) -and
                (Get-Item $obj).LastWriteTime -gt (Get-Item $src).LastWriteTime -and
                (Get-Item $obj).LastWriteTime -gt $newestHeader
    if ($upToDate) {
        Write-Output "up-to-date $s"
        continue
    }
    Write-Output "compiling $s"
    & $gcc @flags -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $s" }
}

Write-Output 'linking timp.exe'
$objs = $srcs | ForEach-Object { "build\$_.o" }
& $gcc @objs -o build\timp.exe @sdlLibs @winLibs
if ($LASTEXITCODE -ne 0) { throw 'link failed' }

# Copy SDL2.dll alongside for portable distribution
$gccDir = Split-Path $gcc -Parent
$sdlDll = Join-Path $gccDir 'SDL2.dll'
if (Test-Path $sdlDll) { Copy-Item $sdlDll build\ -Force }

# Stage skins next to the binary so it works when launched from File Explorer
if (Test-Path skins) {
    $dst = 'build\skins'
    if (-not (Test-Path $dst)) { New-Item -ItemType Directory -Force -Path $dst | Out-Null }
    Copy-Item skins\* $dst -Recurse -Force
}

Write-Output ('built ' + (Resolve-Path build\timp.exe))
