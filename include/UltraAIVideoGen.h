// UltraAIVideoGen.h — video generation and transformation.
#pragma once

#include "UltraAICommon.h"
#include "UltraAIImageGen.h" // JobProgressCallback

#include <functional>
#include <memory>
#include <optional>

namespace UltraAI {

enum class VideoGenMode { TextToVideo, ImageToVideo, VideoToVideo, Interpolation, Upscale };

struct VideoGenRequest {
    VideoGenMode mode = VideoGenMode::TextToVideo;
    std::string  prompt;
    std::string  negativePrompt;
    MediaBlob    initImage;        // ImageToVideo
    MediaBlob    initVideo;        // VideoToVideo / Interpolation / Upscale
    double       durationSeconds = 4.0;
    int          fps    = 24;
    int          width  = 1280;
    int          height = 720;
    bool         withAudio = false; // request an audio track
    std::string  audioPrompt;       // guidance for the audio track
    std::optional<std::uint64_t> seed;
    OptionsMap   options;
};

struct VideoGenResult {
    MediaBlob video;
    MediaBlob audio;   // separate track if the provider returns one
    Error     error;
};

struct VideoGenConfig : ProviderConfig {};

class IVideoGen : public IProvider {
public:
    virtual VideoGenResult Generate(const VideoGenRequest& request,
                                    JobProgressCallback onProgress = nullptr) = 0;
};

using VideoGenFactory = std::function<std::unique_ptr<IVideoGen>(const VideoGenConfig&)>;

std::unique_ptr<IVideoGen> CreateVideoGen(const VideoGenConfig& config, Error* error = nullptr);
std::vector<std::string>   ListVideoGenProviders();
bool RegisterVideoGenProvider(const std::string& name, VideoGenFactory factory,
                              bool makeDefault = false);

} // namespace UltraAI
