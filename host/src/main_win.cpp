// NeonDL native messaging host (Windows).
// Brave talks to this over stdin/stdout; it runs yt-dlp and reports progress.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core.hpp"
#include "json.hpp"

namespace {

constexpr const char* kVersion = "0.1.0";

HANDLE g_stdout = nullptr;
std::mutex g_outMutex;
HWND g_parentWindow = nullptr;
std::atomic<bool> g_pickerOpen{false};

// ---------- strings ----------

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------- native messaging I/O ----------

bool writeAll(HANDLE h, const void* data, DWORD size) {
    auto p = static_cast<const char*>(data);
    while (size) {
        DWORD w = 0;
        if (!WriteFile(h, p, size, &w, nullptr) || !w) return false;
        p += w;
        size -= w;
    }
    return true;
}

bool readAll(HANDLE h, void* data, DWORD size) {
    auto p = static_cast<char*>(data);
    while (size) {
        DWORD r = 0;
        if (!ReadFile(h, p, size, &r, nullptr) || !r) return false;
        p += r;
        size -= r;
    }
    return true;
}

void send(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_outMutex);
    uint32_t len = uint32_t(msg.size());
    writeAll(g_stdout, &len, 4);
    writeAll(g_stdout, msg.data(), len);
}

void sendError(const std::string& id, const std::string& message) {
    send(json::Obj().s("type", "error").s("id", id).s("message", message).done());
}

// ---------- tools ----------

std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(sizeof buf / sizeof buf[0]));
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

core::Tools findTools() {
    std::wstring bin = exeDir() + L"\\bin";
    core::Tools t;
    if (fileExists(bin + L"\\yt-dlp.exe")) t.ytdlp = narrow(bin + L"\\yt-dlp.exe");
    if (fileExists(bin + L"\\ffmpeg\\ffmpeg.exe")) t.ffmpegDir = narrow(bin + L"\\ffmpeg");
    if (fileExists(bin + L"\\deno.exe")) t.deno = narrow(bin + L"\\deno.exe");
    return t;
}

std::string downloadsFolder() {
    PWSTR path = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) out = narrow(path);
    CoTaskMemFree(path);
    return out;
}

// ---------- processes ----------

struct Process {
    HANDLE process = nullptr;
    HANDLE jobObject = nullptr;  // kills yt-dlp, its bootstrap child and ffmpeg together
    HANDLE output = nullptr;     // read end of the merged stdout/stderr pipe
};

// Starts `exe` hidden with stdout+stderr on one pipe. Only the pipe and a NUL
// stdin are inherited, so the child can never read Brave's messages.
bool spawn(const std::string& exe, const std::vector<std::string>& args, const std::wstring& cwd,
           Process& out, std::string& error) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) { error = "CreatePipe failed"; return false; }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    HANDLE inherit[2] = {writeEnd, nul};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = writeEnd;
    si.StartupInfo.hStdError = writeEnd;
    si.lpAttributeList = attrs;

    std::wstring app = widen(exe);
    std::wstring cmd = widen(core::buildCommandLine(exe, args));
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                             nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    DWORD lastError = GetLastError();
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(writeEnd);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(readEnd);
        error = "Could not start " + exe + " (error " + std::to_string(lastError) + ")";
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    out.process = pi.hProcess;
    out.jobObject = job;
    out.output = readEnd;
    return true;
}

// Reads the pipe line by line until the process closes it.
template <typename OnLine>
void readLines(HANDLE pipe, OnLine onLine) {
    std::string buf;
    char chunk[8192];
    DWORD n = 0;
    while (ReadFile(pipe, chunk, sizeof chunk, &n, nullptr) && n) {
        buf.append(chunk, n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) onLine(line);
        }
    }
    if (!buf.empty()) onLine(buf);
}

// ---------- downloads ----------

struct Job {
    std::string id;
    std::string dir;
    Process proc;
    std::mutex mutex;
    std::vector<std::string> files;  // destinations yt-dlp started writing
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> cancel{false};
};

std::mutex g_jobsMutex;
std::map<std::string, std::shared_ptr<Job>> g_jobs;

// Deletes the partial/intermediate files of a cancelled download.
void cleanupFiles(const std::string& dir, const std::vector<std::string>& files) {
    for (auto& f : files) {
        if (!core::isInsideDir(dir, f)) continue;
        std::wstring w = widen(f);
        DeleteFileW((w + L".part").c_str());
        DeleteFileW((w + L".ytdl").c_str());
        DeleteFileW(w.c_str());
        size_t slash = w.find_last_of(L'\\');
        std::wstring folder = w.substr(0, slash + 1);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((w + L".part-Frag*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do DeleteFileW((folder + fd.cFileName).c_str());
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
}

void watchJob(std::shared_ptr<Job> job) {
    std::string lastError, lastLine, finalFile, currentFile;
    readLines(job->proc.output, [&](const std::string& raw) {
        core::Line l = core::parseLine(raw);
        switch (l.kind) {
            case core::LineKind::Progress: {
                if (l.status == "downloading" && l.text != currentFile) {
                    currentFile = l.text;
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->files.push_back(l.text);
                    send(json::Obj().s("type", "dest").s("id", job->id).s("file", l.text).done());
                }
                send(json::Obj().s("type", "progress").s("id", job->id).s("status", l.status)
                         .n("pct", l.percent()).n("done", l.downloaded).n("total", l.total)
                         .n("speed", l.speed).n("eta", l.eta).s("stream", l.stream).done());
                break;
            }
            case core::LineKind::Postprocess:
                if (l.status == "started")
                    send(json::Obj().s("type", "phase").s("id", job->id).s("phase", core::phaseName(l.postprocessor)).done());
                break;
            case core::LineKind::Info:
                send(json::Obj().s("type", "info").s("id", job->id).s("title", l.title).s("videoId", l.id)
                         .s("index", l.playlistIndex).s("count", l.playlistCount).done());
                break;
            case core::LineKind::File:
                finalFile = l.text;
                send(json::Obj().s("type", "file").s("id", job->id).s("file", l.text).done());
                break;
            case core::LineKind::Error:
                lastError = l.text;
                break;
            case core::LineKind::Other:
                if (l.text.compare(0, 9, "WARNING: ") != 0) lastLine = l.text;
                break;
        }
    });
    CloseHandle(job->proc.output);
    WaitForSingleObject(job->proc.process, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(job->proc.process, &code);
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        CloseHandle(job->proc.jobObject);  // also kills any leftover ffmpeg
        job->proc.jobObject = nullptr;
    }
    CloseHandle(job->proc.process);

    if (job->stopRequested) {
        if (job->cancel) {
            std::lock_guard<std::mutex> lock(job->mutex);
            cleanupFiles(job->dir, job->files);
        }
        send(json::Obj().s("type", "stopped").s("id", job->id).b("canceled", job->cancel).done());
    } else if (code == 0) {
        send(json::Obj().s("type", "done").s("id", job->id).s("file", finalFile).done());
    } else {
        std::string msg = !lastError.empty() ? lastError
                        : !lastLine.empty()  ? lastLine
                                             : "yt-dlp exited with code " + std::to_string(code);
        sendError(job->id, msg);
    }

    std::lock_guard<std::mutex> lock(g_jobsMutex);
    auto it = g_jobs.find(job->id);
    if (it != g_jobs.end() && it->second == job) g_jobs.erase(it);
}

void startJob(const json::Value& msg) {
    std::string id = msg["id"].str();
    if (id.empty()) return;
    core::StartParams p;
    p.url = msg["url"].str();
    p.dir = msg["dir"].str();
    p.preset = msg["preset"].str("best");
    p.playlist = msg["playlist"].boolean();

    if (!core::isHttpUrl(p.url)) return sendError(id, "Only http(s) links can be downloaded.");
    if (!core::validPreset(p.preset)) return sendError(id, "Unknown quality preset: " + p.preset);
    std::wstring wdir = widen(p.dir);
    if (wdir.empty()) return sendError(id, "No download folder chosen.");
    if (!dirExists(wdir) && SHCreateDirectoryExW(nullptr, wdir.c_str(), nullptr) != ERROR_SUCCESS)
        return sendError(id, "Cannot create folder: " + p.dir);

    core::Tools tools = findTools();
    if (tools.ytdlp.empty()) return sendError(id, "yt-dlp.exe is missing. Run install.cmd again.");

    auto job = std::make_shared<Job>();
    job->id = id;
    job->dir = p.dir;
    {
        std::lock_guard<std::mutex> lock(g_jobsMutex);
        if (g_jobs.count(id)) return;  // already running
        g_jobs[id] = job;
    }
    std::string error;
    if (!spawn(tools.ytdlp, core::buildArgs(p, tools), wdir, job->proc, error)) {
        {
            std::lock_guard<std::mutex> lock(g_jobsMutex);
            g_jobs.erase(id);
        }
        return sendError(id, error);
    }
    send(json::Obj().s("type", "started").s("id", id).done());
    std::thread(watchJob, job).detach();
}

// Pause = stop the process (yt-dlp resumes .part files on the next start).
// Cancel = stop and delete the partial files.
void stopJob(const json::Value& msg) {
    std::string id = msg["id"].str();
    bool cancel = msg["cancel"].boolean();
    std::vector<std::string> known;
    for (auto& f : msg["files"].a) known.push_back(f.str());

    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(g_jobsMutex);
        auto it = g_jobs.find(id);
        if (it != g_jobs.end()) job = it->second;
    }
    if (job) {
        std::lock_guard<std::mutex> lock(job->mutex);
        for (auto& f : known) job->files.push_back(f);
        job->cancel = cancel;
        job->stopRequested = true;
        if (job->proc.jobObject) TerminateJobObject(job->proc.jobObject, 1);
        return;  // watchJob reports "stopped" once the processes are gone
    }
    if (cancel) cleanupFiles(msg["dir"].str(), known);
    send(json::Obj().s("type", "stopped").s("id", id).b("canceled", cancel).done());
}

// ---------- folder picker ----------

void forceForeground(HWND hwnd) {
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD self = GetCurrentThreadId();
    if (fgThread && fgThread != self) AttachThreadInput(self, fgThread, TRUE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (fgThread && fgThread != self) AttachThreadInput(self, fgThread, FALSE);
}

// Brings the dialog in front of Brave as soon as it appears.
class DialogEvents final : public IFileDialogEvents {
    LONG refs_ = 1;
    bool raised_ = false;

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppv = static_cast<IFileDialogEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&refs_);
        if (!r) delete this;
        return ULONG(r);
    }
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog* dlg) override {
        if (raised_) return S_OK;
        raised_ = true;
        IOleWindow* win = nullptr;
        HWND hwnd = nullptr;
        if (SUCCEEDED(dlg->QueryInterface(IID_IOleWindow, reinterpret_cast<void**>(&win)))) {
            win->GetWindow(&hwnd);
            win->Release();
        }
        if (hwnd) forceForeground(hwnd);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE*) override { return S_OK; }
};

std::string showFolderDialog(const std::string& initial) {
    std::string result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&dlg))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"NeonDL — where should this download go?");
    dlg->SetOkButtonLabel(L"Save here");
    std::wstring winit = widen(initial);
    if (!winit.empty() && dirExists(winit)) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(winit.c_str(), nullptr, IID_IShellItem,
                                                  reinterpret_cast<void**>(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    auto events = new DialogEvents();
    DWORD cookie = 0;
    bool advised = SUCCEEDED(dlg->Advise(events, &cookie));
    HWND owner = (g_parentWindow && IsWindow(g_parentWindow)) ? g_parentWindow : nullptr;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) result = narrow(path);
            CoTaskMemFree(path);
            item->Release();
        }
    }
    if (advised) dlg->Unadvise(cookie);
    events->Release();
    dlg->Release();
    return result;
}

void pickFolder(const json::Value& msg) {
    std::string reqId = msg["reqId"].str();
    std::string initial = msg["initial"].str();
    if (g_pickerOpen.exchange(true)) {
        send(json::Obj().s("type", "folder").s("reqId", reqId).null("path").b("busy", true).done());
        return;
    }
    std::thread([reqId, initial] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::string path = showFolderDialog(initial);
        CoUninitialize();
        g_pickerOpen = false;
        json::Obj o;
        o.s("type", "folder").s("reqId", reqId);
        if (path.empty()) o.null("path"); else o.s("path", path);
        send(o.done());
    }).detach();
}

// ---------- misc commands ----------

void hello() {
    core::Tools t = findTools();
    send(json::Obj().s("type", "hello").s("version", kVersion)
             .b("ytdlp", !t.ytdlp.empty()).b("ffmpeg", !t.ffmpegDir.empty()).b("deno", !t.deno.empty())
             .s("downloads", downloadsFolder()).done());
}

// Shows a file in Explorer or opens a folder. Never executes files.
void openPath(const json::Value& msg) {
    std::wstring path = widen(msg["path"].str());
    if (path.empty()) return;
    if (fileExists(path)) {
        std::wstring params = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        size_t slash = path.find_last_of(L"\\/");
        std::wstring dir = dirExists(path) ? path : slash == std::wstring::npos ? L"" : path.substr(0, slash);
        if (!dir.empty() && dirExists(dir)) ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void updateYtdlp() {
    std::thread([] {
        core::Tools t = findTools();
        if (t.ytdlp.empty()) {
            send(json::Obj().s("type", "updated").b("ok", false).s("text", "yt-dlp.exe is missing.").done());
            return;
        }
        Process proc;
        std::string error, text;
        if (!spawn(t.ytdlp, {"-U"}, L"", proc, error)) {
            send(json::Obj().s("type", "updated").b("ok", false).s("text", error).done());
            return;
        }
        readLines(proc.output, [&](const std::string& line) { text = line; });
        CloseHandle(proc.output);
        WaitForSingleObject(proc.process, 120000);
        DWORD code = 1;
        GetExitCodeProcess(proc.process, &code);
        CloseHandle(proc.jobObject);
        CloseHandle(proc.process);
        send(json::Obj().s("type", "updated").b("ok", code == 0).s("text", text).done());
    }).detach();
}

void handle(const std::string& raw) {
    json::Value msg;
    if (!json::parse(raw, msg)) return;
    std::string type = msg["type"].str();
    if (type == "hello") hello();
    else if (type == "start") startJob(msg);
    else if (type == "stop") stopJob(msg);
    else if (type == "pickFolder") pickFolder(msg);
    else if (type == "open") openPath(msg);
    else if (type == "update") updateYtdlp();
}

}  // namespace

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    g_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);

    // Chrome/Brave pass "--parent-window=<HWND>" so dialogs can sit on top of the browser.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        const std::wstring prefix = L"--parent-window=";
        if (a.compare(0, prefix.size(), prefix) == 0)
            g_parentWindow = reinterpret_cast<HWND>(static_cast<intptr_t>(_wtoi64(a.c_str() + prefix.size())));
    }
    LocalFree(argv);

    for (;;) {
        uint32_t len = 0;
        if (!readAll(in, &len, 4) || len > (64u << 20)) break;
        std::string msg(len, '\0');
        if (len && !readAll(in, &msg[0], len)) break;
        handle(msg);
    }
    // Browser closed the port. Exiting closes the job objects, which stops every download;
    // the extension offers to resume them later.
    ExitProcess(0);
}
