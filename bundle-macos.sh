#!/bin/sh
# Bundle Timp as a self-contained macOS app — produces dist/macos-arm64/Timp.app
# (build/ keeps the intermediates; dist/ mirrors the Windows installer layout).
#   - builds the binary (build-macos.sh)
#   - renders the procedural icon into a full .icns (16 → 1024 px)
#   - writes Info.plist with audio-type associations (Finder "Open With"
#     delivery is handled in-app via the Apple Event → single-instance route)
#   - copies the Homebrew dylib closure into Contents/Frameworks and rewrites
#     install names, so the app runs on Macs without Homebrew
#   - ad-hoc codesigns everything (distribution to strangers additionally
#     needs a Developer ID + notarization)
set -e
cd "$(dirname "$0")"

VERSION=0.12.2
sh build-macos.sh

APP=dist/macos-arm64/Timp.app
FW="$APP/Contents/Frameworks"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$FW"
cp build/timp "$APP/Contents/MacOS/timp"

# ---- icon: procedural renderer → PAM → PNG (ffmpeg) → icns (iconutil) ----
echo 'building icns'
cc -O2 -Isrc tools/makeicns_frames.c -o build/makeicns_frames -lm
ICONSET=build/timp.iconset
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
render() {  # $1 = px, $2 = iconset file name
    build/makeicns_frames "$1" "$ICONSET/tmp.pam"
    ffmpeg -y -loglevel error -i "$ICONSET/tmp.pam" "$ICONSET/$2"
}
render 16   icon_16x16.png
render 32   icon_16x16@2x.png
render 32   icon_32x32.png
render 64   icon_32x32@2x.png
render 128  icon_128x128.png
render 256  icon_128x128@2x.png
render 256  icon_256x256.png
render 512  icon_256x256@2x.png
render 512  icon_512x512.png
render 1024 icon_512x512@2x.png
rm -f "$ICONSET/tmp.pam"
iconutil -c icns -o "$APP/Contents/Resources/timp.icns" "$ICONSET"

# ---- Info.plist ----
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>       <string>timp</string>
    <key>CFBundleIdentifier</key>       <string>io.timp.timp</string>
    <key>CFBundleName</key>             <string>Timp</string>
    <key>CFBundleDisplayName</key>      <string>Timp</string>
    <key>CFBundleShortVersionString</key> <string>$VERSION</string>
    <key>CFBundleVersion</key>          <string>$VERSION</string>
    <key>CFBundlePackageType</key>      <string>APPL</string>
    <key>CFBundleIconFile</key>         <string>timp</string>
    <key>LSMinimumSystemVersion</key>   <string>11.0</string>
    <key>NSHighResolutionCapable</key>  <true/>
    <key>LSApplicationCategoryType</key> <string>public.app-category.music</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>      <string>Audio File</string>
            <key>CFBundleTypeRole</key>      <string>Viewer</string>
            <key>LSHandlerRank</key>         <string>Alternate</string>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>mp3</string> <string>flac</string> <string>ogg</string>
                <string>wav</string> <string>m4a</string> <string>opus</string>
            </array>
        </dict>
    </array>
</dict>
</plist>
PLIST

# ---- self-contained: copy the Homebrew dylib closure, rewrite install names ----
# Deps come in two shapes: absolute /opt/homebrew paths, and @rpath/@loader_path
# names that Homebrew dylibs use for their siblings (resolved via LC_RPATH,
# which breaks once the file moves into the bundle) — those are resolved
# against the referring dylib's ORIGINAL directory, so the walk carries it.
echo 'bundling dylibs'
bundle_deps() {  # $1 = Mach-O file to fix   $2 = its original source directory
    otool -L "$1" | awk 'NR>1 {print $1}' |
    grep -E '^/opt/homebrew|^/usr/local/(opt|Cellar|lib)|^@rpath/|^@loader_path/' |
    while read -r dep; do
        base=$(basename "$dep")
        case "$dep" in
            @rpath/*|@loader_path/*)
                src="$2/$base"
                [ -f "$src" ] || src="$2/../lib/$base"
                [ -f "$src" ] || { echo "warning: cannot resolve $dep (from $1)"; continue; } ;;
            *) src="$dep" ;;
        esac
        if [ ! -f "$FW/$base" ]; then
            cp -L "$src" "$FW/$base"
            chmod u+w "$FW/$base"
            install_name_tool -id "@executable_path/../Frameworks/$base" "$FW/$base" 2>/dev/null
            bundle_deps "$FW/$base" "$(cd "$(dirname "$src")" && pwd -P)"
        fi
        install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" "$1" 2>/dev/null
    done
}
bundle_deps "$APP/Contents/MacOS/timp" "$(pwd)"

# ---- ad-hoc sign (install_name_tool invalidated the linker signatures) ----
echo 'codesigning (ad-hoc)'
find "$FW" -name '*.dylib' -exec codesign --force -s - {} \; 2>/dev/null
codesign --force -s - "$APP/Contents/MacOS/timp" 2>/dev/null
codesign --force -s - "$APP" 2>/dev/null

echo "bundled $(pwd)/$APP"
otool -L "$APP/Contents/MacOS/timp" | sed -n '2,6p'
