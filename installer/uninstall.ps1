# Removes CLDM from the folder this script is in, plus the registry entry Brave uses.
# Your downloads and any ffmpeg you pointed CLDM at are not touched.
# Remove the extension itself in brave://extensions.
$ErrorActionPreference = 'Continue'
$HostName = 'com.cldm.host'
$Root = $PSScriptRoot

Get-Process cldm-host, yt-dlp, ffmpeg, deno -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase) } |
    Stop-Process -Force

foreach ($k in 'HKCU:\Software\Google\Chrome\NativeMessagingHosts',
               'HKCU:\Software\BraveSoftware\Brave-Browser\NativeMessagingHosts',
               'HKCU:\Software\Chromium\NativeMessagingHosts') {
    Remove-Item -Force -Path "$k\$HostName" -ErrorAction SilentlyContinue
}

foreach ($item in 'bin', 'cache', 'extension', 'config.json', "$HostName.json", 'cldm-host.exe') {
    Remove-Item -Recurse -Force -LiteralPath (Join-Path $Root $item) -ErrorAction SilentlyContinue
}

Write-Host "CLDM removed from $Root." -ForegroundColor Green
Write-Host 'You can delete this folder now, and remove the extension in brave://extensions.'
