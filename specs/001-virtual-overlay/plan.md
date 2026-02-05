# Implementation Plan: Virtual Overlay

**Branch**: `001-virtual-overlay` | **Date**: February 4, 2026 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-virtual-overlay/spec.md`

## Summary

A lightweight native Windows utility providing two core features: (1) macOS-style system-wide zoom with modifier+scroll and cursor-following pan using the Windows Magnification API, and (2) virtual desktop name/number overlay with Windows 11-style acrylic blur effects using Direct2D. The application runs as a background process with system tray integration, persists configuration in JSON, and includes a GUI settings window.

## Technical Context

**Language/Version**: C++17 (MSVC)  
**Primary Dependencies**: Windows SDK (Magnification API, Direct2D, DWM, COM), nlohmann/json (header-only)  
**Storage**: JSON configuration file (%APPDATA%\virtual-overlay\config.json)  
**Testing**: Manual testing, Windows Application Verifier for memory/handle leaks  
**Target Platform**: Windows 10 v1803+ (build 17134) and Windows 11, x64 only
**Project Type**: Single native Windows application (Win32)  
**Performance Goals**: 60 FPS pan/zoom, <100ms overlay appearance, <1ms input latency  
**Constraints**: <50MB memory, no runtime dependencies (static CRT), standard user privileges  
**Scale/Scope**: Single-user desktop utility, ~15 source files

## Build Toolchain

| Component | Version | Notes |
|-----------|---------|-------|
| **Compiler** | MSVC (Visual Studio 2022, v17.x) | "Desktop development with C++" workload required |
| **Language Standard** | C++17 | `/std:c++17` flag |
| **Build System** | CMake 3.20+ | Generates VS solution or Ninja |
| **Windows SDK** | 10.0.19041+ | For Magnification, Direct2D, DWM APIs |
| **Runtime Library** | Static CRT | `/MT` (Release), `/MTd` (Debug) — no vcruntime DLL |
| **Architecture** | x64 only | No 32-bit build |
| **Installer** | WiX Toolset 4.x | Optional, for MSI package |

### CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.20)
project(virtual-overlay VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(virtual-overlay WIN32
    src/main.cpp
    src/App.cpp
    # ... additional sources
    res/app.rc
)

target_link_libraries(virtual-overlay PRIVATE
    Magnification.lib
    d2d1.lib
    dwrite.lib
    dwmapi.lib
    ole32.lib
    oleaut32.lib
    shell32.lib
    user32.lib
    gdi32.lib
)

target_compile_definitions(virtual-overlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(virtual-overlay PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)
```

### Build Commands

```powershell
# Configure (generates Visual Studio solution)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --config Release

# Output
# build/Debug/virtual-overlay.exe
# build/Release/virtual-overlay.exe
```

### Alternative: Open in Visual Studio

Visual Studio 2022 has native CMake support. Open the folder directly → VS detects CMakeLists.txt → Configure → Build.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution file contains template placeholders only (no project-specific rules defined).
**Status**: PASS — No violations possible against unpopulated constitution.

**Note**: Project should define constitution principles before production release.

## Project Structure

### Documentation (this feature)

```text
specs/001-virtual-overlay/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (N/A - no API contracts)
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── main.cpp                 # Entry point, message loop
├── App.cpp/.h               # Application controller
│
├── overlay/
│   ├── OverlayWindow.cpp/.h # Direct2D overlay rendering
│   ├── AcrylicHelper.cpp/.h # Windows blur/acrylic effects
│   └── OverlayConfig.h      # Overlay settings struct
│
├── desktop/
│   ├── VirtualDesktop.cpp/.h      # Desktop detection & notifications
│   └── VirtualDesktopInterop.h    # Version-specific COM GUIDs
│
├── zoom/
│   ├── ZoomController.cpp/.h      # Zoom state machine
│   ├── Magnifier.cpp/.h           # Magnification API wrapper
│   └── ZoomConfig.h               # Zoom settings struct
│
├── input/
│   ├── GlobalHooks.cpp/.h         # Low-level keyboard/mouse hooks
│   └── GestureHandler.cpp/.h      # Touchpad pinch gesture support
│
├── settings/
│   ├── SettingsWindow.cpp/.h      # GUI settings window
│   └── SettingsPages.cpp/.h       # Tab pages for settings
│
├── config/
│   ├── Config.cpp/.h              # JSON config loader/saver
│   └── Defaults.h                 # Default configuration values
│
├── tray/
│   └── TrayIcon.cpp/.h            # System tray icon & menu
│
└── utils/
    ├── Monitor.cpp/.h             # Multi-monitor utilities
    ├── Animation.cpp/.h           # Easing/interpolation helpers
    ├── Logger.cpp/.h              # File logging
    └── D2DRenderer.cpp/.h         # Direct2D wrapper

res/
├── app.manifest                   # DPI-aware, Windows 10/11 compatibility
├── app.rc                         # Resources (icon, version info)
└── icon.ico

installer/
├── Product.wxs                    # WiX MSI main file
└── UI.wxs                         # WiX UI customization

lib/
└── json.hpp                       # nlohmann/json header

tests/                             # Manual test procedures
└── test-plan.md
```

**Structure Decision**: Single native Win32 application with modular source organization. No separate frontend/backend — all features in one executable. Installer as separate WiX project.

## Complexity Tracking

> No constitution violations to track.
