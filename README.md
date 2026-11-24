<picture>
  <source media="(prefers-color-scheme: dark)" srcset=".github/logo-dark.png">
  <img src=".github/logo-light.png" alt="OpenKogama Launcher" width="400">
</picture>

The Worlds and Avatars tabs manage your local game data. Add community servers in the Servers tab and click Join to launch the selected KoGaMa version directly. The server chooses its entry world inside the game.

The launcher reads only `/api/server` (`title` and the optional fixed `/api/server/thumbnail` path) for server tiles. Titles are limited to 40 characters, thumbnails to 256 KB and 4096 pixels per side, then cropped to 400 × 160. The last valid title and thumbnail remain cached when the server is offline. Older servers can answer `/ping` without providing tile metadata.

## Install

Download `OpenKogamaLauncher-win-Setup.exe` from the latest release. The launcher updates itself from GitHub releases.

Program files live in `%LocalAppData%\OpenKogamaLauncher` and are replaced on every update. Your data stays in `%LocalAppData%\OpenKogama\OpenKogama Launcher`: installed game versions in `instances`, and worlds, avatars, logs and downloaded assets in `server`. It is kept when the launcher updates or is uninstalled.

### Linux

Download `OpenKogamaLauncher.AppImage` from the latest release, make it executable with `chmod +x OpenKogamaLauncher.AppImage` and run it. It updates itself like the Windows version, and on the first start it adds itself to the application menu and opens `openkogama://` links. It needs glibc 2.35 or newer, so Ubuntu 22.04, Linux Mint 21, Debian 12 or anything newer. If it does not start, install FUSE 2 (`libfuse2`, or `libfuse2t64` on Ubuntu 24.04).

The launcher and the local server run natively, but the KoGaMa builds are Windows programs and run through Wine, so install it from your distribution, for example `sudo apt install wine`. Web player versions and the other 32-bit builds need 32-bit Wine as well. The games use your Wine prefix (`WINEPREFIX`, or `~/.wine`), and `OPENKOGAMA_WINE` picks another Wine binary. Your data stays in `~/.local/share/OpenKogama/OpenKogama Launcher`.

### Building and releasing

To publish a release, run the release workflow with a tag such as `v1.0.0`. It builds the Windows and the Linux version, and both go into the same GitHub release. A tag with a dash, like `v1.0.0-rc.2`, becomes a pre-release that only pre-release installs update to.

Build with `-DBUILD_TESTING=ON` and run `ctest --output-on-failure` to check server metadata, direct join requests, size limits and offline caching. On Linux the build needs Qt 6.5 or newer with the private Qt Core headers (`qt6-base-private-dev` on Debian and Ubuntu). For local worlds, publish [openkogama-server](https://github.com/openkogama/openkogama-server) with `dotnet publish -r linux-x64 --self-contained -o <build folder>/server`.

## Translations

The launcher follows the system language, or the one picked under Settings, General, and falls back to English. Translations are made on Weblate and live in `i18n`: `launcher.ts` lists every text, `launcher_en.ts` holds the English plural forms, and each `launcher_<language>.ts` is one language, picked up by the build without further changes. After changing texts in the code, run `cmake --build <build folder> --target update_translations` and commit the updated files, so Weblate offers the new texts.

## Credits

Made by nightus.

- **Becko**: huge thanks, the main inspiration for this project, and the packet captures
- **LazyLemon**: info on how things looked in old versions, help with the launcher, founding the [r/OpenKogama](https://www.reddit.com/r/OpenKogama/) subreddit, and translating the website into Russian
- **bustersky**: archiving the standalone builds
- **kamilslimak**: finding the 2012 build
- **Dylinn**: announcing OpenKogama on Regama
- **Zpayer**: announcing OpenKogama
- **loafio**: sharing OpenKogama on his server
- **Limeen**: translating the website and the launcher into French
- **lordscorpioni**: translating the website and the launcher into Spanish
- **podleshik**: translating the launcher into Russian
- **reddevil**: porting the launcher to Linux
