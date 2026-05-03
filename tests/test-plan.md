# Virtual Overlay Test Plan

**Version**: 1.0.0  
**Last Updated**: February 4, 2026

## Overview

This document describes manual test procedures for verifying Virtual Overlay overlay, watermark, tray, and virtual desktop features prior to release.

## Prerequisites

- Windows 10 version 1803 or later, or Windows 11
- At least one monitor (multi-monitor recommended for full testing)
- Multiple virtual desktops created (Win+Tab  New Desktop)

## Test Categories

1. [Overlay Feature Tests](#1-overlay-feature-tests)
2. [Settings Window Tests](#2-settings-window-tests)
3. [Tray Icon Tests](#3-tray-icon-tests)
4. [Multi-Monitor Tests](#4-multi-monitor-tests)
5. [Edge Cases and Error Handling](#5-edge-cases-and-error-handling)

---

## 1. Overlay Feature Tests

### 1.1 Basic Overlay Display

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Create at least 2 virtual desktops (Win+Tab) | Desktops created |
| 2 | Press Win+Ctrl+Right Arrow | Overlay appears showing "2: Desktop Name" |
| 3 | Wait 2 seconds | Notification overlay fades out automatically |
| 4 | Press Win+Ctrl+Left Arrow | Overlay shows "1: Desktop 1" |

**Pass**: [ ] **Fail**: [ ]

### 1.2 Watermark Mode

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings > Overlay tab | Overlay settings visible |
| 2 | Set Mode to Watermark and click Apply | Setting saved |
| 3 | Observe desktop | Watermark remains visible |
| 4 | Switch virtual desktops | Watermark text updates to the current desktop |

**Pass**: [ ] **Fail**: [ ]

### 1.3 Overlay Hotkey

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Ensure overlay is enabled | Overlay visible or available |
| 2 | Press Ctrl+Shift+D | Overlay toggles visibility |
| 3 | Press Ctrl+Shift+D again | Overlay toggles back |

**Pass**: [ ] **Fail**: [ ]

---

## 2. Settings Window Tests

### 2.1 Open Settings

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Right-click tray icon | Context menu appears |
| 2 | Click "Settings..." | Settings window opens |
| 3 | Verify 3 tabs exist | General, Overlay, About |

**Pass**: [ ] **Fail**: [ ]

### 2.2 Change Overlay Position

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings > Overlay tab | Overlay settings visible |
| 2 | Change Position dropdown to "Bottom Right" | Selection changes |
| 3 | Click "Preview" button | Overlay appears in bottom-right |
| 4 | Click "Apply" | Settings saved |
| 5 | Switch virtual desktops | Overlay now appears in bottom-right |

**Pass**: [ ] **Fail**: [ ]

### 2.3 Cancel Discards Changes

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings | Settings window opens |
| 2 | Change several settings | Settings modified in UI |
| 3 | Click "Cancel" | Window closes |
| 4 | Reopen Settings | Previous (saved) values shown |

**Pass**: [ ] **Fail**: [ ]

---

## 3. Tray Icon Tests

### 3.1 Tray Icon Visibility

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start Virtual Overlay | Tray icon appears in system tray |
| 2 | Hover over icon | Tooltip shows "Virtual Overlay" |

**Pass**: [ ] **Fail**: [ ]

### 3.2 Context Menu

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Right-click tray icon | Menu appears |
| 2 | Verify menu items | Settings, Start with Windows, About, Exit |
| 3 | Click "About" | About dialog appears |
| 4 | Close About dialog | Dialog closes |

**Pass**: [ ] **Fail**: [ ]

### 3.3 Exit Cleanup

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Ensure overlay is visible | Overlay shown |
| 2 | Click "Exit" from tray menu | Application closes |
| 3 | Verify tray icon removed | Icon no longer in tray |

**Pass**: [ ] **Fail**: [ ]

---

## 4. Multi-Monitor Tests

### 4.1 Overlay on Cursor Monitor

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Ensure overlay set to "Cursor Monitor" | Setting verified |
| 2 | Move cursor to secondary monitor | Cursor on secondary |
| 3 | Switch virtual desktops | Overlay appears on secondary monitor |
| 4 | Move cursor to primary, switch desktops | Overlay appears on primary |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

### 4.2 Monitor Hotplug

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disconnect a monitor | Monitor removed |
| 2 | Overlay still works | No crash, feature works |
| 3 | Reconnect monitor | Monitor restored |
| 4 | Switch desktops again | Overlay appears correctly |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

---

## 5. Edge Cases and Error Handling

### 5.1 Single Instance

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start Virtual Overlay | Application running |
| 2 | Try to start second instance | Settings window opens instead |
| 3 | Verify only one tray icon | Single icon present |

**Pass**: [ ] **Fail**: [ ]

### 5.2 Configuration File

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Delete config.json | File deleted |
| 2 | Start application | Default config created |
| 3 | Verify default settings | All defaults applied |

**Pass**: [ ] **Fail**: [ ]

### 5.3 DPI Changes

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change display scale (Settings > Display) | DPI changes |
| 2 | Overlay appears in correct position | Scaled correctly |
| 3 | No visual glitches | Clean rendering |

**Pass**: [ ] **Fail**: [ ]

---

## Performance Verification

| Check | Target | Result |
|-------|--------|--------|
| Peak memory usage | < 50 MB | [ ] MB |
| Desktop switch overlay | < 100ms | [ ] ms |

---

## Test Results Summary

| Category | Pass | Fail | N/A |
|----------|------|------|-----|
| Overlay Feature | | | |
| Settings Window | | | |
| Tray Icon | | | |
| Multi-Monitor | | | |
| Edge Cases | | | |

**Tester**: ____________________  
**Date**: ____________________  
**Build**: ____________________  
**Overall Result**: [ ] PASS [ ] FAIL
