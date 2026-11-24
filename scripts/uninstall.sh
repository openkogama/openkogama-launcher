#!/usr/bin/env bash
# Uninstalls OpenKogama Launcher from the user's home directory.
#
# Usage:
#   curl -sSL https://raw.githubusercontent.com/M3R710/openkogama-launcher/main/scripts/uninstall.sh | bash
#
# Flags:
#   --purge   Also delete game data (worlds, avatars, and downloaded versions).
set -euo pipefail

PURGE=false
for arg in "$@"; do
    case "$arg" in
        --purge|-p) PURGE=true ;;
        --help|-h)
            echo "Usage: $0 [--purge]"
            echo "  --purge: remove all user game data (~/.local/share/OpenKogama)"
            exit 0
            ;;
    esac
done

echo "==> Uninstalling OpenKogama Launcher..."

INSTALL_DIR="$HOME/.local/share/openkogama-launcher"
BIN_FILE="$HOME/.local/bin/openkogama-launcher"
UNINSTALL_BIN="$HOME/.local/bin/openkogama-launcher-uninstall"
DESKTOP_FILE="$HOME/.local/share/applications/openkogama-launcher.desktop"
ICON_FILE="$HOME/.local/share/icons/hicolor/192x192/apps/openkogama-launcher.png"
PIXMAP_FILE="$HOME/.local/share/pixmaps/openkogama-launcher.png"

rm -rf "$INSTALL_DIR"
rm -f "$BIN_FILE" "$UNINSTALL_BIN" "$DESKTOP_FILE" "$ICON_FILE" "$PIXMAP_FILE"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
fi

if [ "$PURGE" = true ]; then
    echo "==> Purging user game data..."
    rm -rf "$HOME/.local/share/OpenKogama"
else
    echo "Note: Your worlds, avatars and game instances in ~/.local/share/OpenKogama were kept."
    echo "To remove them, re-run with --purge."
fi

echo "==> OpenKogama Launcher uninstalled successfully."
