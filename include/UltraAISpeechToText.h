// UltraAISpeechToText.h — file transcription and live microphone streaming.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <memory>

namespace UltraAI {

struct WordTiming {
    std::string word;
    double      startSec = 0.0;
    double      endSec   = 0.0;
};

struct TranscriptSegment {
    std::string             text;
    double                  startSec  = 0.0;
    double                  endSec    = 0.0;
    int                     speakerId = -1;   // -1 -> diarization off/unknown
    std::vector<WordTiming> words;            // filled if word timestamps enabled
};

struct TranscriptionRequest {
    MediaBlob   audio;                 // file transcription input
    std::string languageHint;          // BCP-47, "" -> auto-detect
    bool        enableDiarization     = false;
    bool        enableWordTimestamps  = false;
    OptionsMap  options;
};

struct TranscriptionResult {
    std::string                    text;
    std::vector<TranscriptSegment> segments;
    std::string                    detectedLanguage;
    Error                          error;
};

// Live streaming -----------------------------------------------------------

enum class SttStreamEventKind { PartialText, FinalSegment, Error };

struct SttStreamEvent {
    SttStreamEventKind kind = SttStreamEventKind::PartialText;
    std::string        partialText;   // PartialText
    TranscriptSegment  segment;       // FinalSegment
    Error              error;         // Error
};

using SttStreamCallback = std::function<void(const SttStreamEvent&)>;

// A live microphone session. Push PCM/encoded chunks; Finish() flushes and
// returns the full transcript.
class ISttSession {
public:
    virtual ~ISttSession() = default;
    virtual Error               PushAudio(const std::uint8_t* data, std::size_t size) = 0;
    virtual TranscriptionResult Finish() = 0;
};

struct SpeechToTextConfig : ProviderConfig {};

class ISpeechToText : public IProvider {
public:
    virtual TranscriptionResult Transcribe(const TranscriptionRequest& request) = 0;

    virtual std::unique_ptr<ISttSession> StartStream(const TranscriptionRequest& request,
                                                     SttStreamCallback onEvent,
                                                     Error* error = nullptr) = 0;
};

using SpeechToTextFactory = std::function<std::unique_ptr<ISpeechToText>(const SpeechToTextConfig&)>;

std::unique_ptr<ISpeechToText> CreateSpeechToText(const SpeechToTextConfig& config, Error* error = nullptr);
std::vector<std::string>       ListSpeechToTextProviders();
bool RegisterSpeechToTextProvider(const std::string& name, SpeechToTextFactory factory,
                                  bool makeDefault = false);

} // namespace UltraAI
