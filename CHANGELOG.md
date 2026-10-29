# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Per-monitor watermark opt-out** - Per-screen checkboxes on the Overlay
  settings page ("Show watermark on these screens") keep the overlay and
  watermark off specific monitors. While the overlay's target screen is opted
  out it stays hidden and reappears automatically on a screen that allows it.

### Changed
- Per-monitor feature toggles (flip and watermark opt-out) now live in a unified
  `monitors` registry in `config.json`
  (`"monitors": { "<screen-id>": { "flip": true, "overlay": false } }`).
  Legacy `flip.monitors` arrays are migrated automatically on load and are no
  longer written.
- When the cursor is on a mirrored (flipped) teleprompter screen, the watermark
  now hides instead of relocating to the primary monitor. Mirrored non-primary
  screens never show the watermark (MirrorWindow would capture and re-render it
  mirrored); the primary keeps it, as before.

## [1.0.0] - 2026-02-05

### Added
- **Watermark Mode** - Always-visible transparent overlay showing desktop name
- **Notification Mode** - Brief popup when switching virtual desktops
- **Customizable Appearance**
  - Position (corners, center, top/bottom)
  - Text color picker
  - Opacity slider (0-100%)
  - Font size slider
- **Dodge Mode** - Overlay moves to opposite side when cursor approaches
  - Horizontal dodge for corner positions
  - Vertical dodge for center positions
  - Multi-monitor aware (stays on same monitor during dodge)
- **Keyboard Shortcut** - Ctrl+Shift+D to toggle overlay visibility
- **Desktop Names** - Reads custom names from Windows Settings
- **Multi-Monitor Support** - Works with cursor, primary, or all monitors
- **Settings UI** - Full settings dialog with live preview
- **System Tray** - Minimize to tray with context menu
- **MSI Installer** - Per-user installation, no admin required
- **Windows Startup** - Optional auto-start with Windows

### Technical
- Direct2D rendering with per-pixel alpha transparency
- Polling fallback for Windows 11 24H2+ COM API compatibility
- WiX 5 installer
- GitHub Actions CI/CD pipeline

### Compatibility
- Windows 10 (1803+)
- Windows 11 (all versions including 24H2 preview builds)

[Unreleased]: https://github.com/ormasoftchile/virtual-overlay/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/ormasoftchile/virtual-overlay/releases/tag/v1.0.0
