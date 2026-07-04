// Base64.h — encode/decode helpers for media payloads in provider protocols.
// Part of ULTRA OS · MIT license · Cloverleaf UG
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraAI {
namespace base64 {

std::string Encode(const std::uint8_t* data, std::size_t size);
inline std::string Encode(const std::vector<std::uint8_t>& data) {
    return Encode(data.data(), data.size());
}

// Ignores whitespace; returns false on any other non-alphabet character.
bool Decode(const std::string& text, std::vector<std::uint8_t>* out);

} // namespace base64
} // namespace UltraAI
