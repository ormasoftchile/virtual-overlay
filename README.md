# Virtual Overlay

A lightweight Windows utility that displays an overlay showing your current virtual desktop name. Perfect for presentations, screen recordings, or just keeping track of which desktop you're on.

![Windows 10/11](https://img.shields.io/badge/Windows-10%20%7C%2011-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## Features

- **Watermark Mode** - Always-visible, transparent text overlay showing desktop name
- **Notification Mode** - Brief popup when switching desktops
- **Live Rename Detection** - Overlay updates immediately when you rename a desktop
- **Customizable** - Position, color, opacity, font size
- **Dodge Mode** - Overlay moves away when your cursor approaches
- **Multi-monitor** - Works across all displays
- **Keyboard Shortcut** - Toggle visibility with Ctrl+Shift+D
- **Desktop Names** - Shows custom names from Windows Settings

## Installation

### MSI Installer (Recommended)
Download the latest `VirtualOverlay.msi` from [Releases](https://github.com/ormasoftchile/virtual-overlay/releases).

- Per-machine installation to `%ProgramFiles%\VirtualOverlay` (admin prompt on install)
- Adds to Windows startup automatically
- Signed releases are marked UIAccess so the teleprompter flip also covers the Start menu and Task View (Windows only allows this for signed apps in Program Files)
- Upgrading from 1.0.1 or earlier (per-user install): uninstall the old version first

### Portable
Keep `virtual-overlay.exe`, all four bundled Vosk DLLs, and the license/notice
files together in the same folder. Copying only the EXE will leave Presenter's
speech recognition unavailable. Use an unsigned build for portable use; signed
UIAccess builds must be installed in Program Files using the MSI.

## Usage

1. Run Virtual Overlay
2. Right-click the tray icon  **Settings**
3. Configure:
   - **Mode**: Watermark (always visible) or Notification (on switch)
   - **Position**: Corner or center of screen
   - **Appearance**: Color, opacity, font size
   - **Dodge**: Auto-hide when cursor approaches

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Shift+D | Toggle overlay visibility |

### Configuration File

Settings are stored in:
```
%APPDATA%\VirtualOverlay\config.json
```

## Building from Source

### Requirements
- Visual Studio 2022 or newer with C++ Desktop Development (C++20 support)
- CMake 3.20+
- WiX Toolset v4+ (for MSI installer)

CI builds use the `windows-2022` runner to match the Visual Studio 2022
generator; `windows-latest` may move to a newer Visual Studio version.

### Build Steps

```powershell
# Clone
git clone https://github.com/ormasoftchile/virtual-overlay.git
cd virtual-overlay

# Build everything (EXE + MSI), no signing
.\build.ps1 -SkipSign

# Build and sign everything (unlock signing in SimplySign Desktop first)
.\build.ps1

# Or manually:
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Build installer (from installer/ directory)
dotnet tool install --global wix
cd installer
wix build Package.wxs -ext WixToolset.Util.wixext -o VirtualOverlay.msi
```

Signed builds require the Windows SDK's `signtool` and access to the private key
of the Certum certificate configured in [build.ps1](build.ps1). Sign in to
SimplySign Desktop and unlock signing before running the build. If SignTool
reports "No certificates were found that met all the given criteria" despite
the certificate being installed and valid, reconnect/unlock SimplySign Desktop
to restore private-key access, then retry.

### Presenter speech runtime and models

CMake downloads the pinned Vosk 0.3.45 x64 runtime, verifies its SHA-256 checksum,
and copies these DLLs beside both executables in the selected build configuration:

- `libvosk.dll`
- `libgcc_s_seh-1.dll`
- `libstdc++-6.dll`
- `libwinpthread-1.dll`

The MSI includes all four DLLs and their license/notice files. The first
configuration needs internet access; later builds reuse the downloaded runtime.
If an older installation reports a missing Vosk runtime, rebuild and reinstall
the updated MSI rather than copying only `libvosk.dll`.

Attribution and component-specific terms are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). All Vosk dependency licenses and
[source-access information](lib/vosk/SOURCES.vosk.txt) are deployed with the
runtime and installed by the MSI. Releases must also publish `SOURCES.vosk.txt`
next to the installer/portable download so source is accessible before installation.
`build.ps1` copies it to `installer\SOURCES.vosk.txt` beside the MSI.

Speech models are separate from the runtime. Download the default English model
into `%LOCALAPPDATA%\VirtualOverlay\models` with:

```powershell
.\scripts\Get-VoskModel.ps1
```

The default model is Apache-2.0; other models may have different licenses.
Check the [official model catalogue](https://alphacephei.com/vosk/models) and
retain the selected model's notices before redistributing it.

### Project Structure

```
src/
 main.cpp              # Entry point, message loop
 App.cpp               # Application controller
 config/               # Configuration management
 desktop/              # Virtual Desktop detection (COM + registry polling)
 overlay/              # D2D overlay rendering
 settings/             # Settings UI
 tray/                 # System tray icon
 utils/                # Helpers (logging, monitors, animation)
```

## Technical Notes

### Windows Virtual Desktop API
This app uses undocumented COM interfaces that Microsoft changes between Windows versions. When the COM API fails (common on newer Windows 11 builds), the app falls back to a polling mechanism using the public `IVirtualDesktopManager` interface and registry reads.

Desktop names are read from the Windows registry:
```
HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops\Desktops\{GUID}\Name
```

Name changes are detected every 150ms via polling, so renaming a desktop updates the overlay without needing to switch away and back.

### Rendering
- Uses Direct2D with per-pixel alpha for true transparency
- `UpdateLayeredWindow` for watermark mode (no window chrome visible)
- Acrylic/Mica backdrop support for notification mode

## Contributing

Contributions welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) for details.

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history.

## Acknowledgments

- [nlohmann/json](https://github.com/nlohmann/json) - JSON for Modern C++
