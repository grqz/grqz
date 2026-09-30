# NeonDL

An IDM-style video/audio downloader for **Brave on Windows 11**, powered by [yt-dlp](https://github.com/yt-dlp/yt-dlp).

- Toolbar popup + right-click **"Download with NeonDL"** on any page, link or video
- Asks where to save (native Windows folder picker), or remembers the last folder
- Live progress, speed and ETA, with **pause / resume / cancel**
- Quality presets: Best, MP4, 4K / 1080p / 720p / 480p, MP3, M4A, and whole playlists
- 8 parallel fragment connections per download (`-N 8`), up to 3 downloads at once
- Dark neon UI

```
Brave extension (JS)  ──native messaging──▶  neondl-host.exe (C++, ~340 KB)  ──▶  yt-dlp + ffmpeg + Deno
```

The C++ helper only runs while something is downloading or the popup is open, then exits.

## Install

1. Get **`NeonDL-windows.zip`** from the repo's [Releases](../../releases) page (or from the *Build NeonDL* run under the **Actions** tab) and unzip it.
2. Double-click **`install.cmd`**. It downloads yt-dlp, Deno and ffmpeg (~150 MB in total) into `%LOCALAPPDATA%\NeonDL` and registers the helper with Brave. No admin rights needed.
3. In Brave, open `brave://extensions`, turn on **Developer mode**, click **Load unpacked** and pick `%LOCALAPPDATA%\NeonDL\extension`.

The extension ID must be `cnlfmgoafkakhohnjgciojahcendpbmp` (it's pinned by the `key` in `manifest.json`).

To uninstall, run `uninstall.cmd`, then remove the extension in `brave://extensions`.

## Use

- Click the toolbar icon: the current tab's link is filled in. Pick a quality and press **Download**.
- Or right-click a page, link or video and choose **Download with NeonDL**.
- **Pause** stops the download and keeps the partial file. **Resume** continues where it stopped (on servers that support it, which includes YouTube).
- **Cancel** stops the download and deletes the partial files.
- If Brave is closed mid-download, the item shows as *interrupted*; press play to continue.
- **Update yt-dlp** (bottom of the popup) runs `yt-dlp -U`. Do this when a site stops working.

## Build from source

The helper needs CMake and either Visual Studio 2022 or MinGW-w64:

```bat
cmake -S host -B host\build -A x64
cmake --build host\build --config Release
ctest --test-dir host\build -C Release
```

`install.cmd` picks up `host\build\Release\neondl-host.exe` automatically when run from a clone.
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
