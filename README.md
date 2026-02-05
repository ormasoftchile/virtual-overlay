# Virtual Overlay

A lightweight Windows utility that displays an overlay showing your current virtual desktop name. Perfect for presentations, screen recordings, or just keeping track of which desktop you're on.

![Windows 10/11](https://img.shields.io/badge/Windows-10%20%7C%2011-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## Features

- **Watermark Mode** - Always-visible, transparent text overlay showing desktop name
- **Notification Mode** - Brief popup when switching desktops
- **Customizable** - Position, color, opacity, font size
- **Dodge Mode** - Overlay moves away when your cursor approaches
- **Multi-monitor** - Works across all displays
- **Keyboard Shortcut** - Toggle visibility with Ctrl+Shift+D
- **Desktop Names** - Shows custom names from Windows Settings
- **Zoom Feature** - Ctrl+Scroll to magnify screen content

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
2. Right-click the tray icon → **Settings**
3. Configure:
   - **Mode**: Watermark (always visible) or Notification (on switch)
   - **Position**: Corner or center of screen
   - **Appearance**: Color, opacity, font size
   - **Dodge**: Auto-hide when cursor approaches

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Shift+D | Toggle overlay visibility |
| Ctrl+Scroll | Zoom in/out |

### Configuration File

Settings are stored in:
```
%APPDATA%\VirtualOverlay\config.json
```

## Building from Source

### Requirements
- Visual Studio 2022 with C++ Desktop Development
- CMake 3.20+
- WiX Toolset 5 (for MSI installer)

### Build Steps

```powershell
# Clone
git clone https://github.com/ormasoftchile/virtual-overlay.git
cd virtual-overlay

# Generate project
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Build installer (optional)
cd installer
dotnet tool install --global wix --version 5.0.1
wix build Package.wxs -o VirtualOverlay.msi
```

### Project Structure

```
src/
├── main.cpp              # Entry point
├── App.cpp               # Application controller
├── config/               # Configuration management
├── desktop/              # Virtual Desktop COM API
├── overlay/              # D2D overlay rendering
├── settings/             # Settings UI
├── tray/                 # System tray icon
├── input/                # Keyboard/mouse hooks
├── zoom/                 # Magnification feature
└── utils/                # Helpers (logging, monitors)
```

## Technical Notes

### Windows Virtual Desktop API
This app uses undocumented COM interfaces that Microsoft changes between Windows versions. When the COM API fails (common on newer Windows 11 builds), the app falls back to a polling mechanism using the public `IVirtualDesktopManager` interface.

Desktop names are read from the Windows registry:
```
HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops\Desktops\{GUID}\Name
```

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
