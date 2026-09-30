// Minimal JSON reader/writer, just enough for Chrome native messaging.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::vector<std::pair<std::string, Value>> o;

    const Value& operator[](const std::string& key) const {
        static const Value null;
        if (type == Object)
            for (auto& kv : o)
                if (kv.first == key) return kv.second;
        return null;
    }
    std::string str(const std::string& def = "") const { return type == String ? s : def; }
    double num(double def = 0) const { return type == Number ? n : def; }
    bool boolean(bool def = false) const { return type == Bool ? b : def; }
};

namespace detail {

struct Parser {
    const std::string& t;
    size_t i = 0;
    int depth = 0;

    explicit Parser(const std::string& text) : t(text) {}

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& out) {
        if (i + 4 > t.size()) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = t[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') out |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= unsigned(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        if (i >= t.size() || t[i] != '"') return false;
        ++i;
        while (i < t.size()) {
            char c = t[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                        i += 2;
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool value(Value& v) {
        if (++depth > 64) return false;
        ws();
        if (i >= t.size()) return false;
        char c = t[i];
        bool ok = true;
        if (c == '{') {
            v.type = Value::Object;
            ++i;
            ws();
            if (i < t.size() && t[i] == '}') { ++i; }
            else {
                for (;;) {
                    ws();
                    std::string key;
                    if (!string(key)) { ok = false; break; }
                    ws();
                    if (i >= t.size() || t[i] != ':') { ok = false; break; }
                    ++i;
                    Value item;
                    if (!value(item)) { ok = false; break; }
                    v.o.emplace_back(std::move(key), std::move(item));
                    ws();
                    if (i < t.size() && t[i] == ',') { ++i; continue; }
                    if (i < t.size() && t[i] == '}') { ++i; break; }
                    ok = false;
                    break;
                }
            }
        } else if (c == '[') {
            v.type = Value::Array;
            ++i;
            ws();
            if (i < t.size() && t[i] == ']') { ++i; }
            else {
                for (;;) {
                    Value item;
                    if (!value(item)) { ok = false; break; }
                    v.a.push_back(std::move(item));
                    ws();
                    if (i < t.size() && t[i] == ',') { ++i; continue; }
                    if (i < t.size() && t[i] == ']') { ++i; break; }
                    ok = false;
                    break;
                }
            }
        } else if (c == '"') {
            v.type = Value::String;
            ok = string(v.s);
        } else if (t.compare(i, 4, "true") == 0) {
            v.type = Value::Bool; v.b = true; i += 4;
        } else if (t.compare(i, 5, "false") == 0) {
            v.type = Value::Bool; v.b = false; i += 5;
        } else if (t.compare(i, 4, "null") == 0) {
            v.type = Value::Null; i += 4;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = t.c_str() + i;
            char* end = nullptr;
            v.type = Value::Number;
            v.n = std::strtod(start, &end);
            if (end == start) ok = false;
            i += size_t(end - start);
        } else {
            ok = false;
        }
        --depth;
        return ok;
    }
};

}  // namespace detail

inline bool parse(const std::string& text, Value& out) {
    detail::Parser p(text);
    if (!p.value(out)) return false;
    p.ws();
    return p.i == text.size();
}

// Escapes a UTF-8 string for JSON. Invalid UTF-8 bytes become '?', because
// Chrome drops messages that are not valid UTF-8.
inline std::string quote(const std::string& s) {
    std::string out = "\"";
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t n = s.size();
    for (size_t k = 0; k < n; ++k) {
        unsigned char c = p[k];
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else if (c < 0x80) {
            out += char(c);
        } else {
            size_t len = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
            bool valid = len && k + len <= n;
            for (size_t j = 1; valid && j < len; ++j) valid = (p[k + j] & 0xC0) == 0x80;
            if (valid) {
                out.append(s, k, len);
                k += len - 1;
            } else {
                out += '?';
            }
        }
    }
    out += '"';
    return out;
}

// Builds one JSON object: json::Obj().s("type", "hello").n("pct", 1.5).done()
class Obj {
    std::string out_ = "{";
    void key(const char* k) {
        if (out_.size() > 1) out_ += ',';
        out_ += quote(k);
        out_ += ':';
    }

public:
    Obj& s(const char* k, const std::string& v) { key(k); out_ += quote(v); return *this; }
    Obj& n(const char* k, double v) {
        key(k);
        if (std::isfinite(v)) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.15g", v);
            out_ += buf;
        } else {
            out_ += "null";
        }
        return *this;
    }
    Obj& b(const char* k, bool v) { key(k); out_ += v ? "true" : "false"; return *this; }
    Obj& null(const char* k) { key(k); out_ += "null"; return *this; }
    Obj& raw(const char* k, const std::string& rawJson) { key(k); out_ += rawJson; return *this; }
    std::string done() const { return out_ + "}"; }
};

}  // namespace json
