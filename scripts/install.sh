#!/usr/bin/env bash
# OpenKogama Launcher automatic installer for Linux.
#
# Usage:
#   curl -sSL https://raw.githubusercontent.com/M3R710/openkogama-launcher/main/scripts/install.sh | bash
set -euo pipefail

REPO="M3R710/openkogama-launcher"
INSTALL_DIR="$HOME/.local/share/openkogama-launcher"
BIN_DIR="$HOME/.local/bin"
DESKTOP_DIR="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/192x192/apps"
PIXMAP_DIR="$HOME/.local/share/pixmaps"

echo "==> OpenKogama Launcher Linux Installer"

# 1. Architecture check
ARCH="$(uname -m)"
if [ "$ARCH" != "x86_64" ]; then
    echo "Error: OpenKogama Launcher currently supports x86_64 architecture (detected: $ARCH)." >&2
    exit 1
fi

# 2. Dependency checks
MISSING=()
for cmd in curl tar; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        MISSING+=("$cmd")
    fi
done
if [ ${#MISSING[@]} -ne 0 ]; then
    echo "Error: Required tools are missing: ${MISSING[*]}" >&2
    echo "Please install them using your package manager." >&2
    exit 1
fi

# 3. Find latest release download URL
echo "==> Fetching latest release info from GitHub..."
RELEASE_JSON="$(curl -sSL "https://api.github.com/repos/$REPO/releases/latest")"
DOWNLOAD_URL="$(echo "$RELEASE_JSON" | grep -o 'https://[^"]*OpenKogamaLauncher-linux-x86_64-[^"]*\.tar\.gz' | head -n 1 || true)"

if [ -z "$DOWNLOAD_URL" ]; then
    # Fallback to direct download URL
    DOWNLOAD_URL="https://github.com/$REPO/releases/latest/download/OpenKogamaLauncher-linux-x86_64-0.1.0.tar.gz"
fi

echo "==> Downloading from: $DOWNLOAD_URL"
TMP_DIR="$(mktemp -d /tmp/openkogama-install.XXXXXX)"
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

curl -# -L -f --retry 3 -o "$TMP_DIR/launcher.tar.gz" "$DOWNLOAD_URL"

# 4. Extract into ~/.local/share/openkogama-launcher
echo "==> Installing to $INSTALL_DIR..."
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
tar -xzf "$TMP_DIR/launcher.tar.gz" -C "$INSTALL_DIR"

# Ensure binaries and scripts are executable
chmod +x "$INSTALL_DIR/openkogama-launcher" 2>/dev/null || true
chmod +x "$INSTALL_DIR/openkogama-launcher.sh" 2>/dev/null || true
if [ -d "$INSTALL_DIR/server" ]; then
    chmod +x "$INSTALL_DIR/server/openkogama-server" 2>/dev/null || true
fi

# 5. Copy uninstall script
if [ -f "$INSTALL_DIR/scripts/uninstall.sh" ]; then
    cp "$INSTALL_DIR/scripts/uninstall.sh" "$INSTALL_DIR/uninstall.sh"
    chmod +x "$INSTALL_DIR/uninstall.sh"
fi

# 6. Set up command line symlinks in ~/.local/bin
mkdir -p "$BIN_DIR"
ln -sf "$INSTALL_DIR/openkogama-launcher.sh" "$BIN_DIR/openkogama-launcher"
if [ -f "$INSTALL_DIR/uninstall.sh" ]; then
    ln -sf "$INSTALL_DIR/uninstall.sh" "$BIN_DIR/openkogama-launcher-uninstall"
fi

# 7. Desktop entry & icons
mkdir -p "$DESKTOP_DIR" "$ICON_DIR" "$PIXMAP_DIR"

if [ -f "$INSTALL_DIR/share/icons/hicolor/192x192/apps/openkogama-launcher.png" ]; then
    cp "$INSTALL_DIR/share/icons/hicolor/192x192/apps/openkogama-launcher.png" "$ICON_DIR/openkogama-launcher.png"
    cp "$INSTALL_DIR/share/icons/hicolor/192x192/apps/openkogama-launcher.png" "$PIXMAP_DIR/openkogama-launcher.png"
fi

cat > "$DESKTOP_DIR/openkogama-launcher.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=OpenKogama Launcher
Comment=Play classic KoGaMa versions, join servers and manage your worlds
Exec="$INSTALL_DIR/openkogama-launcher.sh" %u
Icon=openkogama-launcher
Terminal=false
Categories=Game;
MimeType=x-scheme-handler/openkogama;
StartupWMClass=openkogama-launcher
EOF
chmod +x "$DESKTOP_DIR/openkogama-launcher.desktop"

# 8. Register URL handler and update desktop database
if command -v xdg-mime >/dev/null 2>&1; then
    xdg-mime default openkogama-launcher.desktop x-scheme-handler/openkogama 2>/dev/null || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
fi

echo "==> Installation complete!"
echo ""
echo "You can now run OpenKogama Launcher:"
echo "  1. From your application menu ('OpenKogama Launcher')"
echo "  2. From terminal: openkogama-launcher"
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo "     (Note: Add $BIN_DIR to your PATH if 'openkogama-launcher' command is not found)"
fi
echo ""
echo "To uninstall later, run:"
echo "  openkogama-launcher-uninstall"
echo "  (or curl -sSL https://raw.githubusercontent.com/$REPO/main/scripts/uninstall.sh | bash)"
echo ""

# 9. Wine check
if ! command -v wine >/dev/null 2>&1; then
    echo "Notice: Wine was not detected on your system."
    echo "The historical KoGaMa client games require Wine to run. Please install Wine:"
    echo "  Ubuntu/Debian: sudo apt install wine"
    echo "  Fedora:        sudo dnf install wine"
    echo "  Arch Linux:    sudo pacman -S wine"
    echo ""
fi
