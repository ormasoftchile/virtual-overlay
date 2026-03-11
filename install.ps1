#!/usr/bin/env pwsh
# Virtual Overlay Installer
# Usage: powershell -ExecutionPolicy ByPass -c "irm https://raw.githubusercontent.com/ormasoftchile/virtual-overlay/main/install.ps1 | iex"

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repo = "ormasoftchile/virtual-overlay"
$installDir = Join-Path $env:LOCALAPPDATA "VirtualOverlay"
$exeName = "virtual-overlay.exe"
$exePath = Join-Path $installDir $exeName
$appName = "VirtualOverlay"

try {
    # Fetch latest release
    Write-Host "Fetching latest release..." -ForegroundColor Cyan
    $releaseUrl = "https://api.github.com/repos/$repo/releases/latest"
    $release = Invoke-RestMethod -Uri $releaseUrl -Headers @{ "User-Agent" = "VirtualOverlay-Installer" }
    $version = $release.tag_name

    # Find the exe asset
    $asset = $release.assets | Where-Object { $_.name -eq $exeName }
    if (-not $asset) {
        throw "Could not find '$exeName' in release $version assets."
    }
    $downloadUrl = $asset.browser_download_url

    # Create install directory
    if (-not (Test-Path $installDir)) {
        New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    }

    # Download exe
    Write-Host "Downloading Virtual Overlay $version..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $downloadUrl -OutFile $exePath -UseBasicParsing

    # Create Start Menu shortcut
    Write-Host "Creating Start Menu shortcut..." -ForegroundColor Cyan
    $startMenuDir = Join-Path ([Environment]::GetFolderPath("StartMenu")) "Programs"
    $shortcutPath = Join-Path $startMenuDir "$appName.lnk"

    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exePath
    $shortcut.WorkingDirectory = $installDir
    $shortcut.Description = "Virtual Overlay — virtual desktop name overlays"
    $shortcut.Save()

    # Add to startup (registry)
    Write-Host "Adding to startup..." -ForegroundColor Cyan
    $regPath = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
    Set-ItemProperty -Path $regPath -Name $appName -Value "`"$exePath`""

    # Success
    Write-Host ""
    Write-Host "Virtual Overlay $version installed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Install location : $exePath"
    Write-Host "  Start Menu       : $shortcutPath"
    Write-Host "  Startup          : Enabled (runs at login)"
    Write-Host ""
    Write-Host "How to run:" -ForegroundColor Cyan
    Write-Host "  Run from Start Menu, or: $exePath"
    Write-Host ""
    Write-Host "To uninstall:" -ForegroundColor Yellow
    Write-Host "  powershell -ExecutionPolicy ByPass -c `"irm https://raw.githubusercontent.com/$repo/main/uninstall.ps1 | iex`""
}
catch {
    Write-Host ""
    Write-Host "Installation failed: $_" -ForegroundColor Red
    Write-Host "Please report issues at https://github.com/$repo/issues" -ForegroundColor Yellow
    exit 1
}
