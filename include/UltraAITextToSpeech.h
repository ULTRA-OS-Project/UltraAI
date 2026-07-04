// UltraAITextToSpeech.h — synthesis, voice listing, streamed playback, cloning.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <memory>

namespace UltraAI {

struct Voice {
    std::string id;
    std::string name;
    std::string language;    // BCP-47
    std::string gender;      // "male" | "female" | "neutral" | ""
    std::string description;
    bool        cloned = false;
};

struct SpeakRequest {
    std::string text;
    std::string voiceId;      // "" -> provider default voice
    std::string audioFormat = "audio/wav"; // requested output MIME
    double      speed = 1.0;
    double      pitch = 0.0;  // semitones
    OptionsMap  options;
};

struct SpeakResult {
    MediaBlob audio;
    Error     error;
};

// Chunked streaming playback: audio arrives in playable chunks; the final
// call has `last == true`.
using TtsChunkCallback = std::function<void(const MediaBlob& chunk, bool last)>;

struct TextToSpeechConfig : ProviderConfig {};

class ITextToSpeech : public IProvider {
public:
    virtual std::vector<Voice> ListVoices(Error* error = nullptr) = 0;

    virtual SpeakResult Speak(const SpeakRequest& request) = 0;

    virtual Error SpeakStream(const SpeakRequest& request, TtsChunkCallback onChunk) = 0;

    // Voice cloning from reference samples. Providers without cloning return
    // ErrorCode::NotSupported.
    virtual Voice CloneVoice(const std::string& name,
                             const std::vector<MediaBlob>& samples,
                             Error* error = nullptr) = 0;
};

using TextToSpeechFactory = std::function<std::unique_ptr<ITextToSpeech>(const TextToSpeechConfig&)>;

std::unique_ptr<ITextToSpeech> CreateTextToSpeech(const TextToSpeechConfig& config, Error* error = nullptr);
std::vector<std::string>       ListTextToSpeechProviders();
bool RegisterTextToSpeechProvider(const std::string& name, TextToSpeechFactory factory,
                                  bool makeDefault = false);

} // namespace UltraAI
