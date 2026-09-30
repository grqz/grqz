# CLDM installer for Windows 10/11. Per-user, no admin needed.
#
# Everything lives in ONE folder you choose:
#   <folder>\cldm-host.exe     the helper Brave talks to
#   <folder>\extension\        load this in brave://extensions
#   <folder>\bin\              yt-dlp, Deno (and ffmpeg, unless you point CLDM at your own)
#   <folder>\cache\            yt-dlp/Deno cache and temp files
#   <folder>\config.json       where the tools are
# The only thing outside that folder is the registry entry Brave needs to find the helper.
#
#   install.cmd                                         asks where to install and which ffmpeg to use
#   install.ps1 -InstallDir D:\CLDM -Ffmpeg D:\ffmpeg   no questions
#   install.ps1 -UpdateYtdlp                            used by the popup's "Update yt-dlp" button
param(
    [string]$InstallDir,
    [string]$Ffmpeg,
    [switch]$UpdateYtdlp,
    [switch]$SkipTools
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$HostName    = 'com.cldm.host'
$ExtensionId = 'cnlfmgoafkakhohnjgciojahcendpbmp'   # fixed by the "key" in extension/manifest.json
$Repo        = 'ahanaf-adil/cloned-dm'
$Here        = $PSScriptRoot
$RepoRoot    = Split-Path $Here -Parent
$RegKeys     = @(
    'HKCU:\Software\Google\Chrome\NativeMessagingHosts',          # Brave on Windows reads this one
    'HKCU:\Software\BraveSoftware\Brave-Browser\NativeMessagingHosts',
    'HKCU:\Software\Chromium\NativeMessagingHosts'
)

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }

function Download($url, $dest) {
    Write-Host "    $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $dest
}

function Full($path) { [IO.Path]::GetFullPath($path).TrimEnd('\') }

function Same-Path($a, $b) { (Full $a) -ieq (Full $b) }

# Scratch space inside the install folder, so nothing lands in %TEMP%.
function New-Scratch($root) {
    $dir = Join-Path $root ("cache\tmp\install-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $dir
}

function Write-Utf8($path, $text) {
    # No BOM: Chrome refuses native messaging manifests that start with one.
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false))
}

# yt-dlp's folder build: starts faster than the single exe and never unpacks itself into %TEMP%.
function Install-Ytdlp($root) {
    $tmp = New-Scratch $root
    try {
        Download 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_win.zip' "$tmp\yt-dlp.zip"
        Expand-Archive -Force "$tmp\yt-dlp.zip" "$tmp\yt-dlp"
        if (-not (Test-Path "$tmp\yt-dlp\yt-dlp.exe")) { throw 'yt-dlp.exe not found in the downloaded archive.' }
        $dest = Join-Path $root 'bin\yt-dlp'
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        Move-Item "$tmp\yt-dlp" $dest
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

# Accepts ffmpeg.exe itself, its folder, or a folder with a bin\ subfolder. Returns the folder or $null.
function Resolve-Ffmpeg($path) {
    $path = "$path".Trim().Trim('"')
    if (-not $path) { return $null }
    foreach ($c in @($path, (Join-Path $path 'ffmpeg.exe'), (Join-Path $path 'bin\ffmpeg.exe'))) {
        if ((Test-Path -LiteralPath $c -PathType Leaf) -and ((Split-Path $c -Leaf) -ieq 'ffmpeg.exe')) {
            return (Split-Path (Resolve-Path -LiteralPath $c).Path -Parent)
        }
    }
    return $null
}

# --- update mode (called by the helper) ----------------------------------------
if ($UpdateYtdlp) {
    try {
        Install-Ytdlp $Here
        $version = & (Join-Path $Here 'bin\yt-dlp\yt-dlp.exe') --version
        Write-Output "yt-dlp updated to $version"
        exit 0
    } catch {
        Write-Output "Update failed: $($_.Exception.Message)"
        exit 1
    }
}

# --- 1. install folder ----------------------------------------------------------
$default = if (Test-Path (Join-Path $Here 'cldm-host.exe')) { $Here } else { Join-Path $env:SystemDrive 'CLDM' }
if (-not $InstallDir) {
    Write-Host ''
    Write-Host 'CLDM keeps everything (helper, extension, yt-dlp, Deno, cache) in one folder.'
    $answer = (Read-Host "Install folder (Enter = $default)").Trim().Trim('"')
    $InstallDir = if ($answer) { $answer } else { $default }
}
$Root = Full $InstallDir
foreach ($pf in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:windir)) {
    if ($pf -and ($Root -like "$(Full $pf)*")) { throw "Pick a folder outside $pf (it isn't writable without admin rights)." }
}
New-Item -ItemType Directory -Force -Path $Root, "$Root\bin", "$Root\cache" | Out-Null

# --- 2. ffmpeg -------------------------------------------------------------------
$oldConfig = $null
if (Test-Path "$Root\config.json") {
    try { $oldConfig = Get-Content -Raw "$Root\config.json" | ConvertFrom-Json } catch { }
}
$ffDir = $null
if ($Ffmpeg) {
    $ffDir = Resolve-Ffmpeg $Ffmpeg
    if (-not $ffDir) { throw "No ffmpeg.exe found in '$Ffmpeg'." }
} elseif (-not $SkipTools) {
    $suggest = $null
    if ($oldConfig -and $oldConfig.ffmpeg -and -not (Same-Path $oldConfig.ffmpeg "$Root\bin\ffmpeg")) {
        $suggest = Resolve-Ffmpeg $oldConfig.ffmpeg
    }
    if (-not $suggest) {
        $onPath = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($onPath) { $suggest = Split-Path $onPath.Source -Parent }
    }
    Write-Host ''
    Write-Host 'ffmpeg: use one you already have, or let CLDM download its own copy (~90 MB) into bin\ffmpeg.'
    while ($true) {
        $hint = if ($suggest) { "Enter = use $suggest, D = download a copy" } else { 'Enter = download a copy' }
        $answer = (Read-Host "Your ffmpeg folder ($hint)").Trim().Trim('"')
        if (-not $answer) { $ffDir = $suggest; break }
        if ($answer -ieq 'd') { $ffDir = $null; break }
        $ffDir = Resolve-Ffmpeg $answer
        if ($ffDir) { break }
        Write-Host "    No ffmpeg.exe in '$answer'. Try again." -ForegroundColor Yellow
    }
}
if ($ffDir -and -not (Test-Path (Join-Path $ffDir 'ffprobe.exe'))) {
    Write-Host "    Note: ffprobe.exe is missing next to ffmpeg.exe; some conversions may fail." -ForegroundColor Yellow
}

# --- 3. app files ------------------------------------------------------------------
Step "Installing CLDM to $Root"
Get-Process cldm-host -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

$hostExe = Join-Path $Root 'cldm-host.exe'
$localExe = @(
    (Join-Path $Here 'cldm-host.exe'),
    (Join-Path $RepoRoot 'host\build\Release\cldm-host.exe'),
    (Join-Path $RepoRoot 'host\build\cldm-host.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($localExe) {
    if (-not (Same-Path $localExe $hostExe)) { Copy-Item -Force $localExe $hostExe }
} else {
    Download "https://github.com/$Repo/releases/latest/download/cldm-host.exe" $hostExe
}

$extSrc = @((Join-Path $Here 'extension'), (Join-Path $RepoRoot 'extension')) |
    Where-Object { Test-Path (Join-Path $_ 'manifest.json') } | Select-Object -First 1
if (-not $extSrc) { throw 'Could not find the extension folder next to the installer.' }
$extDest = Join-Path $Root 'extension'
if (-not (Same-Path $extSrc $extDest)) {
    if (Test-Path $extDest) { Remove-Item -Recurse -Force $extDest }
    Copy-Item -Recurse -Force $extSrc $extDest
}

# The scripts stay with the install: "Update yt-dlp" and uninstall use them.
if (-not (Same-Path $Here $Root)) {
    foreach ($f in 'install.ps1', 'install.cmd', 'uninstall.ps1', 'uninstall.cmd') {
        Copy-Item -Force (Join-Path $Here $f) (Join-Path $Root $f)
    }
}

# --- 4. tools ------------------------------------------------------------------------
if (-not $SkipTools) {
    Step 'Downloading yt-dlp'
    Install-Ytdlp $Root

    Step 'Downloading Deno (the JavaScript runtime yt-dlp needs for YouTube)'
    $tmp = New-Scratch $Root
    try {
        Download 'https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip' "$tmp\deno.zip"
        Expand-Archive -Force "$tmp\deno.zip" "$tmp\deno"
        Copy-Item -Force "$tmp\deno\deno.exe" (Join-Path $Root 'bin\deno.exe')
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }

    $ownFfmpeg = Join-Path $Root 'bin\ffmpeg'
    if ($ffDir) {
        Step "Using your ffmpeg in $ffDir"
        if ((Test-Path $ownFfmpeg) -and -not (Same-Path $ffDir $ownFfmpeg)) { Remove-Item -Recurse -Force $ownFfmpeg }
    } else {
        Step 'Downloading ffmpeg (yt-dlp build, ~90 MB)'
        $tmp = New-Scratch $Root
        try {
            Download 'https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip' "$tmp\ffmpeg.zip"
            Expand-Archive -Force "$tmp\ffmpeg.zip" "$tmp\ffmpeg"
            $bin = Get-ChildItem -Recurse -Directory "$tmp\ffmpeg" |
                Where-Object { Test-Path (Join-Path $_.FullName 'ffmpeg.exe') } | Select-Object -First 1
            if (-not $bin) { throw 'ffmpeg.exe not found in the downloaded archive.' }
            if (Test-Path $ownFfmpeg) { Remove-Item -Recurse -Force $ownFfmpeg }
            Move-Item $bin.FullName $ownFfmpeg
        } finally {
            Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        }
        $ffDir = $ownFfmpeg
    }
} elseif (-not $ffDir -and $oldConfig) {
    $ffDir = $oldConfig.ffmpeg
}

# --- 5. config + registration --------------------------------------------------------------
Step 'Registering the helper with Brave'
$ffValue = if ($ffDir) { "$ffDir" } else { '' }
$config = [ordered]@{
    ytdlp  = Join-Path $Root 'bin\yt-dlp\yt-dlp.exe'
    ffmpeg = $ffValue
    deno   = Join-Path $Root 'bin\deno.exe'
}
Write-Utf8 (Join-Path $Root 'config.json') ($config | ConvertTo-Json)

$manifestPath = Join-Path $Root "$HostName.json"
$manifest = [ordered]@{
    name            = $HostName
    description     = 'CLDM download helper (runs yt-dlp)'
    path            = 'cldm-host.exe'
    type            = 'stdio'
    allowed_origins = @("chrome-extension://$ExtensionId/")
}
Write-Utf8 $manifestPath ($manifest | ConvertTo-Json)
foreach ($k in $RegKeys) {
    New-Item -Force -Path "$k\$HostName" -Value $manifestPath | Out-Null
}

# Remove leftovers of the earlier "NeonDL" build, which installed into %LOCALAPPDATA%.
foreach ($k in $RegKeys) { Remove-Item -Force -Path "$k\com.neondl.host" -ErrorAction SilentlyContinue }
$oldDir = Join-Path $env:LOCALAPPDATA 'NeonDL'
if (Test-Path (Join-Path $oldDir 'neondl-host.exe')) {
    Get-Process neondl-host -ErrorAction SilentlyContinue | Stop-Process -Force
    Remove-Item -Recurse -Force $oldDir -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host 'Done!' -ForegroundColor Green
Write-Host "Everything is in: $Root"
Write-Host ''
Write-Host 'Last step, in Brave:'
Write-Host '  1. Open  brave://extensions'
Write-Host '  2. Turn on "Developer mode" (top right)'
Write-Host '  3. Click "Load unpacked" and pick this folder:'
Write-Host "       $extDest" -ForegroundColor Magenta
Write-Host "  (Extension ID should read $ExtensionId)"
