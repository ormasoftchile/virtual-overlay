# Anne — Tray registration reliability fix

## Decision
Defer the first tray registration until the hidden main window's message loop starts, and re-register the icon when Windows broadcasts `TaskbarCreated`.

## Why
`Shell_NotifyIconW(NIM_ADD, ...)` was being called during `App::Init()` before the first `GetMessageW()` pump. That left tray startup vulnerable to shell timing during install-launched startup, and the icon could also disappear permanently after an Explorer restart.

## Implementation
- Added `WM_APP_INIT_TRAY` in `src/tray/TrayIcon.h`.
- `WinMain` now posts `WM_APP_INIT_TRAY` to the hidden owner window after `App::Init(...)` succeeds.
- `MainWndProc` handles `WM_APP_INIT_TRAY` by calling `TrayIcon::Instance().Show()` on the first message pump iteration.
- Registered `TaskbarCreated` in `src/main.cpp` and call `TrayIcon::Instance().Restore()` when Explorer restarts.
- Refactored tray add logic into `TrayIcon::AddIcon()` so both initial show and recovery use the same `NIM_ADD`/`NIM_SETVERSION` path.

## Result
The tray icon no longer depends on pre-loop shell timing and will recover automatically after Explorer crashes or restarts.
