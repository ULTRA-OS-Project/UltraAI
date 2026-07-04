// Base64.cpp
// Part of ULTRA OS · MIT license · Cloverleaf UG
#include "Base64.h"

namespace UltraAI {
namespace base64 {

static const char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Encode(const std::uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= size) {
        unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                     (static_cast<unsigned>(data[i + 1]) << 8) |
                     static_cast<unsigned>(data[i + 2]);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
        i += 3;
    }
    if (i + 1 == size) {
        unsigned v = static_cast<unsigned>(data[i]) << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == size) {
        unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                     (static_cast<unsigned>(data[i + 1]) << 8);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

static int DecodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool Decode(const std::string& text, std::vector<std::uint8_t>* out) {
    out->clear();
    unsigned buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') break;
        int v = DecodeChar(c);
        if (v < 0) return false;
        buffer = (buffer << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return true;
}

} // namespace base64
} // namespace UltraAI
