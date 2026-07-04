// MiniJson.cpp — recursive-descent JSON parser and serializer.
// Part of ULTRA OS · MIT license · Cloverleaf UG
#include "MiniJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace UltraAI {
namespace minijson {

const Value& Value::Get(const std::string& key) const {
    static const Value kNull;
    if (!IsObject()) return kNull;
    auto it = object_.find(key);
    return it == object_.end() ? kNull : it->second;
}

Value& Value::Set(const std::string& key, Value v) {
    type_ = Type::Object;
    object_[key] = std::move(v);
    return *this;
}

Value& Value::Push(Value v) {
    type_ = Type::Array;
    array_.push_back(std::move(v));
    return *this;
}

// ---------------------------------------------------------------- serialize

static void EscapeTo(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

static void DumpTo(const Value& v, std::string& out) {
    switch (v.type()) {
        case Value::Type::Null:   out += "null"; break;
        case Value::Type::Bool:   out += v.AsBool() ? "true" : "false"; break;
        case Value::Type::Number: {
            double d = v.AsDouble();
            if (std::isfinite(d) && d == std::floor(d) &&
                d >= -9007199254740992.0 && d <= 9007199254740992.0) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.0f", d);
                out += buf;
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", d);
                out += buf;
            }
            break;
        }
        case Value::Type::String: EscapeTo(v.AsString(), out); break;
        case Value::Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& e : v.AsArray()) {
                if (!first) out += ',';
                first = false;
                DumpTo(e, out);
            }
            out += ']';
            break;
        }
        case Value::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : v.AsObject()) {
                if (!first) out += ',';
                first = false;
                EscapeTo(kv.first, out);
                out += ':';
                DumpTo(kv.second, out);
            }
            out += '}';
            break;
        }
    }
}

std::string Value::Dump() const {
    std::string out;
    DumpTo(*this, out);
    return out;
}

// ---------------------------------------------------------------- parse

namespace {

struct Parser {
    const std::string& text;
    std::size_t        pos = 0;
    std::string        error;

    explicit Parser(const std::string& t) : text(t) {}

    bool Fail(const std::string& msg) {
        error = msg + " at offset " + std::to_string(pos);
        return false;
    }

    void SkipWs() {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }

    bool Consume(char c) {
        if (pos < text.size() && text[pos] == c) { ++pos; return true; }
        return false;
    }

    static void AppendUtf8(unsigned cp, std::string& out) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseHex4(unsigned* out) {
        if (pos + 4 > text.size()) return Fail("truncated \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text[pos++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return Fail("bad hex digit in \\u escape");
        }
        *out = v;
        return true;
    }

    bool ParseString(std::string* out) {
        if (!Consume('"')) return Fail("expected '\"'");
        out->clear();
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= text.size()) return Fail("truncated escape");
                char e = text[pos++];
                switch (e) {
                    case '"':  *out += '"';  break;
                    case '\\': *out += '\\'; break;
                    case '/':  *out += '/';  break;
                    case 'b':  *out += '\b'; break;
                    case 'f':  *out += '\f'; break;
                    case 'n':  *out += '\n'; break;
                    case 'r':  *out += '\r'; break;
                    case 't':  *out += '\t'; break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!ParseHex4(&cp)) return false;
                        // Surrogate pair
                        if (cp >= 0xD800 && cp <= 0xDBFF &&
                            pos + 1 < text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
                            pos += 2;
                            unsigned lo = 0;
                            if (!ParseHex4(&lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        AppendUtf8(cp, *out);
                        break;
                    }
                    default: return Fail("unknown escape");
                }
            } else {
                *out += c;
            }
        }
        return Fail("unterminated string");
    }

    bool ParseValue(Value* out) {
        SkipWs();
        if (pos >= text.size()) return Fail("unexpected end of input");
        char c = text[pos];
        if (c == '{') {
            ++pos;
            *out = Value::MakeObject();
            SkipWs();
            if (Consume('}')) return true;
            while (true) {
                SkipWs();
                std::string key;
                if (!ParseString(&key)) return false;
                SkipWs();
                if (!Consume(':')) return Fail("expected ':'");
                Value v;
                if (!ParseValue(&v)) return false;
                out->Set(key, std::move(v));
                SkipWs();
                if (Consume(',')) continue;
                if (Consume('}')) return true;
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++pos;
            *out = Value::MakeArray();
            SkipWs();
            if (Consume(']')) return true;
            while (true) {
                Value v;
                if (!ParseValue(&v)) return false;
                out->Push(std::move(v));
                SkipWs();
                if (Consume(',')) continue;
                if (Consume(']')) return true;
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string s;
            if (!ParseString(&s)) return false;
            *out = Value(std::move(s));
            return true;
        }
        if (text.compare(pos, 4, "true") == 0)  { pos += 4; *out = Value(true);  return true; }
        if (text.compare(pos, 5, "false") == 0) { pos += 5; *out = Value(false); return true; }
        if (text.compare(pos, 4, "null") == 0)  { pos += 4; *out = Value();      return true; }
        // number
        {
            const char* start = text.c_str() + pos;
            char* end = nullptr;
            double d = std::strtod(start, &end);
            if (end == start) return Fail("unexpected character");
            pos += static_cast<std::size_t>(end - start);
            *out = Value(d);
            return true;
        }
    }
};

} // namespace

bool Value::Parse(const std::string& text, Value* out, std::string* error) {
    Parser p(text);
    if (!p.ParseValue(out)) {
        if (error) *error = p.error;
        return false;
    }
    p.SkipWs();
    if (p.pos != text.size()) {
        if (error) *error = "trailing characters at offset " + std::to_string(p.pos);
        return false;
    }
    return true;
}

} // namespace minijson
} // namespace UltraAI
