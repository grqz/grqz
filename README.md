# CLDM

An IDM-style video/audio downloader for **Brave on Windows 11**, powered by [yt-dlp](https://github.com/yt-dlp/yt-dlp).

- Toolbar popup + right-click **"Download with CLDM"** on any page, link or video
- Asks where to save (native Windows folder picker), or remembers the last folder
- Live progress, speed and ETA, with **pause / resume / cancel**
- Quality presets: Best, MP4, 4K / 1080p / 720p / 480p, MP3, M4A, and whole playlists
- 8 parallel fragment connections per download (`-N 8`), up to 3 downloads at once
- Dark neon UI

```
Brave extension (JS)  ──native messaging──▶  cldm-host.exe (C++, ~340 KB)  ──▶  yt-dlp + ffmpeg + Deno
```

The C++ helper only runs while something is downloading or the popup is open, then exits.

## Install

1. Get **`CLDM-windows.zip`** from the repo's [Releases](../../releases) page (or from the *Build CLDM* run under the **Actions** tab) and unzip it **where you want CLDM to live** (e.g. `D:\Apps\CLDM`).
2. Double-click **`install.cmd`**. It asks two things:
   - **Install folder**: press Enter to install right where you unzipped it.
   - **ffmpeg**: type the folder of an ffmpeg you already have, or press Enter to let CLDM download its own copy into `bin\ffmpeg`.

   Then it downloads yt-dlp and Deno into the folder and registers the helper with Brave. No admin rights needed.
3. In Brave, open `brave://extensions`, turn on **Developer mode**, click **Load unpacked** and pick the `extension` folder inside your CLDM folder.

The extension ID must be `cnlfmgoafkakhohnjgciojahcendpbmp` (it's pinned by the `key` in `manifest.json`).

### What goes where

Everything stays in the folder you chose:

```
CLDM\
  cldm-host.exe         helper Brave talks to
  com.cldm.host.json    tells Brave where the helper is
  config.json           tool paths (edit "ffmpeg" to point at another ffmpeg any time)
  extension\            the Brave extension
  bin\yt-dlp\           yt-dlp (folder build: starts faster, never unpacks into %TEMP%)
  bin\deno.exe          JavaScript runtime yt-dlp needs for YouTube
  bin\ffmpeg\           only if you let CLDM download ffmpeg
  cache\                yt-dlp + Deno cache and temp files (instead of AppData)
  install.cmd, uninstall.cmd
```

Outside it there is only one registry entry (`HKCU\Software\Google\Chrome\NativeMessagingHosts\com.cldm.host`, plus the Brave/Chromium equivalents). Brave requires it to find the helper. Your downloads go wherever you pick in the popup.

To uninstall, run `uninstall.cmd` in the CLDM folder (it removes the registry entry and everything above except the scripts, and never touches your own ffmpeg), then remove the extension in `brave://extensions`.

## Use

- Click the toolbar icon: the current tab's link is filled in. Pick a quality and press **Download**.
- Or right-click a page, link or video and choose **Download with CLDM**.
- **Pause** stops the download and keeps the partial file. **Resume** continues where it stopped (on servers that support it, which includes YouTube).
- **Cancel** stops the download and deletes the partial files.
- If Brave is closed mid-download, the item shows as *interrupted*; press play to continue.
- **Update yt-dlp** (bottom of the popup) downloads the latest yt-dlp into `bin\yt-dlp`. Do this when a site stops working.

## Build from source

The helper needs CMake and either Visual Studio 2022 or MinGW-w64:

```bat
cmake -S host -B host\build -A x64
cmake --build host\build --config Release
ctest --test-dir host\build -C Release
```

`install.cmd` picks up `host\build\Release\cldm-host.exe` automatically when run from a clone.
The `tools/make-icons.js` script regenerates the icons (`node tools/make-icons.js`).

## Layout

| Path | What |
|---|---|
| `extension/` | Manifest V3 extension: `background.js` (queue, helper connection), `popup.*` (UI) |
| `host/src/main_win.cpp` | Native messaging host: runs yt-dlp in a Job Object, folder picker, Explorer |
| `host/src/core.hpp` | yt-dlp arguments and output parsing (portable, unit-tested) |
| `installer/` | `install.cmd` / `install.ps1`, `uninstall.*` |
| `.github/workflows/build.yml` | Windows build, tests, zip, release on `v*` tags |

## Notes

- The Chrome Web Store bans YouTube downloaders, so the extension is loaded unpacked.
- YouTube needs a JavaScript runtime since yt-dlp 2025.11.12, so Deno is bundled.
- Only download content you have the right to download.
