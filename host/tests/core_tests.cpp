// Unit tests for the platform-independent host logic. Run: ctest (or ./core_tests).
#include <cmath>
#include <cstdio>
#include <string>

#include "core.hpp"
#include "json.hpp"

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static void testQuoteArg() {
    using core::quoteArg;
    CHECK(quoteArg("plain") == "plain");
    CHECK(quoteArg("") == "\"\"");
    CHECK(quoteArg("has space") == "\"has space\"");
    CHECK(quoteArg("C:\\My Videos\\") == "\"C:\\My Videos\\\\\"");
    CHECK(quoteArg("say \"hi\"") == "\"say \\\"hi\\\"\"");
    CHECK(quoteArg("a\\\"b") == "\"a\\\\\\\"b\"");
    CHECK(quoteArg("https://x.com/watch?v=1&list=2") == "https://x.com/watch?v=1&list=2");
}

static void testUrl() {
    CHECK(core::isHttpUrl("https://www.youtube.com/watch?v=abc"));
    CHECK(core::isHttpUrl("HTTP://example.com/a"));
    CHECK(!core::isHttpUrl("file:///C:/Windows"));
    CHECK(!core::isHttpUrl("--exec calc"));
    CHECK(!core::isHttpUrl("https://a.com/ b"));
    CHECK(!core::isHttpUrl("javascript:alert(1)"));
}

static void testArgs() {
    core::StartParams p;
    p.url = "https://example.com/v";
    p.dir = "C:\\Downloads";
    p.preset = "mp3";
    core::Tools t;
    t.ytdlp = "C:\\x\\yt-dlp.exe";
    t.ffmpegDir = "C:\\x\\ffmpeg";
    t.deno = "C:\\x\\deno.exe";
    auto a = core::buildArgs(p, t);
    CHECK(a.size() >= 2);
    CHECK(a[a.size() - 2] == "--");
    CHECK(a.back() == p.url);
    auto has = [&](const std::string& s) {
        for (auto& x : a) if (x == s) return true;
        return false;
    };
    CHECK(has("--no-playlist"));
    CHECK(has("deno:C:\\x\\deno.exe"));
    CHECK(has("C:\\x\\ffmpeg"));
    CHECK(has("mp3"));
    CHECK(core::validPreset("1080"));
    CHECK(!core::validPreset("rm -rf"));
    std::string cmd = core::buildCommandLine(t.ytdlp, a);
    CHECK(cmd.find("\"download:NDLP|") == std::string::npos);  // template has no spaces, stays unquoted
    CHECK(cmd.find("-- https://example.com/v") != std::string::npos);
}

static void testParse() {
    auto l = core::parseLine("NDLP|downloading|1048576|4194304|NA|524288.5|6|NA|NA|none|opus|C:\\d\\a|b.f251.webm");
    CHECK(l.kind == core::LineKind::Progress);
    CHECK(l.status == "downloading");
    CHECK(l.downloaded == 1048576);
    CHECK(l.total == 4194304);
    CHECK(l.speed == 524288.5);
    CHECK(l.eta == 6);
    CHECK(l.stream == "audio");
    CHECK(l.text == "C:\\d\\a|b.f251.webm");
    CHECK(l.percent() == 25);

    l = core::parseLine("NDLP|downloading|100|NA|400.0|NA|NA|NA|NA|avc1|none|x.mp4");
    CHECK(l.total == 400);
    CHECK(l.speed < 0);
    CHECK(l.stream == "video");
    CHECK(l.percent() == 25);

    // Fragment counter wins over a byte estimate (it survives a resume).
    l = core::parseLine("NDLP|downloading|0|NA|1600000|NA|NA|5|10|NA|NA|x.mp4");
    CHECK(l.percent() == 50);

    l = core::parseLine("NDLP|downloading|0|NA|NA|NA|NA|2|8|NA|NA|x.ts");
    CHECK(l.percent() == 25);
    CHECK(l.stream == "");

    l = core::parseLine("NDPP|started|Merger");
    CHECK(l.kind == core::LineKind::Postprocess);
    CHECK(core::phaseName(l.postprocessor) == "Merging");

    l = core::parseLine("NDINFO|dQw4|3|12|Title | with | bars");
    CHECK(l.kind == core::LineKind::Info);
    CHECK(l.id == "dQw4");
    CHECK(l.playlistIndex == "3");
    CHECK(l.playlistCount == "12");
    CHECK(l.title == "Title | with | bars");

    l = core::parseLine("NDFILE|C:\\Downloads\\Song [x].mp3");
    CHECK(l.kind == core::LineKind::File);
    CHECK(l.text == "C:\\Downloads\\Song [x].mp3");

    l = core::parseLine("ERROR: [youtube] abc: Video unavailable");
    CHECK(l.kind == core::LineKind::Error);
    CHECK(l.text == "[youtube] abc: Video unavailable");

    CHECK(core::parseLine("NDLP|broken").kind == core::LineKind::Other);
}

static void testInsideDir() {
    CHECK(core::isInsideDir("C:\\Downloads", "C:\\Downloads\\a.mp4"));
    CHECK(core::isInsideDir("C:\\Downloads\\", "c:\\downloads\\sub\\a.mp4"));
    CHECK(core::isInsideDir("C:/Downloads", "C:\\Downloads\\a.mp4"));
    CHECK(!core::isInsideDir("C:\\Downloads", "C:\\DownloadsX\\a.mp4"));
    CHECK(!core::isInsideDir("C:\\Downloads", "C:\\Downloads\\..\\Windows\\a.dll"));
    CHECK(!core::isInsideDir("C:\\Downloads", "C:\\Downloads\\a.mp4:evil"));
    CHECK(!core::isInsideDir("C:\\Downloads", "C:\\Downloads"));
    CHECK(!core::isInsideDir("", "C:\\a"));
}

static void testJson() {
    json::Value v;
    CHECK(json::parse(R"({"type":"start","id":"j1","playlist":true,"n":-2.5e1,"files":["a","b\\c"],"u":"\u00e9\ud83d\ude00"})", v));
    CHECK(v["type"].str() == "start");
    CHECK(v["playlist"].boolean());
    CHECK(v["n"].num() == -25);
    CHECK(v["files"].a.size() == 2);
    CHECK(v["files"].a[1].str() == "b\\c");
    CHECK(v["u"].str() == "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(v["missing"].str("d") == "d");
    CHECK(!json::parse("{\"a\":}", v));
    CHECK(!json::parse("{\"a\":1} x", v));

    CHECK(json::quote("a\"b\\\n") == "\"a\\\"b\\\\\\n\"");
    CHECK(json::quote("\xC3\xA9") == "\"\xC3\xA9\"");
    CHECK(json::quote("bad\xFF") == "\"bad?\"");
    std::string o = json::Obj().s("type", "x").n("pct", 12.5).n("nan", std::nan("")).b("ok", true).null("p").done();
    CHECK(o == R"({"type":"x","pct":12.5,"nan":null,"ok":true,"p":null})");
}

int main() {
    testQuoteArg();
    testUrl();
    testArgs();
    testParse();
    testInsideDir();
    testJson();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
