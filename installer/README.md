# Virtual Overlay Installer

This directory contains WiX v4 MSI installer definition files.

## Prerequisites

- [WiX Toolset v4](https://wixtoolset.org/releases/) or later
- .NET SDK 6.0 or later (for WiX v4)
- Built Release binary (`build/Release/virtual-overlay.exe`)

## Files

| File | Description |
|------|-------------|
| `Package.wxs` | Release MSI definition. Build this file for all release installers. |
| `Product.wxs` | ⚠️ Deprecated and not used for releases. Do not build this file. |
| `UI.wxs` | ⚠️ Deprecated and not used for releases. Do not build this file. |
| `License.rtf` | License agreement (create before build) |
| `banner.bmp` | 493x58 banner image for dialog headers |
| `dialog.bmp` | 493x312 background image for welcome dialog |

## Building the MSI

Build `Package.wxs` only. It is the only installer definition used for releases.

⚠️ `Product.wxs` and `UI.wxs` are deprecated and not used for releases. Do not build these.

### Recommended: use build.ps1 (from project root)

```powershell
.\build.ps1 -SkipSign       # build EXE + MSI, no signing
.\build.ps1                  # build EXE + MSI + sign both (requires Certum SimplySign)
```

### Manual (from the installer/ directory)

```powershell
wix build Package.wxs -ext WixToolset.Util.wixext -o VirtualOverlay.msi
```

⚠️ The `-ext WixToolset.Util.wixext` flag is required. The build will fail without it.

Expected install location: `%ProgramFiles%\VirtualOverlay\` (per-machine; prompts for admin).

The install is per-machine because signed builds are marked `uiAccess="true"` (see `VO_UIACCESS` in `CMakeLists.txt`), which Windows only honors for signed EXEs in Program Files. A signed `build\Release\virtual-overlay.exe` therefore will not launch from `build\`; install the MSI to run it. `-SkipSign` builds are not marked uiAccess and run from anywhere.

Earlier versions installed per-user to `%LocalAppData%\VirtualOverlay\`. MSI major upgrades cannot cross install scopes, so uninstall a per-user version before installing this one.

Autostart is registered during install by writing the installing user's `Run` key. No manual registration is required.

## Required Assets (Before Building)

Create the following files in the `installer/` directory:

### License.rtf

Create an RTF file with your license text. Minimal example:

```rtf
{\rtf1\ansi
Virtual Overlay - MIT License\par
\par
Copyright (c) 2026 Virtual Overlay Project\par
\par
Permission is hereby granted, free of charge...
}
```

### banner.bmp (493x58 pixels)

Top banner for installer dialogs. Use your project branding.

### dialog.bmp (493x312 pixels)

Side panel image for welcome/completion dialogs.

## Testing the Installer

1. **Install**: Double-click the MSI file
2. **Verify**:
   - Application appears in Start Menu
   - Desktop shortcut created (if enabled)
   - Tray icon appears when launched
3. **Uninstall**: Control Panel → Programs → Virtual Overlay → Uninstall
4. **Verify cleanup**:
   - Application removed from Start Menu
   - Auto-start registry key removed
   - Installation folder deleted

## GUIDs

| Purpose | GUID | Notes |
|---------|------|-------|
| UpgradeCode | F8E3B5A1-7C2D-4E9F-B6A8-3D1C5E7F9A2B | Never change - identifies product family |
| MainExecutable | EA0F230B-AD57-44D8-9E3A-F05D8A0AD277 | Component GUID (Package.wxs, Program Files) |
| AutoStart | 5404BA2C-0AF9-4B52-B45F-755D6DA263E6 | Component GUID (Package.wxs, HKCU Run key) |
| ConfigDir | c3d4e5f6-a7b8-4c9d-0e1f-2a3b4c5d6e7f | Component GUID |
| ApplicationShortcut | b2c3d4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e | Component GUID |
| CleanupAutoStart | d4e5f6a7-b8c9-4d0e-1f2a-3b4c5d6e7f8a | Component GUID |

## Version Updates

When releasing a new version:

1. Update `Version` in `Package.wxs`
2. Keep `UpgradeCode` the same (enables upgrades)
3. Rebuild MSI
4. Major upgrades automatically uninstall previous versions
