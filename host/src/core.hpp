// Platform-independent logic: yt-dlp arguments, output parsing, path checks.
// Kept free of Win32 so it can be unit-tested anywhere.
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace core {

// Every machine-readable line yt-dlp prints for us starts with one of these tags.
// The last field of each template may contain '|', so it always goes last.
inline const char* kProgressTemplate =
    "download:NDLP|%(progress.status)s|%(progress.downloaded_bytes)s|%(progress.total_bytes)s"
    "|%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s"
    "|%(progress.fragment_index)s|%(progress.fragment_count)s"
    "|%(info.vcodec)s|%(info.acodec)s|%(progress.filename)s";
inline const char* kPostprocessTemplate = "postprocess:NDPP|%(progress.status)s|%(progress.postprocessor)s";
inline const char* kInfoTemplate = "video:NDINFO|%(id)s|%(playlist_index|)s|%(n_entries|)s|%(title)s";
inline const char* kFileTemplate = "after_move:NDFILE|%(filepath)s";

struct Tools {
    std::string ytdlp;      // path to yt-dlp.exe
    std::string ffmpegDir;  // folder holding ffmpeg.exe, empty if missing
    std::string deno;       // path to deno.exe, empty if missing
};

struct StartParams {
    std::string url;
    std::string dir;
    std::string preset;
    bool playlist = false;
};

inline bool startsWithNoCase(const std::string& s, const char* prefix) {
    size_t i = 0;
    for (; prefix[i]; ++i) {
        if (i >= s.size()) return false;
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

inline bool isHttpUrl(const std::string& url) {
    if (!startsWithNoCase(url, "http://") && !startsWithNoCase(url, "https://")) return false;
    if (url.size() > 8192) return false;
    for (unsigned char c : url)
        if (c <= 0x20 || c == 0x7F) return false;
    return true;
}

inline std::vector<std::string> presetArgs(const std::string& preset) {
    auto capped = [](const char* h) {
        std::string f = std::string("bv*[height<=") + h + "]+ba/b[height<=" + h + "]/b";
        return std::vector<std::string>{"-t", "mp4", "-f", f};
    };
    if (preset == "best") return {"-f", "bv*+ba/b"};
    if (preset == "mp4") return {"-t", "mp4"};
    if (preset == "2160") return capped("2160");
    if (preset == "1080") return capped("1080");
    if (preset == "720") return capped("720");
    if (preset == "480") return capped("480");
    if (preset == "mp3") return {"-t", "mp3", "--audio-quality", "0", "--embed-metadata"};
    if (preset == "m4a") return {"-f", "ba[ext=m4a]/ba/b", "-x", "--audio-format", "m4a", "--embed-metadata"};
    return {};
}

inline bool validPreset(const std::string& preset) { return !presetArgs(preset).empty(); }

// Arguments after argv[0]. The URL always follows "--" so it can never be read as an option.
inline std::vector<std::string> buildArgs(const StartParams& p, const Tools& t) {
    std::vector<std::string> a = {
        "--ignore-config", "--encoding", "utf-8", "--color", "never",
        "--newline", "--progress", "--no-simulate", "--progress-delta", "0.5",
        "--progress-template", kProgressTemplate,
        "--progress-template", kPostprocessTemplate,
        "--print", kInfoTemplate,
        "--print", kFileTemplate,
        "-N", "8", "--no-mtime",
        "-P", p.dir,
        "-o", "%(title).150B [%(id)s].%(ext)s",
        p.playlist ? "--yes-playlist" : "--no-playlist",
    };
    if (!t.ffmpegDir.empty()) { a.push_back("--ffmpeg-location"); a.push_back(t.ffmpegDir); }
    if (!t.deno.empty()) { a.push_back("--js-runtimes"); a.push_back("deno:" + t.deno); }
    for (auto& s : presetArgs(p.preset)) a.push_back(s);
    a.push_back("--");
    a.push_back(p.url);
    return a;
}

// Quotes one argument following the rules CommandLineToArgvW / the MSVC CRT use.
inline std::string quoteArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') out.append(backslashes * 2 + 1, '\\');
        else out.append(backslashes, '\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

inline std::string buildCommandLine(const std::string& exe, const std::vector<std::string>& args) {
    std::string cmd = quoteArg(exe);
    for (auto& a : args) { cmd += ' '; cmd += quoteArg(a); }
    return cmd;
}

enum class LineKind { Progress, Postprocess, Info, File, Error, Other };

struct Line {
    LineKind kind = LineKind::Other;
    // Progress: negative means unknown ("NA").
    std::string status;
    double downloaded = -1, total = -1, speed = -1, eta = -1, fragIndex = -1, fragCount = -1;
    std::string stream;  // "video", "audio" or "" (both / unknown)
    // Postprocess
    std::string postprocessor;
    // Info
    std::string id, playlistIndex, playlistCount, title;
    // Progress filename, File path, Error text
    std::string text;

    double percent() const {
        if (status == "finished") return 100;
        // For HLS/DASH the byte total is only an estimate (and restarts at 0 after a
        // resume), so the fragment counter is the steadier measure.
        if (fragIndex >= 0 && fragCount > 0) return fragIndex * 100.0 / fragCount;
        if (downloaded >= 0 && total > 0) return downloaded * 100.0 / total;
        return -1;
    }
};

// Splits into exactly `count` fields; the last field keeps any remaining '|'.
inline bool splitFields(const std::string& s, size_t count, std::vector<std::string>& out) {
    out.clear();
    size_t start = 0;
    for (size_t k = 0; k + 1 < count; ++k) {
        size_t bar = s.find('|', start);
        if (bar == std::string::npos) return false;
        out.push_back(s.substr(start, bar - start));
        start = bar + 1;
    }
    out.push_back(s.substr(start));
    return true;
}

inline double toNumber(const std::string& s) {
    if (s.empty() || s == "NA") return -1;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? -1 : v;
}

inline Line parseLine(const std::string& raw) {
    Line l;
    std::vector<std::string> f;
    auto has = [&](const char* tag) { return raw.compare(0, std::char_traits<char>::length(tag), tag) == 0; };
    if (has("NDLP|") && splitFields(raw.substr(5), 11, f)) {
        l.kind = LineKind::Progress;
        l.status = f[0];
        l.downloaded = toNumber(f[1]);
        l.total = toNumber(f[2]);
        if (l.total <= 0) l.total = toNumber(f[3]);
        l.speed = toNumber(f[4]);
        l.eta = toNumber(f[5]);
        l.fragIndex = toNumber(f[6]);
        l.fragCount = toNumber(f[7]);
        if (f[8] == "none") l.stream = "audio";
        else if (f[9] == "none") l.stream = "video";
        l.text = f[10];
    } else if (has("NDPP|") && splitFields(raw.substr(5), 2, f)) {
        l.kind = LineKind::Postprocess;
        l.status = f[0];
        l.postprocessor = f[1];
    } else if (has("NDINFO|") && splitFields(raw.substr(7), 4, f)) {
        l.kind = LineKind::Info;
        l.id = f[0];
        l.playlistIndex = f[1];
        l.playlistCount = f[2];
        l.title = f[3];
    } else if (has("NDFILE|")) {
        l.kind = LineKind::File;
        l.text = raw.substr(7);
    } else if (has("ERROR: ")) {
        l.kind = LineKind::Error;
        l.text = raw.substr(7);
    } else {
        l.text = raw;
    }
    return l;
}

// Human-friendly name for a yt-dlp postprocessor key.
inline std::string phaseName(const std::string& pp) {
    if (pp == "Merger") return "Merging";
    if (pp == "ExtractAudio" || pp == "FFmpegExtractAudio") return "Converting audio";
    if (pp.find("Remux") != std::string::npos || pp.find("VideoConvertor") != std::string::npos) return "Remuxing";
    if (pp.find("Metadata") != std::string::npos) return "Writing metadata";
    if (pp.find("Fixup") != std::string::npos) return "Fixing up";
    if (pp == "MoveFiles") return "Finishing";
    return "Processing";
}

// True when `file` sits inside `dir` (Windows paths, case-insensitive) with no
// ".." segments or alternate-stream colons. Guards the cleanup of partial files.
inline bool isInsideDir(const std::string& dir, const std::string& file) {
    auto norm = [](std::string s) {
        for (auto& c : s) {
            if (c == '/') c = '\\';
            else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        }
        return s;
    };
    std::string d = norm(dir), f = norm(file);
    while (!d.empty() && d.back() == '\\') d.pop_back();
    if (d.empty() || f.size() <= d.size() + 1) return false;
    if (f.compare(0, d.size(), d) != 0 || f[d.size()] != '\\') return false;
    if (f.find(':', 2) != std::string::npos) return false;
    size_t pos = 0;
    for (;;) {
        size_t next = f.find('\\', pos);
        std::string seg = f.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (seg == "..") return false;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return true;
}

}  // namespace core
