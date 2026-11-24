#!/usr/bin/env bash
# Packs the installed Linux build into an AppImage with linuxdeploy.
#
# Usage: scripts/package-appimage.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-linux}"
appdir="${APPDIR:-$root/AppDir}"
tools="${TOOLS_DIR:-$root/build-tools}"
version="${VERSION:-$(git -C "$root" describe --tags --always 2>/dev/null || echo dev)}"

mkdir -p "$tools"
fetch() {
    local url="$1" name="$2"
    if [ ! -x "$tools/$name" ]; then
        curl -L --fail --retry 3 -o "$tools/$name" "$url"
        chmod +x "$tools/$name"
    fi
}
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage linuxdeploy
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy-plugin-qt
fetch https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage appimagetool

cmake -S "$root" -B "$build" -G "${GENERATOR:-Ninja}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENKOGAMA_VERSION="$version" \
    -DCMAKE_INSTALL_BINDIR=bin
cmake --build "$build" --parallel "$(nproc)"

rm -rf "$appdir"
cmake --install "$build" --prefix "$appdir/usr"

for folder in server player; do
    source="${OPENKOGAMA_${folder^^}_DIR:-$build/$folder}"
    if [ -d "$source" ]; then
        rm -rf "$appdir/usr/bin/$folder"
        cp -a "$source" "$appdir/usr/bin/$folder"
    fi
done

export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"

"$tools/linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/openkogama-launcher" \
    --desktop-file "$appdir/usr/share/applications/openkogama-launcher.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/192x192/apps/openkogama-launcher.png" \
    --plugin qt

output="$root/OpenKogamaLauncher-$version-x86_64.AppImage"
"$tools/appimagetool" "$appdir" "$output"
echo "Packed $output"
