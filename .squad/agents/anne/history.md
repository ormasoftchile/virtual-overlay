# Project Context

- **Owner:** Cristian Ormazabal
- **Project:** virtual-overlay — lightweight Windows utility (C++17/Win32) displaying virtual desktop name as an overlay. Watermark/notification modes, live rename detection, multi-monitor, dodge mode, zoom/pinch, tray icon, MSI installer.
- **Stack:** C++17, Win32 API, CMake, WiX (MSI)
- **Created:** 2026-05-02

## Learnings

<!-- Append new learnings below. Each entry is something lasting about the project. -->
- The GitHub release workflow is MSI-only; portable EXE artifacts are intentionally excluded from CI and tagged releases.
- Release installers must be built from `installer/Package.wxs`; it installs to `%LocalAppData%\VirtualOverlay\` and writes the HKCU `Run` key for autostart, so the Run target must stay quoted for paths with spaces.
- In WiX v4/v5, post-install launch sequencing in `InstallExecuteSequence` uses the `Condition` attribute on `<Custom ... />`; inner text conditions do not compile.
- `AppConfig` defaults come from the struct member initializers in `src/config/Config.h`; `Config::Reset()` rehydrates them via `m_config = AppConfig{}`, so changing overlay defaults there affects only fresh or missing configs.
- Tray startup currently runs through `WinMain -> App::Init() -> App::InitTrayIcon() -> TrayIcon::Show()` before the main message loop starts; the hidden owner window uses `WS_EX_TOOLWINDOW`, so no taskbar button is expected, and tray registration is only a one-shot `Shell_NotifyIconW(NIM_ADD)` with no retry or `TaskbarCreated` recovery.
- The tray icon is now queued with `PostMessageW(hwnd, WM_APP_INIT_TRAY, ...)` after app init, and the hidden main window also listens for the registered `TaskbarCreated` broadcast to re-add the icon after Explorer restarts.
- `Shell_NotifyIconW(NIM_ADD)` can fail during early post-install startup before the notification area is ready; `TrayIcon::AddIcon()` now arms a 1s window timer (`TIMER_TRAY_RETRY`) and retries up to 10 times through `MainWndProc` instead of failing permanently.

## Cross-Agent Notes (2026-05-02)

Collaborated with Icer Addis on installer consolidation:
- **Icer** architected the decision to deprecate `Product.wxs`/`UI.wxs` and standardize on `Package.wxs`
- **Anne** executed the fix: quoted Run key in `Package.wxs`, rewrote `installer/README.md` to document only the supported build path
- Both decisions merged into `.squad/decisions/decisions.md`
