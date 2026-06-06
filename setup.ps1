# setup.ps1 - fetch the vendored single-header dependencies into vendor/.
# Downloads miniaudio.h (audio decode/playback) and stb_image.h (cover-art
# decoding) when missing. build.ps1 calls this automatically on the first
# build, so you rarely need to run it by hand.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vendor = Join-Path $root 'vendor'
New-Item -ItemType Directory -Force -Path $vendor | Out-Null

$files = @{
    'miniaudio.h' = 'https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h'
    'stb_image.h' = 'https://raw.githubusercontent.com/nothings/stb/master/stb_image.h'
}

foreach ($name in $files.Keys) {
    $dest = Join-Path $vendor $name
    if (Test-Path $dest) { Write-Output "have $name"; continue }
    Write-Output "fetching $name"
    Invoke-WebRequest -UseBasicParsing -Uri $files[$name] -OutFile $dest
}
Write-Output 'deps ready'
