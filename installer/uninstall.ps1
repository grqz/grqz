# Removes NeonDL: helper, tools and native messaging registration.
# The extension itself is removed from brave://extensions.
$ErrorActionPreference = 'Continue'
$HostName = 'com.neondl.host'
Get-Process neondl-host, yt-dlp, ffmpeg -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "$env:LOCALAPPDATA\NeonDL\*" } | Stop-Process -Force
foreach ($k in 'HKCU:\Software\Google\Chrome\NativeMessagingHosts',
               'HKCU:\Software\BraveSoftware\Brave-Browser\NativeMessagingHosts',
               'HKCU:\Software\Chromium\NativeMessagingHosts') {
    Remove-Item -Force -Path "$k\$HostName" -ErrorAction SilentlyContinue
}
Remove-Item -Recurse -Force (Join-Path $env:LOCALAPPDATA 'NeonDL') -ErrorAction SilentlyContinue
Write-Host 'NeonDL removed. Remove the extension from brave://extensions too.' -ForegroundColor Green
