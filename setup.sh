#!/usr/bin/env bash
set -e
root="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$root/vendor"
fetch() {
    local name="$1" url="$2" dest="$root/vendor/$name"
    [ -f "$dest" ] && { echo "have $name"; return; }
    echo "fetching $name"
    curl -fsSL "$url" -o "$dest"
}
fetch miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
fetch stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
echo 'deps ready'
