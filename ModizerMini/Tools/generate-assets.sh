#!/usr/bin/env bash
#
# Regenerates the app icon, the document icon and the asset catalogue entries.
# Run from anywhere:  ModizerMini/Tools/generate-assets.sh
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="$HERE/Sources/App/Assets.xcassets"
RESOURCES="$HERE/Sources/App/Resources"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "==> drawing icons"
swift "$HERE/Tools/make-icons.swift" "$STAGE"

echo "==> app icon set"
APPICON="$ASSETS/AppIcon.appiconset"
mkdir -p "$APPICON"
cp "$STAGE/icon_16.png"   "$APPICON/icon_16x16.png"
cp "$STAGE/icon_32.png"   "$APPICON/icon_16x16@2x.png"
cp "$STAGE/icon_32.png"   "$APPICON/icon_32x32.png"
cp "$STAGE/icon_64.png"   "$APPICON/icon_32x32@2x.png"
cp "$STAGE/icon_128.png"  "$APPICON/icon_128x128.png"
cp "$STAGE/icon_256.png"  "$APPICON/icon_128x128@2x.png"
cp "$STAGE/icon_256.png"  "$APPICON/icon_256x256.png"
cp "$STAGE/icon_512.png"  "$APPICON/icon_256x256@2x.png"
cp "$STAGE/icon_512.png"  "$APPICON/icon_512x512.png"
cp "$STAGE/icon_1024.png" "$APPICON/icon_512x512@2x.png"

echo "==> document icon"
mkdir -p "$RESOURCES"
ICONSET="$STAGE/ChipDoc.iconset"
mkdir -p "$ICONSET"
cp "$STAGE/doc_16.png"   "$ICONSET/icon_16x16.png"
cp "$STAGE/doc_32.png"   "$ICONSET/icon_16x16@2x.png"
cp "$STAGE/doc_32.png"   "$ICONSET/icon_32x32.png"
cp "$STAGE/doc_64.png"   "$ICONSET/icon_32x32@2x.png"
cp "$STAGE/doc_128.png"  "$ICONSET/icon_128x128.png"
cp "$STAGE/doc_256.png"  "$ICONSET/icon_128x128@2x.png"
cp "$STAGE/doc_256.png"  "$ICONSET/icon_256x256.png"
cp "$STAGE/doc_512.png"  "$ICONSET/icon_256x256@2x.png"
cp "$STAGE/doc_512.png"  "$ICONSET/icon_512x512.png"
cp "$STAGE/doc_1024.png" "$ICONSET/icon_512x512@2x.png"
iconutil -c icns "$ICONSET" -o "$RESOURCES/ChipDoc.icns"

echo "==> done"
