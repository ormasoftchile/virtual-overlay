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

```powershell
# From the installer/ directory
wix build Package.wxs -o VirtualOverlay.msi
```

Expected install location: `%LocalAppData%\VirtualOverlay\`

Autostart is registered during install by writing the current user `Run` key. No manual registration is required.

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
| UpgradeCode | f47ac10b-58cc-4372-a567-0e02b2c3d479 | Never change - identifies product family |
| MainExecutable | a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d | Component GUID |
| ConfigDir | c3d4e5f6-a7b8-4c9d-0e1f-2a3b4c5d6e7f | Component GUID |
| ApplicationShortcut | b2c3d4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e | Component GUID |
| CleanupAutoStart | d4e5f6a7-b8c9-4d0e-1f2a-3b4c5d6e7f8a | Component GUID |

## Version Updates

When releasing a new version:

1. Update `Version` in Product.wxs
2. Keep `UpgradeCode` the same (enables upgrades)
3. Rebuild MSI
4. Major upgrades automatically uninstall previous versions
