#!/usr/bin/env pwsh
# Virtual Overlay Uninstaller
# Usage: powershell -ExecutionPolicy ByPass -c "irm https://raw.githubusercontent.com/ormasoftchile/virtual-overlay/main/uninstall.ps1 | iex"

$ErrorActionPreference = 'Stop'

$installDir = Join-Path $env:LOCALAPPDATA "VirtualOverlay"
$appName = "VirtualOverlay"

try {
    Write-Host "Uninstalling Virtual Overlay..." -ForegroundColor Cyan

    # Stop running instance
    $proc = Get-Process -Name "virtual-overlay" -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "Stopping running instance..." -ForegroundColor Yellow
        $proc | Stop-Process -Force
        Start-Sleep -Seconds 1
    }

    # Remove install directory
    if (Test-Path $installDir) {
        Remove-Item -Path $installDir -Recurse -Force
        Write-Host "  Removed: $installDir" -ForegroundColor Gray
    }
    else {
        Write-Host "  Install directory not found (already removed?)" -ForegroundColor Gray
    }

    # Remove Start Menu shortcut
    $startMenuDir = Join-Path ([Environment]::GetFolderPath("StartMenu")) "Programs"
    $shortcutPath = Join-Path $startMenuDir "$appName.lnk"
    if (Test-Path $shortcutPath) {
        Remove-Item -Path $shortcutPath -Force
        Write-Host "  Removed: Start Menu shortcut" -ForegroundColor Gray
    }
    else {
        Write-Host "  Start Menu shortcut not found (already removed?)" -ForegroundColor Gray
    }

    # Remove startup registry entry
    $regPath = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
    $regValue = Get-ItemProperty -Path $regPath -Name $appName -ErrorAction SilentlyContinue
    if ($regValue) {
        Remove-ItemProperty -Path $regPath -Name $appName -Force
        Write-Host "  Removed: Startup registry entry" -ForegroundColor Gray
    }
    else {
        Write-Host "  Startup registry entry not found (already removed?)" -ForegroundColor Gray
    }

    Write-Host ""
    Write-Host "Virtual Overlay uninstalled successfully." -ForegroundColor Green
}
catch {
    Write-Host ""
    Write-Host "Uninstall failed: $_" -ForegroundColor Red
    exit 1
}
