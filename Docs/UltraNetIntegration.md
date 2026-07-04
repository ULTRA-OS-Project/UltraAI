# UltraNet Integration

How UltraAI network adapters (anthropic, openai, local-llama-server, ...) use
UltraNet instead of bundling their own HTTP stack.

## Principle

UltraAI never opens sockets itself. All transport goes through UltraNet so the
OS keeps a single point for TLS policy, proxy configuration, DNS, connection
pooling, and traffic accounting. The dependency is **opt-in**: the core library
(interfaces + registry + mocks) builds with zero network code. Only adapters
enabled via `ULTRAAI_ADAPTER_<NAME>` pull in UltraNet, guarded by
`ULTRAAI_USE_ULTRANET` / `ULTRAAI_HAS_ULTRANET`.

## Transport mapping

| Provider pattern | UltraNet primitive |
|---|---|
| Request/response JSON (chat, embeddings, translate) | `UltraNet::HttpClient` POST |
| Server-sent events (LLM token streaming) | `HttpClient` streaming body + UltraAI SSE parser |
| WebSocket (live STT, realtime voice) | `UltraNet::WebSocket` |
| Large binary upload/download (audio, image, video) | `HttpClient` with chunked bodies and progress callbacks |

The SSE parser lives in UltraAI (`adapters/common/SseParser`), because event
framing is an AI-provider concern, not a transport concern. It consumes raw
byte callbacks from UltraNet and emits `StreamEvent`s.

## Threading model

* Adapter calls are **synchronous from the caller's view**; blocking variants
  (`Chat`) run the UltraNet request on the calling thread.
* Streaming variants (`ChatStream`, `SpeakStream`, `StartStream`) drive
  callbacks **on an adapter-owned worker thread**. Callbacks must therefore be
  thread-safe or marshal to the UI thread (UltraCanvas provides
  `PostToUiThread` for widgets).
* Cancellation: destroying the returned session/interface object cancels the
  underlying UltraNet request.

## Error mapping

| Transport condition | `UltraAI::ErrorCode` |
|---|---|
| DNS/connect/TLS failure | `Network` |
| HTTP 401/403 | `AuthFailure` |
| HTTP 429 | `RateLimited` |
| Deadline exceeded | `Timeout` |
| HTTP 4xx (other) | `InvalidArgument` |
| HTTP 5xx | `Internal` |
| Provider safety refusal | `ContentFiltered` |

Retries with exponential backoff + jitter are handled once, in the shared
adapter runtime — individual adapters only declare which operations are
idempotent and therefore retryable.

## Adapter checklist

1. Add `ULTRAAI_ADAPTER_<NAME>` CMake option (default OFF).
2. Guard sources with `ULTRAAI_HAS_ULTRANET`.
3. Resolve credentials only through `apiKeyRef` -> UltraVault (never raw keys).
4. Implement the capability interface(s); register via
   `Register<Capability>Provider("<name>", factory)` inside
   `EnsureBuiltinAdapters()` or a dedicated registration TU.
5. Map transport errors per the table above.
6. Expose the underlying client via `RawProvider()`.
7. Provide record/replay fixtures so CI runs without network.
