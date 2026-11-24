#!/usr/bin/env bash
# Publishes the OpenKogama server for Linux, so the launcher can start it.
#
# The server lives in its own repositories. Put openkogama-server,
# openkogama-photon and openkogama-mods next to each other and run this script,
# or point OPENKOGAMA_SOURCES at their parent folder.
#
# Usage: scripts/build-server.sh [output-dir]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/build-linux/server}"
sources="${OPENKOGAMA_SOURCES:-$(dirname "$root")}"

for repo in openkogama-server openkogama-photon openkogama-mods; do
    if [ ! -d "$sources/$repo" ]; then
        echo "Missing $sources/$repo" >&2
        echo "Clone openkogama-server, openkogama-photon and openkogama-mods next to this checkout." >&2
        exit 1
    fi
done

if ! command -v dotnet >/dev/null 2>&1; then
    echo "The .NET SDK 10 is required: https://dotnet.microsoft.com/download" >&2
    exit 1
fi

mkdir -p "$out"
dotnet publish "$sources/openkogama-server/openkogama-server.csproj" \
    -c Release -r linux-x64 --self-contained -o "$out"

# The mod loader and its scripts are Windows DLLs that the launcher copies into
# the game. They are optional: without them the launcher simply runs without
# mods. Build them with a Windows or mingw toolchain and drop them here.
for name in okmod.dll scripts.dll OkModShim.dll; do
    if [ -f "$sources/openkogama-mods/build/$name" ]; then
        cp "$sources/openkogama-mods/build/$name" "$out/"
    fi
done

echo "Server published to $out"
