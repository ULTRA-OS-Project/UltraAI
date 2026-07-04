# UltraVault — credential storage for UltraAI

## Problem

Cloud adapters need API keys. Keys must never appear in app code, config files,
process arguments, or logs — and apps should not even be able to read them.

## Options considered

| Approach | Verdict |
|---|---|
| Env vars / dotfiles | Rejected: world-readable patterns, leak via `/proc`, no revocation |
| Per-app keychains (libsecret directly) | Rejected: every app re-implements prompting/consent, keys scatter |
| **Central OS vault (UltraVault) with named references** | **Chosen** |

## Model

Apps and UltraAI configs carry only a **named reference**
(`ProviderConfig::apiKeyRef`, e.g. `"anthropic/main"`). Resolution happens
inside the adapter at request time:

```
Adapter ──resolve("anthropic/main")──► UltraVault daemon
                                          │  per-app ACL + user consent
                                          ▼
                                    secret bytes (memory-locked)
```

* Secrets are handed to the adapter as a locked, zero-on-free buffer and used
  immediately to sign/authorize the UltraNet request; they are never returned
  to the app through the UltraAI API.
* First use by a new app triggers a one-time OS consent prompt
  (UltraCanvas dialog): "*App X wants to use the Anthropic key for chat*".
* Backend: OS keyring (kernel keyctl or TPM-sealed file) with per-user
  encryption at rest.
* Revocation and rotation happen in one place; apps are untouched.

## UltraAI touchpoints

* `ProviderConfig.apiKeyRef` — the only credential field in the public API.
* `ErrorCode::AuthFailure` — returned when resolution is denied/missing.
* CMake: `ULTRAAI_USE_ULTRAVAULT` links the client; without it, adapters fall
  back to `endpoint`-local providers (e.g. local llama server) that need no key.

## Status

The lookup seam is wired (`apiKeyRef` + build flag). Blocked on the UltraVault
module itself; mocks ignore credentials entirely.
