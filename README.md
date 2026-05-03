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

- Per-user installation (no admin required)
- Adds to Windows startup automatically
- Installs to `%LocalAppData%\VirtualOverlay`

### Portable
Download `virtual-overlay.exe` and run directly.

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
- Visual Studio 2022 with C++ Desktop Development
- CMake 3.20+
- WiX Toolset v4+ (for MSI installer)

### Build Steps

```powershell
# Clone
git clone https://github.com/ormasoftchile/virtual-overlay.git
cd virtual-overlay

# Build everything (EXE + MSI), no signing
.\build.ps1 -SkipSign

# Or manually:
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Build installer (from installer/ directory)
dotnet tool install --global wix
cd installer
wix build Package.wxs -ext WixToolset.Util.wixext -o VirtualOverlay.msi
```

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
