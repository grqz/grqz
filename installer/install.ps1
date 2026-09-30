# NeonDL installer for Windows 10/11. Per-user, no admin needed.
#  - copies the helper and the extension to %LOCALAPPDATA%\NeonDL
#  - downloads yt-dlp, Deno and ffmpeg into %LOCALAPPDATA%\NeonDL\bin
#  - registers the helper with Brave / Chrome (native messaging)
# Run through install.cmd, or:  powershell -ExecutionPolicy Bypass -File install.ps1 [-SkipTools]
param([switch]$SkipTools)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$HostName    = 'com.neondl.host'
$ExtensionId = 'cnlfmgoafkakhohnjgciojahcendpbmp'   # fixed by the "key" in extension/manifest.json
$Repo        = 'ahanaf-adil/cloned-dm'
$Root        = Join-Path $env:LOCALAPPDATA 'NeonDL'
$Bin         = Join-Path $Root 'bin'
$Here        = $PSScriptRoot
$RepoRoot    = Split-Path $Here -Parent

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }

function Download($url, $dest) {
    Write-Host "    $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $dest
}

New-Item -ItemType Directory -Force -Path $Bin | Out-Null

# --- helper exe --------------------------------------------------------------
Step 'Installing the NeonDL helper'
Get-Process neondl-host -ErrorAction SilentlyContinue | Stop-Process -Force
$hostExe = Join-Path $Root 'neondl-host.exe'
$candidates = @(
    (Join-Path $Here 'neondl-host.exe'),
    (Join-Path $RepoRoot 'neondl-host.exe'),
    (Join-Path $RepoRoot 'host\build\Release\neondl-host.exe'),
    (Join-Path $RepoRoot 'host\build\neondl-host.exe')
)
$local = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($local) {
    Copy-Item -Force $local $hostExe
} else {
    Download "https://github.com/$Repo/releases/latest/download/neondl-host.exe" $hostExe
}

# --- extension ---------------------------------------------------------------
Step 'Copying the extension'
$extSrc = @((Join-Path $Here 'extension'), (Join-Path $RepoRoot 'extension')) |
    Where-Object { Test-Path (Join-Path $_ 'manifest.json') } | Select-Object -First 1
if (-not $extSrc) { throw 'Could not find the extension folder next to the installer.' }
$extDest = Join-Path $Root 'extension'
if (Test-Path $extDest) { Remove-Item -Recurse -Force $extDest }
Copy-Item -Recurse -Force $extSrc $extDest

# --- tools -------------------------------------------------------------------
if (-not $SkipTools) {
    $tmp = Join-Path $env:TEMP ("neondl-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    try {
        Step 'Downloading yt-dlp'
        Download 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe' (Join-Path $Bin 'yt-dlp.exe')

        Step 'Downloading Deno (JavaScript runtime yt-dlp needs for YouTube)'
        $denoZip = Join-Path $tmp 'deno.zip'
        Download 'https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip' $denoZip
        Expand-Archive -Force $denoZip (Join-Path $tmp 'deno')
        Copy-Item -Force (Join-Path $tmp 'deno\deno.exe') (Join-Path $Bin 'deno.exe')

        Step 'Downloading ffmpeg (yt-dlp build, ~90 MB)'
        $ffZip = Join-Path $tmp 'ffmpeg.zip'
        Download 'https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip' $ffZip
        Expand-Archive -Force $ffZip (Join-Path $tmp 'ffmpeg')
        $ffBin = Get-ChildItem -Recurse -Directory (Join-Path $tmp 'ffmpeg') |
            Where-Object { Test-Path (Join-Path $_.FullName 'ffmpeg.exe') } | Select-Object -First 1
        if (-not $ffBin) { throw 'ffmpeg.exe not found in the downloaded archive.' }
        $ffDest = Join-Path $Bin 'ffmpeg'
        if (Test-Path $ffDest) { Remove-Item -Recurse -Force $ffDest }
        New-Item -ItemType Directory -Force -Path $ffDest | Out-Null
        Copy-Item -Force (Join-Path $ffBin.FullName '*') $ffDest
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

# --- native messaging registration --------------------------------------------
Step 'Registering the helper with Brave and Chrome'
$manifestPath = Join-Path $Root "$HostName.json"
$manifest = [ordered]@{
    name            = $HostName
    description     = 'NeonDL download helper (runs yt-dlp)'
    path            = 'neondl-host.exe'
    type            = 'stdio'
    allowed_origins = @("chrome-extension://$ExtensionId/")
}
# UTF-8 without BOM: Chrome refuses manifests that start with a BOM.
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))

# Brave on Windows reads Chrome's key; the Brave and Chromium keys are added for good measure.
$keys = @(
    'HKCU:\Software\Google\Chrome\NativeMessagingHosts',
    'HKCU:\Software\BraveSoftware\Brave-Browser\NativeMessagingHosts',
    'HKCU:\Software\Chromium\NativeMessagingHosts'
)
foreach ($k in $keys) {
    New-Item -Force -Path "$k\$HostName" -Value $manifestPath | Out-Null
}

Write-Host ''
Write-Host 'Done!' -ForegroundColor Green
Write-Host 'Last step, in Brave:'
Write-Host '  1. Open  brave://extensions'
Write-Host '  2. Turn on "Developer mode" (top right)'
Write-Host '  3. Click "Load unpacked" and pick this folder:'
Write-Host "       $extDest" -ForegroundColor Magenta
Write-Host "  (Extension ID should read $ExtensionId)"
