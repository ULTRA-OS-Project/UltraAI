// BuiltinAdapters.cpp — single registration entry point for compiled-in
// adapters. Factories call EnsureBuiltinAdapters() lazily so adapters
// self-register even when UltraAI is consumed as a static library.
// Each adapter family contributes a Register*Adapters() function, compiled
// in only when its ULTRAAI_ADAPTER_<NAME> CMake option is ON.
// Part of ULTRA OS · MIT license · Cloverleaf UG

namespace UltraAI {
namespace detail {

#if defined(ULTRAAI_HAS_MOCK_ADAPTERS)
void RegisterMockAdapters();   // adapters/mock/MockAdapters.cpp
#endif
#if defined(ULTRAAI_HAS_LOCAL_ADAPTERS)
void RegisterLocalAdapters();  // adapters/local/LocalAdapters.cpp
#endif

void EnsureBuiltinAdapters() {
    static const bool once = [] {
#if defined(ULTRAAI_HAS_MOCK_ADAPTERS)
        RegisterMockAdapters(); // registered first -> stays the default provider
#endif
#if defined(ULTRAAI_HAS_LOCAL_ADAPTERS)
        RegisterLocalAdapters();
#endif
        return true;
    }();
    (void)once;
}

} // namespace detail
} // namespace UltraAI
