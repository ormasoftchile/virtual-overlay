<#
.SYNOPSIS
    Downloads an offline Vosk speech model and installs it into %LOCALAPPDATA%\VirtualOverlay\models
    (or the repository models/ directory) for Virtual Overlay Presenter.

.PARAMETER Model
    The Vosk model name. Defaults to the small US-English model (~40 MB).
    Options: https://alphacephei.com/vosk/models
      vosk-model-small-en-us-0.15   (English, ~40 MB, fast, supports grammar lock)
      vosk-model-en-us-0.22         (English, ~1.8 GB, larger vocabulary)
#>
param(
    [string]$Model = "vosk-model-small-en-us-0.15",
    [string]$BaseUrl = "https://alphacephei.com/vosk/models",
    [string]$InstallDir = "$env:LOCALAPPDATA\VirtualOverlay\models"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$dest = Join-Path $InstallDir $Model
if (Test-Path $dest) {
    Write-Host "Model already installed: $dest" -ForegroundColor Green
    exit 0
}

$zip = Join-Path $InstallDir "$Model.zip"
$url = "$BaseUrl/$Model.zip"

Write-Host "Downloading $url ..." -ForegroundColor Cyan
Invoke-WebRequest -Uri $url -OutFile $zip

Write-Host "Extracting to $InstallDir ..." -ForegroundColor Cyan
Expand-Archive -Path $zip -DestinationPath $InstallDir -Force
Remove-Item $zip -Force

Write-Host "Installed offline Vosk model: $dest" -ForegroundColor Green
