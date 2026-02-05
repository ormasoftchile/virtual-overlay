# Quickstart: Virtual Overlay

**Date**: February 4, 2026  
**Feature**: [spec.md](spec.md) | [plan.md](plan.md)

---

## Prerequisites

| Requirement | Version | Notes |
|-------------|---------|-------|
| Windows | 10 (1803+) or 11 | Development and target platform |
| Visual Studio | 2022 (17.x) | With "Desktop development with C++" workload |
| CMake | 3.20+ | Build system |
| Windows SDK | 10.0.19041+ | For Magnification API, Direct2D |
| Git | Any | Version control |

### Optional
| Tool | Purpose |
|------|---------|
| WiX Toolset 4.x | MSI installer creation |
| Windows Application Verifier | Memory/handle leak detection |

---

## Setup

### 1. Clone Repository

```powershell
git clone <repository-url> virtual-overlay
cd virtual-overlay
git checkout 001-virtual-overlay
```

### 2. Install Dependencies

The only external dependency is nlohmann/json (header-only). Download to `lib/`:

```powershell
mkdir lib
Invoke-WebRequest -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -OutFile "lib/json.hpp"
```

### 3. Configure Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
```

Or open the folder directly in Visual Studio (CMake support built-in).

### 4. Build

```powershell
# Debug build
cmake --build build --config Debug

# Release build
cmake --build build --config Release
```

Output: `build/Debug/virtual-overlay.exe` or `build/Release/virtual-overlay.exe`

---

## Run

```powershell
# Run debug build
.\build\Debug\virtual-overlay.exe

# Run release build
.\build\Release\virtual-overlay.exe
```

The application will:
1. Show a system tray icon
2. Create config at `%APPDATA%\virtual-overlay\config.json` (if not exists)
3. Begin monitoring for modifier+scroll (zoom) and desktop switches (overlay)

### Verify Features

| Feature | Test Action | Expected Result |
|---------|-------------|-----------------|
| Zoom | Hold Ctrl, scroll up | Screen magnifies at cursor |
| Zoom reset | Double-tap Ctrl | Zoom returns to 1.0x |
| Pan | While zoomed, move cursor to edge | View follows cursor |
| Overlay | Win+Ctrl+Right (switch desktop) | Overlay shows desktop name |
| Settings | Right-click tray icon → Settings | Settings window opens |
| Exit | Right-click tray icon → Exit | Application closes, zoom resets |

---

## Project Structure Quick Reference

```
src/
├── main.cpp           # WinMain, message loop
├── App.cpp            # App lifecycle, component wiring
├── zoom/              # Magnification API wrapper
├── overlay/           # Direct2D overlay window
├── desktop/           # Virtual desktop COM interface
├── input/             # Low-level hooks
├── settings/          # GUI settings window
├── config/            # JSON config loader
├── tray/              # System tray icon
└── utils/             # Helpers (logging, animation, D2D)
```

---

## Development Workflow

### Adding a New Feature

1. Identify which module owns the feature
2. Add implementation to appropriate `src/<module>/` folder
3. Update `CMakeLists.txt` if adding new source files
4. Test manually (see test actions above)

### Modifying Configuration Schema

1. Update schema in `data-model.md`
2. Update `src/config/Defaults.h` with new defaults
3. Update `src/config/Config.cpp` to read/write new fields
4. Update settings GUI if user-facing

### Testing Virtual Desktop Detection

Virtual desktop COM interfaces are version-specific:

```powershell
# Check Windows build number
(Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion").CurrentBuild
```

Ensure GUIDs in `src/desktop/VirtualDesktopInterop.h` match your build.

---

## Debugging

### Logs

Logs are written to:
```
%APPDATA%\virtual-overlay\logs\virtual-overlay.log
```

### Debug Build Features

- Verbose logging enabled by default
- Assertions enabled
- Address Sanitizer can be enabled via CMake

### Common Issues

| Issue | Solution |
|-------|----------|
| Zoom not working | Check if Windows Magnifier is running (conflicts) |
| Overlay not appearing | Verify virtual desktops enabled; check COM GUIDs for Windows version |
| Hooks not capturing | Run app from normal (non-elevated) prompt; elevated apps block hooks |
| Blur not rendering | Check Windows "Transparency effects" is enabled in Settings |

---

## Build Installer (Optional)

Requires WiX Toolset 4.x installed.

```powershell
cd installer
wix build Product.wxs -o ..\build\virtual-overlay.msi
```

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                         App.cpp                              │
│                    (Main Controller)                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │   Config    │  │  TrayIcon   │  │  SettingsWindow     │  │
│  │   Loader    │  │             │  │                     │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                    │              │
│         └────────────────┼────────────────────┘              │
│                          │                                   │
│         ┌────────────────┼────────────────────┐              │
│         ▼                ▼                    ▼              │
│  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐    │
│  │    Zoom     │  │   Overlay   │  │   GlobalHooks     │    │
│  │ Controller  │  │   Window    │  │   (input)         │    │
│  └──────┬──────┘  └──────┬──────┘  └─────────┬─────────┘    │
│         │                │                   │               │
│         ▼                ▼                   │               │
│  ┌─────────────┐  ┌─────────────┐            │               │
│  │ Magnifier   │  │ D2DRenderer │◀───────────┘               │
│  │    API      │  └─────────────┘                            │
│  └─────────────┘                                             │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## Next Steps

See [tasks.md](tasks.md) for implementation breakdown (generated by `/speckit.tasks`).
