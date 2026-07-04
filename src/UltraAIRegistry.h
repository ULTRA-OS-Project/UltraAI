// UltraAIRegistry.h — internal, thread-safe provider registry template.
// Not installed; capability factory .cpp files instantiate this.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace UltraAI {
namespace detail {

// Defined in adapters/mock/MockRegistration.cpp (or the no-mock stub).
// Called lazily by every factory so builtin adapters self-register even
// when UltraAI is consumed as a static library.
void EnsureBuiltinAdapters();

template <typename Interface, typename Config>
class Registry {
public:
    using Factory = std::function<std::unique_ptr<Interface>(const Config&)>;

    static Registry& Instance() {
        static Registry instance;
        return instance;
    }

    bool Register(const std::string& name, Factory factory, bool makeDefault) {
        if (name.empty() || !factory) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        factories_[name] = std::move(factory);
        if (makeDefault || defaultName_.empty()) defaultName_ = name;
        return true;
    }

    std::unique_ptr<Interface> Create(const Config& config, Error* error) {
        EnsureBuiltinAdapters();
        Factory factory;
        std::string resolved;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            resolved = config.providerId.empty() ? defaultName_ : config.providerId;
            auto it = factories_.find(resolved);
            if (it != factories_.end()) factory = it->second;
        }
        if (!factory) {
            if (error) {
                *error = Error::Make(ErrorCode::ProviderUnavailable,
                                     resolved.empty()
                                         ? "no provider registered for this capability"
                                         : "provider not registered: " + resolved);
            }
            return nullptr;
        }
        auto instance = factory(config);
        if (!instance && error) {
            *error = Error::Make(ErrorCode::Internal,
                                 "provider factory returned null: " + resolved);
        } else if (error) {
            *error = Error::None_();
        }
        return instance;
    }

    std::vector<std::string> List() {
        EnsureBuiltinAdapters();
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(factories_.size());
        for (const auto& kv : factories_) names.push_back(kv.first);
        return names;
    }

private:
    Registry() = default;
    std::mutex mutex_;
    std::map<std::string, Factory> factories_;
    std::string defaultName_;
};

} // namespace detail
} // namespace UltraAI
