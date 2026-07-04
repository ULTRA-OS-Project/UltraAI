# Local adapters — provider `"local"`

Real (non-mock) adapters for **locally running model servers** that expose
the de-facto standard OpenAI-compatible HTTP API. One adapter family covers
every server speaking that protocol:

| Server | Capabilities it can back |
|---|---|
| llama.cpp server | TextLLM, Embeddings, VisionAnalyzer (llava), Translator, CodeAssist |
| Ollama | TextLLM, Embeddings, VisionAnalyzer, Translator, CodeAssist |
| vLLM | TextLLM, Embeddings, Translator, CodeAssist |
| whisper.cpp server / LocalAI | SpeechToText |
| LocalAI / openedai-speech | TextToSpeech (+ voice listing) |
| LocalAI / sd-webui (OpenAI images API) | ImageGen |

Enable with `-DULTRAAI_ADAPTER_LOCAL=ON`, select with
`config.providerId = "local"`. The mock provider remains the default route.

```cpp
TextLLMConfig cfg;
cfg.providerId = "local";
cfg.endpoint   = "http://127.0.0.1:11434"; // e.g. Ollama; default is :8080
cfg.model      = "llama3.2";
auto llm = CreateTextLLM(cfg);
```

## Endpoints used

Relative to `ProviderConfig::endpoint` (default `http://127.0.0.1:8080`):

| Endpoint | Capability |
|---|---|
| `POST /v1/chat/completions` | TextLLM (incl. SSE streaming, tool calls, structured output); also backs VisionAnalyzer, Translator, CodeAssist |
| `POST /v1/embeddings` | Embeddings |
| `POST /v1/audio/transcriptions` | SpeechToText (multipart, `verbose_json` segments/words) |
| `POST /v1/audio/speech` | TextToSpeech (binary response) |
| `GET /v1/audio/voices` | Voice listing (extension; `NotSupported` when absent) |
| `POST /v1/images/generations` | ImageGen (`b64_json`) |

**VideoGen and MusicGen are not registered** — no local server ecosystem has
a settled API for them yet.

## Configuration

| Field | Meaning |
|---|---|
| `endpoint` | Base URL. Loopback only — the transport refuses other hosts. |
| `model` | Forwarded verbatim; `""` uses the server's default model. |
| `options["local.api_key"]` | Optional bearer token some local servers require. Cloud adapters will use `apiKeyRef` + UltraVault instead. |

## Transport note (UltraNet seam)

ULTRA OS policy is that UltraAI never opens sockets itself — transport
belongs to UltraNet. UltraNet does not exist yet, so
`adapters/common/HttpClient` fills the gap for local servers only: plain
HTTP/1.1, **loopback hosts only** (localhost / 127.x / ::1), no TLS. When
UltraNet lands, this implementation is swapped behind the same functions
(`ULTRAAI_HAS_ULTRANET`) without touching adapter code. The SSE parser
(`adapters/common/SseParser`) already follows the split promised in
`UltraNetIntegration.md`: framing in UltraAI, bytes from the transport.

## Behavioural caveats

* **Live STT**: local servers have no live-transcription endpoint, so
  `ISttSession` buffers pushed audio and transcribes once on `Finish()`,
  emitting `FinalSegment` events (no partials).
* **TTS streaming**: `SpeakStream` synthesizes fully, then chunks.
* **ImageGen**: `TextToImage` only for now; edit modes return `NotSupported`.
  The API reports no incremental progress; the callback fires once at 1.0.
* **Vision**: `Caption` / `Tags` / `OCR` / `VQA` via multimodal chat; the
  structured tasks (detection boxes, segmentation, faces, safety scores)
  return `NotSupported` until a suitable local API exists.
* **Translator / CodeAssist** are LLM-backed: quality follows the loaded
  model. `DetectBugs` asks for JSON findings and falls back to plain text
  in `output` if the model doesn't comply.

## Error mapping

Per `UltraNetIntegration.md`: connect/DNS failure → `Network`,
401/403 → `AuthFailure`, 429 → `RateLimited`, other 4xx → `InvalidArgument`,
5xx → `Internal`, socket timeout → `Timeout`.

## Testing

`tests/TestLocalAdapters.cpp` runs the whole adapter family against an
in-process fake server (`tests/FakeLocalServer.h`) bound to an ephemeral
loopback port — CI needs no real model server, no network.

## Smoke test against a real server

`examples/SmokeLocal.cpp` (built with `-DULTRAAI_BUILD_EXAMPLES=ON`) drives
an actual model server: chat, streaming chat, and embeddings. Run it on a
machine with a local server up:

```bash
# llama.cpp:  llama-server -m model.gguf            (port 8080)
./ultraai_smoke_local

# Ollama:     ollama serve                          (port 11434)
./ultraai_smoke_local http://127.0.0.1:11434 llama3.2
```

It prints each step's result and exits non-zero on failure. CI compiles it
but doesn't run it (no model server on CI machines).
