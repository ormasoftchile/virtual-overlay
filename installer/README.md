# Virtual Overlay Installer

This directory contains WiX v4 MSI installer definition files.

## Prerequisites

- [WiX Toolset v4](https://wixtoolset.org/releases/) or later
- .NET SDK 6.0 or later (for WiX v4)
- Built Release binary (`build/Release/virtual-overlay.exe`)

## Files

| File | Description |
|------|-------------|
| `Product.wxs` | Main MSI definition with components and features |
| `UI.wxs` | UI customization and launch-on-exit behavior |
| `License.rtf` | License agreement (create before build) |
| `banner.bmp` | 493x58 banner image for dialog headers |
| `dialog.bmp` | 493x312 background image for welcome dialog |

## Building the MSI

### Option 1: Using WiX CLI (wix.exe)

```powershell
# From repository root
wix build installer/Product.wxs installer/UI.wxs `
    -d BuildDir=build/Release `
    -ext WixToolset.UI.wixext `
    -o build/VirtualOverlay-1.0.0.msi
```

### Option 2: Using dotnet

```powershell
# Create a wixproj file first, then:
dotnet build installer/VirtualOverlay.Installer.wixproj -c Release
```

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
