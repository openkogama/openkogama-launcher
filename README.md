<picture>
  <source media="(prefers-color-scheme: dark)" srcset=".github/logo-dark.png">
  <img src=".github/logo-light.png" alt="OpenKogama Launcher" width="400">
</picture>

# OpenKogama Launcher (Linux)

Native Linux port of the launcher for OpenKogama: play classic KoGaMa versions, join servers, and manage your worlds and avatars on Linux.

The Worlds and Avatars tabs manage your local game data. Add community servers in the Servers tab and click Join to launch the selected KoGaMa version directly.

## Features on Linux

- **Native Qt 6 Interface**: Wayland and X11 support with automatic system dark/light mode detection.
- **Native Linux Server**: `openkogama-server` runs natively (.NET 10 linux-x64) for local hosting, asset downloads, and SQLite database management.
- **Seamless Game Compatibility**: Automatically detects and launches Windows KoGaMa client builds through Wine (`wine` / `wine64`).
- **Desktop & Browser Integration**: Automatically registers the `openkogama://` URL protocol with XDG, enabling one-click join from Firefox, Chrome, etc.
- **Discord Rich Presence**: Native Unix domain socket IPC integration with Linux Discord clients.
- **Secure Account Storage**: Stores authentication tokens in your user data directory with strict file permissions (`0600`).

## Install

Download `OpenKogamaLauncher-<version>-x86_64.AppImage` or the portable `.tar.gz` from the [Releases](https://github.com/M3R710/openkogama-launcher/releases) page.

### AppImage

```bash
chmod +x OpenKogamaLauncher-*.AppImage
./OpenKogamaLauncher-*.AppImage
```

### Portable tar.gz

```bash
tar -xzf OpenKogamaLauncher-linux-x86_64-*.tar.gz
cd OpenKogamaLauncher-linux-x86_64-*/
./openkogama-launcher.sh
```

### Prerequisites

The launcher and local server run natively on Linux. Since historical KoGaMa client versions were built for Windows, Wine is used to run the game instances:

```bash
# Ubuntu / Debian
sudo apt install wine

# Fedora
sudo dnf install wine

# Arch Linux
sudo pacman -S wine
```

### Data Paths

All user data is stored according to XDG standards in `~/.local/share/OpenKogama/OpenKogama Launcher`:
- `instances/`: installed KoGaMa clients.
- `server/`: native server files, local worlds, avatars, and assets.
- `profile/`: local player settings, models, and account tokens.

## Building from Source

### Requirements
- CMake 3.19+
- A C++20 compiler (`g++` or `clang++`)
- Qt 6.5+ development libraries (`qt6-base-dev`, `qt6-base-private-dev`, `qt6-tools-dev`, `qt6-l10n-tools`)
- .NET 10 SDK (for building `openkogama-server`)

### Build
```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-linux
QT_QPA_PLATFORM=offscreen ctest --test-dir build-linux --output-on-failure
```

Helper scripts are available in `scripts/`:
- `scripts/build-linux.sh`: builds the launcher and runs test suites.
- `scripts/build-server.sh`: publishes the native `openkogama-server` for Linux x64 via .NET SDK.
- `scripts/package-linux.sh`: creates a standalone portable `.tar.gz`.
- `scripts/package-appimage.sh`: creates an `.AppImage` bundle.

## Translations

The launcher follows your system language, or the one chosen under Settings -> General.

## Credits

Based on the original [OpenKogama Launcher](https://github.com/openkogama/openkogama-launcher) made by nightus and the OpenKogama community.
Linux fork and native port maintained by [M3R710](https://github.com/M3R710).
