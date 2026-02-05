# Virtual Overlay Test Plan

**Version**: 1.0.0  
**Last Updated**: February 4, 2026

## Overview

This document describes manual test procedures for verifying all Virtual Overlay features prior to release.

## Prerequisites

- Windows 10 version 1803 or later, or Windows 11
- At least one monitor (multi-monitor recommended for full testing)
- Multiple virtual desktops created (Win+Tab → New Desktop)

## Test Categories

1. [Zoom Feature Tests](#1-zoom-feature-tests)
2. [Overlay Feature Tests](#2-overlay-feature-tests)
3. [Settings Window Tests](#3-settings-window-tests)
4. [Tray Icon Tests](#4-tray-icon-tests)
5. [Multi-Monitor Tests](#5-multi-monitor-tests)
6. [Edge Cases and Error Handling](#6-edge-cases-and-error-handling)

---

## 1. Zoom Feature Tests

### 1.1 Basic Zoom In/Out

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Hold Ctrl key | No visual change |
| 2 | While holding Ctrl, scroll mouse wheel down | Screen zooms in, centered on cursor |
| 3 | Continue scrolling down | Zoom level increases |
| 4 | Scroll mouse wheel up (still holding Ctrl) | Screen zooms out |
| 5 | Release Ctrl key | Zoom level maintained |

**Pass**: [ ] **Fail**: [ ]

### 1.2 Cursor-Following Pan

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Zoom in to 2x or higher | Screen magnified |
| 2 | Move cursor to right edge | View pans right to follow cursor |
| 3 | Move cursor to bottom edge | View pans down to follow cursor |
| 4 | Move cursor to top-left | View pans to top-left |

**Pass**: [ ] **Fail**: [ ]

### 1.3 Double-Tap Reset

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Zoom in to any level | Screen magnified |
| 2 | Double-tap Ctrl key quickly | Zoom resets to 1.0x (no zoom) |
| 3 | Verify timing: taps must be within 300ms | Reset occurs only if taps are quick |

**Pass**: [ ] **Fail**: [ ]

### 1.4 Touchpad Pinch (if touchpad available)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Pinch outward on touchpad | Screen zooms in |
| 2 | Pinch inward on touchpad | Screen zooms out |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

---

## 2. Overlay Feature Tests

### 2.1 Basic Overlay Display

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Create at least 2 virtual desktops (Win+Tab) | Desktops created |
| 2 | Press Win+Ctrl+Right Arrow | Overlay appears showing "2: Desktop Name" |
| 3 | Wait 2 seconds | Overlay fades out automatically |
| 4 | Press Win+Ctrl+Left Arrow | Overlay shows "1: Desktop 1" |

**Pass**: [ ] **Fail**: [ ]

### 2.2 Rapid Desktop Switching

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Rapidly switch desktops (Win+Ctrl+Arrow×3) | Overlay updates each time, no flicker |
| 2 | Final overlay shows correct desktop | Correct number/name |

**Pass**: [ ] **Fail**: [ ]

### 2.3 Overlay Animation

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Switch desktops | Overlay fades in smoothly (150ms) |
| 2 | After display period | Overlay fades out smoothly (200ms) |
| 3 | Slide-in animation visible | Overlay slides up slightly while fading in |

**Pass**: [ ] **Fail**: [ ]

---

## 3. Settings Window Tests

### 3.1 Open Settings

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Right-click tray icon | Context menu appears |
| 2 | Click "Settings..." | Settings window opens |
| 3 | Verify 4 tabs exist | General, Overlay, Zoom, About |

**Pass**: [ ] **Fail**: [ ]

### 3.2 Change Overlay Position

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings > Overlay tab | Overlay settings visible |
| 2 | Change Position dropdown to "Bottom Right" | Selection changes |
| 3 | Click "Preview" button | Overlay appears in bottom-right |
| 4 | Click "Apply" | Settings saved |
| 5 | Switch virtual desktops | Overlay now appears in bottom-right |

**Pass**: [ ] **Fail**: [ ]

### 3.3 Change Zoom Settings

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings > Zoom tab | Zoom settings visible |
| 2 | Change Modifier key to "Alt" | Selection changes |
| 3 | Click "Apply" | Settings saved |
| 4 | Test Alt+scroll | Zoom works with Alt key |
| 5 | Change back to "Ctrl" and Apply | Original behavior restored |

**Pass**: [ ] **Fail**: [ ]

### 3.4 Cancel Discards Changes

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings | Settings window opens |
| 2 | Change several settings | Settings modified in UI |
| 3 | Click "Cancel" | Window closes |
| 4 | Reopen Settings | Previous (saved) values shown |

**Pass**: [ ] **Fail**: [ ]

---

## 4. Tray Icon Tests

### 4.1 Tray Icon Visibility

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start Virtual Overlay | Tray icon appears in system tray |
| 2 | Hover over icon | Tooltip shows "Virtual Overlay" |

**Pass**: [ ] **Fail**: [ ]

### 4.2 Context Menu

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Right-click tray icon | Menu appears |
| 2 | Verify menu items | Settings, Start with Windows, About, Exit |
| 3 | Click "About" | About dialog appears |
| 4 | Close About dialog | Dialog closes |

**Pass**: [ ] **Fail**: [ ]

### 4.3 Exit Cleanup

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Zoom screen to 2x | Screen magnified |
| 2 | Click "Exit" from tray menu | Application closes |
| 3 | Verify zoom is reset | Screen returns to normal |
| 4 | Verify tray icon removed | Icon no longer in tray |

**Pass**: [ ] **Fail**: [ ]

### 4.4 Auto-Start

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Right-click tray icon | Menu appears |
| 2 | Click "Start with Windows" | Checkmark appears |
| 3 | Restart Windows (or check registry) | App launches on login |
| 4 | Disable "Start with Windows" | Checkmark removed |

**Pass**: [ ] **Fail**: [ ]

---

## 5. Multi-Monitor Tests

### 5.1 Zoom on Secondary Monitor

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Move cursor to secondary monitor | Cursor on secondary |
| 2 | Ctrl+scroll to zoom | Zoom works on secondary monitor |
| 3 | Pan to edges | View pans within screen bounds |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

### 5.2 Overlay on Cursor Monitor

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Ensure overlay set to "Cursor Monitor" | Setting verified |
| 2 | Move cursor to secondary monitor | Cursor on secondary |
| 3 | Switch virtual desktops | Overlay appears on secondary monitor |
| 4 | Move cursor to primary, switch desktops | Overlay appears on primary |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

### 5.3 Monitor Hotplug

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disconnect a monitor | Monitor removed |
| 2 | Zoom and overlay still work | No crash, features work |
| 3 | Reconnect monitor | Monitor restored |
| 4 | Zoom on reconnected monitor | Works correctly |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

---

## 6. Edge Cases and Error Handling

### 6.1 Windows Magnifier Conflict

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start Windows Magnifier (Win+Plus) | Magnifier running |
| 2 | Check application logs | Warning logged |
| 3 | Zoom may not work correctly | Expected behavior |

**Pass**: [ ] **Fail**: [ ]

### 6.2 Single Instance

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start Virtual Overlay | Application running |
| 2 | Try to start second instance | Settings window opens instead |
| 3 | Verify only one tray icon | Single icon present |

**Pass**: [ ] **Fail**: [ ]

### 6.3 Configuration File

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Delete config.json | File deleted |
| 2 | Start application | Default config created |
| 3 | Verify default settings | All defaults applied |

**Pass**: [ ] **Fail**: [ ]

### 6.4 DPI Changes

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change display scale (Settings > Display) | DPI changes |
| 2 | Overlay appears in correct position | Scaled correctly |
| 3 | No visual glitches | Clean rendering |

**Pass**: [ ] **Fail**: [ ]

---

## Performance Verification

### Memory Usage

| Check | Target | Result |
|-------|--------|--------|
| Peak memory usage | < 50 MB | [ ] MB |
| Memory after 1 hour idle | < 30 MB | [ ] MB |

### Input Latency

| Check | Target | Result |
|-------|--------|--------|
| Ctrl+scroll response | < 16ms | [ ] ms |
| Desktop switch overlay | < 100ms | [ ] ms |

---

## Test Results Summary

| Category | Pass | Fail | N/A |
|----------|------|------|-----|
| Zoom Feature | | | |
| Overlay Feature | | | |
| Settings Window | | | |
| Tray Icon | | | |
| Multi-Monitor | | | |
| Edge Cases | | | |

**Tester**: ____________________  
**Date**: ____________________  
**Build**: ____________________  
**Overall Result**: [ ] PASS [ ] FAIL
