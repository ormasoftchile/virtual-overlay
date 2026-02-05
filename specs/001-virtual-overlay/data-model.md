# Data Model: Virtual Overlay

**Date**: February 4, 2026  
**Feature**: [spec.md](spec.md) | [plan.md](plan.md)

---

## Overview

This is a native desktop application, not a client-server system. The "data model" represents:
1. **Configuration** — Persisted user preferences (JSON file)
2. **Runtime State** — In-memory application state (not persisted)

---

## 1. Configuration (Persisted)

### Schema: `config.json`

```json
{
  "$schema": "virtual-overlay-config-v1",
  
  "general": {
    "startWithWindows": true,
    "showTrayIcon": true,
    "settingsHotkey": "Ctrl+Shift+O"
  },
  
  "zoom": {
    "enabled": true,
    "modifierKey": "ctrl",
    "zoomStep": 0.25,
    "minZoom": 1.0,
    "maxZoom": 10.0,
    "smoothing": true,
    "smoothingFactor": 0.15,
    "animationDurationMs": 100,
    "doubleTapToReset": true,
    "doubleTapWindowMs": 300,
    "touchpadPinch": true
  },
  
  "overlay": {
    "enabled": true,
    "position": "top-center",
    "showDesktopNumber": true,
    "showDesktopName": true,
    "format": "{number}: {name}",
    "autoHide": true,
    "autoHideDelayMs": 2000,
    "monitor": "cursor",
    
    "style": {
      "blur": "acrylic",
      "tintColor": "#000000",
      "tintOpacity": 0.6,
      "cornerRadius": 8,
      "borderColor": "#404040",
      "borderWidth": 1,
      "shadowEnabled": true,
      "padding": 16
    },
    
    "text": {
      "fontFamily": "Segoe UI Variable",
      "fontSize": 20,
      "fontWeight": 600,
      "color": "#FFFFFF"
    },
    
    "animation": {
      "fadeInDurationMs": 150,
      "fadeOutDurationMs": 200,
      "slideIn": true,
      "slideDistance": 10
    }
  }
}
```

### Field Definitions

#### General Settings

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `startWithWindows` | bool | `true` | Launch application on Windows login |
| `showTrayIcon` | bool | `true` | Display system tray icon |
| `settingsHotkey` | string | `"Ctrl+Shift+O"` | Global hotkey to open settings |

#### Zoom Settings

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `enabled` | bool | `true` | — | Enable zoom feature |
| `modifierKey` | string | `"ctrl"` | ctrl/alt/shift/win | Key to hold for zoom |
| `zoomStep` | float | `0.25` | 0.1–1.0 | Zoom increment per scroll notch |
| `minZoom` | float | `1.0` | 1.0 (fixed) | Minimum zoom level |
| `maxZoom` | float | `10.0` | 2.0–20.0 | Maximum zoom level |
| `smoothing` | bool | `true` | — | Smooth pan/zoom transitions |
| `smoothingFactor` | float | `0.15` | 0.05–0.5 | Lower = smoother but slower |
| `animationDurationMs` | int | `100` | 0–500 | Zoom transition duration |
| `doubleTapToReset` | bool | `true` | — | Double-tap modifier resets zoom |
| `doubleTapWindowMs` | int | `300` | 100–1000 | Time window for double-tap |
| `touchpadPinch` | bool | `true` | — | Enable pinch gesture support |

#### Overlay Settings

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `true` | Enable overlay feature |
| `position` | enum | `"top-center"` | Overlay screen position |
| `showDesktopNumber` | bool | `true` | Include desktop index |
| `showDesktopName` | bool | `true` | Include desktop name |
| `format` | string | `"{number}: {name}"` | Display format template |
| `autoHide` | bool | `true` | Hide after timeout |
| `autoHideDelayMs` | int | `2000` | Time before auto-hide |
| `monitor` | enum | `"cursor"` | Which monitor shows overlay |

**Position enum**: `top-left`, `top-center`, `top-right`, `center`, `bottom-left`, `bottom-center`, `bottom-right`

**Monitor enum**: `cursor`, `primary`, `all`

#### Style Settings

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `blur` | enum | `"acrylic"` | Background effect (acrylic/mica/solid) |
| `tintColor` | string | `"#000000"` | Background tint (hex RGB) |
| `tintOpacity` | float | `0.6` | Background opacity (0.0–1.0) |
| `cornerRadius` | int | `8` | Rounded corner radius (px) |
| `borderColor` | string | `"#404040"` | Border color (hex RGB) |
| `borderWidth` | int | `1` | Border width (px) |
| `shadowEnabled` | bool | `true` | Drop shadow effect |
| `padding` | int | `16` | Content padding (px) |

#### Text Settings

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `fontFamily` | string | `"Segoe UI Variable"` | Font face |
| `fontSize` | int | `20` | Font size (pt) |
| `fontWeight` | int | `600` | Font weight (100–900) |
| `color` | string | `"#FFFFFF"` | Text color (hex RGB) |

#### Animation Settings

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `fadeInDurationMs` | int | `150` | Fade-in time |
| `fadeOutDurationMs` | int | `200` | Fade-out time |
| `slideIn` | bool | `true` | Slide animation on appear |
| `slideDistance` | int | `10` | Slide distance (px) |

---

## 2. Runtime State (In-Memory)

### ZoomState

```cpp
struct ZoomState {
    float currentLevel;      // 1.0 to maxZoom
    float targetLevel;       // For animation interpolation
    int offsetX;             // Pan offset X
    int offsetY;             // Pan offset Y
    HMONITOR activeMonitor;  // Monitor being zoomed
    bool modifierHeld;       // Is modifier key currently pressed
    DWORD lastModifierTap;   // Timestamp for double-tap detection
};
```

### OverlayState

```cpp
struct OverlayState {
    bool visible;            // Currently showing
    float opacity;           // Current opacity (for animation)
    int currentDesktopIndex; // 1-based
    std::wstring desktopName;// Current desktop name
    DWORD hideTimerStart;    // When auto-hide timer started
};
```

### VirtualDesktopInfo

```cpp
struct VirtualDesktopInfo {
    GUID id;                 // Desktop GUID
    int index;               // 1-based index
    std::wstring name;       // User-assigned name (may be empty)
};
```

---

## 3. Validation Rules

### On Load
1. If file missing → Create with defaults
2. If file corrupt (invalid JSON) → Log error, use defaults, offer to reset
3. For each field: If invalid type or out of range → Use default value

### On Save
1. Validate all values before writing
2. Create backup of previous config
3. Write atomically (write to temp, rename)

---

## 4. Migration Strategy

For future config versions:
```json
{
  "$schema": "virtual-overlay-config-v2",
  ...
}
```

On load, check schema version and migrate old configs forward.

---

## State Transitions

### Zoom State Machine

```
┌─────────────┐
│   NORMAL    │ (level = 1.0)
│             │
└──────┬──────┘
       │ modifier + scroll up
       ▼
┌─────────────┐
│   ZOOMING   │ (level > 1.0)
│             │◀──── cursor move: update offset
└──────┬──────┘
       │ scroll down to 1.0 OR double-tap modifier
       ▼
┌─────────────┐
│   NORMAL    │
└─────────────┘
```

### Overlay State Machine

```
┌─────────────┐
│   HIDDEN    │
│             │
└──────┬──────┘
       │ desktop switch event
       ▼
┌─────────────┐
│  FADE_IN    │ (opacity 0→1 over 150ms)
│             │
└──────┬──────┘
       │ animation complete
       ▼
┌─────────────┐
│   VISIBLE   │ (start hide timer)
│             │
└──────┬──────┘
       │ hide timer expires (2000ms)
       ▼
┌─────────────┐
│  FADE_OUT   │ (opacity 1→0 over 200ms)
│             │
└──────┬──────┘
       │ animation complete
       ▼
┌─────────────┐
│   HIDDEN    │
└─────────────┘
```

**Interrupt handling**: If desktop switches during VISIBLE or FADE_OUT, restart at FADE_IN with new data.
