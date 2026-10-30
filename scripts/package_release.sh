#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# Portable Linux Groovebox Framework — SD Card Release Packager (macOS / Linux)
# Builds RK3326 (Cortex-A35) and A133P (Cortex-A53) binaries and assembles a
# ready-to-copy SD card Ports bundle in dist/Ports/ + dist/groovebox-handheld-ports.zip
# ==============================================================================

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

export COPYFILE_DISABLE=1

echo "==> [1/4] Cross-compiling AArch64 binary for Rockchip RK3326 (MagicX XU10 / R36S)..."
cmake --preset rk3326
cmake --build --preset rk3326 --target xu10_groovebox -j4

echo "==> [2/4] Cross-compiling AArch64 binary for Allwinner A133P (Trimui Brick / Brick Pro)..."
cmake --preset a133p
cmake --build --preset a133p --target xu10_groovebox -j4

DIST_DIR="$ROOT_DIR/dist"
PORTS_DIR="$DIST_DIR/Ports"
APP_DIR="$PORTS_DIR/groovebox"

echo "==> [3/4] Assembling portable SD card bundle in $PORTS_DIR ..."
rm -rf "$DIST_DIR"
mkdir -p "$APP_DIR/profiles" \
         "$APP_DIR/oneshots" \
         "$APP_DIR/loops" \
         "$APP_DIR/dx7" \
         "$APP_DIR/projects" \
         "$APP_DIR/recordings"

# Copy binaries
cp "$ROOT_DIR/build-xu10/xu10_groovebox"  "$APP_DIR/xu10_groovebox_rk3326"
cp "$ROOT_DIR/build-a133p/xu10_groovebox" "$APP_DIR/xu10_groovebox_a133p"
cp "$ROOT_DIR/build-xu10/xu10_groovebox"  "$APP_DIR/xu10_groovebox"
chmod +x "$APP_DIR/xu10_groovebox_rk3326" "$APP_DIR/xu10_groovebox_a133p" "$APP_DIR/xu10_groovebox"

# Copy hardware profiles
cp "$ROOT_DIR"/profiles/*.ini "$APP_DIR/profiles/"

# Copy factory samples & DX7 cartridges
if [ -d "$ROOT_DIR/res/samples/drums" ]; then
    cp -R "$ROOT_DIR/res/samples/drums/"* "$APP_DIR/oneshots/" 2>/dev/null || true
fi
if [ -d "$ROOT_DIR/res/samples/loops" ]; then
    cp -R "$ROOT_DIR/res/samples/loops/"* "$APP_DIR/loops/" 2>/dev/null || true
fi
if [ -d "$ROOT_DIR/res/dx7" ]; then
    cp -R "$ROOT_DIR/res/dx7/"*.syx "$APP_DIR/dx7/" 2>/dev/null || true
fi

# Generate universal launcher script (ROCKNIX / ArkOS / CrossMix / NextUI / PortMaster)
cat > "$PORTS_DIR/Groovebox.sh" << 'EOF'
#!/bin/sh
# Universal Handheld Launcher for Portable Linux Groovebox Framework
# Supports ROCKNIX, ArkOS, AmberELEC, CrossMix-OS, NextUI, MinUI

PORT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
APP_DIR="$PORT_DIR/groovebox"
cd "$APP_DIR" || exit 1

export GROOVEBOX_DATA_DIR="$APP_DIR"

# Detect SoC / Device Model to pick optimal CPU binary
MODEL=""
if [ -r /proc/device-tree/model ]; then
    MODEL="$(tr -d '\0' < /proc/device-tree/model)"
elif [ -r /sys/firmware/devicetree/base/model ]; then
    MODEL="$(tr -d '\0' < /sys/firmware/devicetree/base/model)"
fi

BIN="$APP_DIR/xu10_groovebox_rk3326"
case "$MODEL" in
    *Trimui*|*TRIMUI*|*Brick*|*Smart\ Pro*|*sun50iw10*|*A133*)
        if [ -x "$APP_DIR/xu10_groovebox_a133p" ]; then
            BIN="$APP_DIR/xu10_groovebox_a133p"
        fi
        ;;
esac

# Prefer Direct KMSDRM when no desktop display server is active
if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
fi

exec "$BIN" "$@"
EOF
chmod +x "$PORTS_DIR/Groovebox.sh"

# Clean any macOS metadata files
find "$DIST_DIR" -name '.DS_Store' -o -name '._*' -delete 2>/dev/null || true

echo "==> [4/4] Creating zip archive $DIST_DIR/groovebox-handheld-ports.zip ..."
if command -v zip >/dev/null 2>&1; then
    (cd "$DIST_DIR" && zip -rq "groovebox-handheld-ports.zip" "Ports" -x "*.DS_Store" -x "*/._*")
fi

echo "==> Done! Ready-to-copy SD card bundle:"
echo "    Folder:  $PORTS_DIR/"
echo "    Archive: $DIST_DIR/groovebox-handheld-ports.zip"
