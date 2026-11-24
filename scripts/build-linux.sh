#!/usr/bin/env bash
# Builds the OpenKogama Launcher on Linux.
#
# Requirements: CMake 3.19+, Ninja or Make, a C++20 compiler and the Qt 6.5+
# development packages (Qt6 Core, Widgets, Network, Concurrent, LinguistTools,
# plus the private headers of Qt Core).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-linux}"
version="${VERSION:-$(git -C "$root" describe --tags --always 2>/dev/null || echo dev)}"

cmake -S "$root" -B "$build" -G "${GENERATOR:-Ninja}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENKOGAMA_VERSION="$version" \
    -DBUILD_TESTING=ON

cmake --build "$build" --parallel "$(nproc)"

QT_QPA_PLATFORM=offscreen ctest --test-dir "$build" --output-on-failure

echo "Launcher built in $build"
