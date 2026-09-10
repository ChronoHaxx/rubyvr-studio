// json_scan.cpp — see json_scan.h for the scope of this on purpose.

#include "json_scan.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vr {
namespace json {
namespace {

struct Parser {
    const char* p = nullptr;
    const char* end = nullptr;
    const char* begin = nullptr;
    bool        ok = true;
    std::string error;

    void fail(const char* why) {
        if (!ok) return;   // keep the FIRST error; later ones are consequences
        ok = false;
        error = why;
    }

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool literal(const char* word) {
        const size_t n = std::strlen(word);
        if (static_cast<size_t>(end - p) < n || std::memcmp(p, word, n) != 0)
            return false;
        p += n;
        return true;
    }

    // JSON strings, with the escapes the spec requires. \u is decoded to UTF-8
    // for the basic multilingual plane and surrogate pairs are NOT joined —
    // pokeruby's map data is ASCII identifiers and file paths, so this exists
    // to avoid corrupting a stray accent rather than to be a text pipeline.
    bool parse_string(std::string* out) {
        if (p >= end || *p != '"') { fail("expected a string"); return false; }
        ++p;
        out->clear();
        while (p < end && *p != '"') {
            if (*p != '\\') { out->push_back(*p++); continue; }
            ++p;
            if (p >= end) { fail("string ends inside an escape"); return false; }
            switch (*p) {
                case '"':  out->push_back('"');  ++p; break;
                case '\\': out->push_back('\\'); ++p; break;
                case '/':  out->push_back('/');  ++p; break;
                case 'b':  out->push_back('\b'); ++p; break;
                case 'f':  out->push_back('\f'); ++p; break;
                case 'n':  out->push_back('\n'); ++p; break;
                case 'r':  out->push_back('\r'); ++p; break;
                case 't':  out->push_back('\t'); ++p; break;
                case 'u': {
                    if (end - p < 5) { fail("truncated \\u escape"); return false; }
                    unsigned cp = 0;
                    for (int i = 1; i <= 4; ++i) {
                        const char c = p[i];
                        cp <<= 4;
                        if      (c >= '0' && c <= '9') cp |= static_cast<unsigned>(c - '0');
                        else if (c >= 'a' && c <= 'f') cp |= static_cast<unsigned>(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') cp |= static_cast<unsigned>(c - 'A' + 10);
                        else { fail("bad hex in \\u escape"); return false; }
                    }
                    p += 5;
                    if (cp < 0x80) {
                        out->push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default:
                    fail("unknown escape");
                    return false;
            }
        }
        if (p >= end) { fail("unterminated string"); return false; }
        ++p;   // closing quote
        return true;
    }

    bool parse_value(Value* v) {
        skip_ws();
        if (p >= end) { fail("unexpected end of input"); return false; }

        switch (*p) {
            case '{': return parse_object(v);
            case '[': return parse_array(v);
            case '"':
                v->type = Value::Type::String;
                return parse_string(&v->string);
            case 't':
                if (!literal("true")) { fail("expected true"); return false; }
                v->type = Value::Type::Bool;
                v->boolean = true;
                return true;
            case 'f':
                if (!literal("false")) { fail("expected false"); return false; }
                v->type = Value::Type::Bool;
                v->boolean = false;
                return true;
            case 'n':
                if (!literal("null")) { fail("expected null"); return false; }
                v->type = Value::Type::Null;
                return true;
            default: {
                // Numbers. strtod handles the whole JSON number grammar and
                // then some; the leading-character check above is what stops it
                // from being handed something that is not a number at all.
                char*        stop = nullptr;
                const double d = std::strtod(p, &stop);
                if (stop == p) { fail("expected a value"); return false; }
                v->type = Value::Type::Number;
                v->number = d;
                p = stop;
                return true;
            }
        }
    }

    bool parse_array(Value* v) {
        ++p;   // '['
        v->type = Value::Type::Array;
        skip_ws();
        if (p < end && *p == ']') { ++p; return true; }
        for (;;) {
            v->items.emplace_back();
            if (!parse_value(&v->items.back())) return false;
            skip_ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; return true; }
            fail("expected , or ] in array");
            return false;
        }
    }

    bool parse_object(Value* v) {
        ++p;   // '{'
        v->type = Value::Type::Object;
        skip_ws();
        if (p < end && *p == '}') { ++p; return true; }
        for (;;) {
            skip_ws();
            std::string key;
            if (!parse_string(&key)) return false;
            skip_ws();
            if (p >= end || *p != ':') { fail("expected : after a key"); return false; }
            ++p;
            v->keys.push_back(std::move(key));
            v->values.emplace_back();
            if (!parse_value(&v->values.back())) return false;
            skip_ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; return true; }
            fail("expected , or } in object");
            return false;
        }
    }
};

}  // namespace

const Value* Value::find(const char* key) const {
    if (type != Type::Object) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) return &values[i];
    return nullptr;
}

const char* Value::as_str(const char* def) const {
    return type == Type::String ? string.c_str() : def;
}

int Value::as_int(int def) const {
    return type == Type::Number ? static_cast<int>(number) : def;
}

bool parse_file(const char* path, Value* out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "[json] cannot open %s\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fprintf(stderr, "[json] %s is empty\n", path);
        std::fclose(f);
        return false;
    }
    std::string text(static_cast<size_t>(n), '\0');
    const size_t got = std::fread(text.data(), 1, text.size(), f);
    std::fclose(f);
    if (got != text.size()) {
        std::fprintf(stderr, "[json] short read of %s\n", path);
        return false;
    }

    // SKIP A UTF-8 BOM. Windows editors add one freely — PowerShell's
    // Set-Content -Encoding utf8 does it, and so does Notepad — and without
    // this the parser stops on the very first byte with "expected a value at
    // byte 0", which reads like a corrupt file rather than an encoding. These
    // files are meant to be edited by hand, so the first thing a person does
    // to one must not break it.
    size_t start = 0;
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF
                         && static_cast<uint8_t>(text[1]) == 0xBB
                         && static_cast<uint8_t>(text[2]) == 0xBF)
        start = 3;

    Parser ps;
    ps.begin = text.c_str();
    ps.p     = text.c_str() + start;
    ps.end   = text.c_str() + text.size();

    Value v;
    if (!ps.parse_value(&v) || !ps.ok) {
        std::fprintf(stderr, "[json] %s: %s at byte %ld\n",
                     path, ps.error.c_str(),
                     static_cast<long>(ps.p - ps.begin));
        return false;
    }
    *out = std::move(v);
    return true;
}

}  // namespace json
}  // namespace vr
