// UltraAIMusicGen.h — music and sound-effect generation.
#pragma once

#include "UltraAICommon.h"
#include "UltraAIImageGen.h" // JobProgressCallback

#include <functional>
#include <map>
#include <memory>

namespace UltraAI {

enum class MusicGenMode { Instrumental, Song, SoundEffect, Continuation };

struct MusicGenRequest {
    MusicGenMode mode = MusicGenMode::Instrumental;
    std::string  prompt;            // style / mood / instrumentation
    std::string  lyrics;            // Song mode; "" -> provider writes lyrics
    double       durationSeconds = 30.0;
    MediaBlob    continuationAudio; // Continuation mode input
    bool         wantStems = false; // request separated stems
    OptionsMap   options;
};

struct MusicGenResult {
    MediaBlob                        audio;      // full mix
    MediaBlob                        vocals;     // Song mode, if separated
    std::map<std::string, MediaBlob> stems;      // "drums", "bass", "vocals", ...
    std::string                      lyricsUsed; // final lyrics (given or generated)
    Error                            error;
};

struct MusicGenConfig : ProviderConfig {};

class IMusicGen : public IProvider {
public:
    virtual MusicGenResult Generate(const MusicGenRequest& request,
                                    JobProgressCallback onProgress = nullptr) = 0;
};

using MusicGenFactory = std::function<std::unique_ptr<IMusicGen>(const MusicGenConfig&)>;

std::unique_ptr<IMusicGen> CreateMusicGen(const MusicGenConfig& config, Error* error = nullptr);
std::vector<std::string>   ListMusicGenProviders();
bool RegisterMusicGenProvider(const std::string& name, MusicGenFactory factory,
                              bool makeDefault = false);

} // namespace UltraAI
