// UltraAICommon.h — shared types for all UltraAI capabilities.
// Part of ULTRA OS · MIT license · Cloverleaf UG
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraAI {

// ---------------------------------------------------------------- Options

// Free-form, provider-specific key/value options.
// Keys are namespaced by convention, e.g. "anthropic.top_k", "mock.reply".
using OptionsMap = std::map<std::string, std::string>;

// ---------------------------------------------------------------- Errors

enum class ErrorCode : int {
    None = 0,
    InvalidArgument,
    NotSupported,        // capability/mode not supported by this provider
    ProviderUnavailable, // no such provider registered / not linked
    AuthFailure,         // bad or missing credentials
    Network,             // transport-level failure (via UltraNet)
    RateLimited,
    Timeout,
    Cancelled,
    ContentFiltered,     // provider refused for safety/policy reasons
    Internal
};

struct Error {
    ErrorCode   code = ErrorCode::None;
    std::string message;

    explicit operator bool() const { return code != ErrorCode::None; }

    static Error None_() { return {}; }
    static Error Make(ErrorCode c, std::string msg) { return { c, std::move(msg) }; }
};

const char* ErrorCodeName(ErrorCode code);

// ---------------------------------------------------------------- Media

// A binary payload, either inline (data + mimeType) or by reference (uri).
struct MediaBlob {
    std::vector<std::uint8_t> data;
    std::string               mimeType; // "image/png", "audio/wav", ...
    std::string               uri;      // optional: file:// or https:// reference

    bool empty() const { return data.empty() && uri.empty(); }

    static MediaBlob FromText(const std::string& text, std::string mime) {
        MediaBlob b;
        b.data.assign(text.begin(), text.end());
        b.mimeType = std::move(mime);
        return b;
    }
    std::string AsText() const { return std::string(data.begin(), data.end()); }
};

// ---------------------------------------------------------------- Usage

struct TokenUsage {
    std::uint64_t inputTokens  = 0;
    std::uint64_t outputTokens = 0;
    std::uint64_t totalTokens  = 0;
};

// ---------------------------------------------------------------- Provider config

// Base configuration shared by every capability's Create<Capability>() call.
struct ProviderConfig {
    std::string providerId; // "" -> default registered provider
    std::string model;      // provider-specific model name ("" -> provider default)
    std::string endpoint;   // override base URL (cloud) or socket/path (local)
    std::string apiKeyRef;  // named secret resolved through UltraVault (never a raw key)
    OptionsMap  options;    // provider-specific extras
};

// ---------------------------------------------------------------- Interface base

// Root of every capability interface. RawProvider() is the escape hatch:
// it returns the underlying SDK/client pointer (or nullptr) so power users
// keep full provider-specific feature access.
class IProvider {
public:
    virtual ~IProvider() = default;

    virtual std::string ProviderId() const = 0;
    virtual void*       RawProvider() { return nullptr; }
};

} // namespace UltraAI
