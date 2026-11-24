#!/usr/bin/env bash
# Builds a portable Linux folder and packs it into a tar.gz.
#
# The launcher needs the server next to its binary to start local worlds and to
# patch game files, so run scripts/build-server.sh first (or point
# OPENKOGAMA_SERVER_DIR at an already published server).
#
# Usage: scripts/package-linux.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-linux}"
dist="${DIST_DIR:-$root/dist}"
version="${VERSION:-$(git -C "$root" describe --tags --always 2>/dev/null || echo dev)}"

cmake -S "$root" -B "$build" -G "${GENERATOR:-Ninja}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENKOGAMA_VERSION="$version" \
    -DCMAKE_INSTALL_BINDIR=.
cmake --build "$build" --parallel "$(nproc)"

rm -rf "$dist"
cmake --install "$build" --prefix "$dist"

server="${OPENKOGAMA_SERVER_DIR:-$build/server}"
if [ -d "$server" ]; then
    rm -rf "$dist/server"
    cp -a "$server" "$dist/server"
else
    echo "warning: no server found in $server; local worlds and file patches will not work" >&2
fi

player="${OPENKOGAMA_PLAYER_DIR:-$build/player}"
if [ -d "$player" ]; then
    rm -rf "$dist/player"
    cp -a "$player" "$dist/player"
else
    echo "note: no player found in $player; versions before 3.0 need it" >&2
fi

archive="$root/OpenKogamaLauncher-linux-x86_64-$version.tar.gz"

cat > "$dist/openkogama-launcher.sh" <<'EOF'
#!/usr/bin/env bash
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
if [ -d "$HERE/lib" ]; then
    export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
if [ -d "$HERE/plugins" ]; then
    export QT_PLUGIN_PATH="$HERE/plugins"
fi
exec "$HERE/openkogama-launcher" "$@"
EOF
chmod +x "$dist/openkogama-launcher.sh"

if [ -f "$root/scripts/uninstall.sh" ]; then
    cp -a "$root/scripts/uninstall.sh" "$dist/uninstall.sh"
    chmod +x "$dist/uninstall.sh"
fi

tar -czf "$archive" -C "$dist" .
echo "Packed $archive"
