// UltraAIImageGen.h — image generation and editing.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <memory>
#include <optional>

namespace UltraAI {

enum class ImageGenMode { TextToImage, ImageToImage, Inpaint, Outpaint, Upscale, Variation };

struct ControlNetInput {
    std::string type;   // "canny", "depth", "pose", provider-specific
    MediaBlob   image;
    double      weight = 1.0;
};

struct ImageGenRequest {
    ImageGenMode mode = ImageGenMode::TextToImage;
    std::string  prompt;
    std::string  negativePrompt;
    int          width  = 1024;
    int          height = 1024;
    int          count  = 1;
    MediaBlob    initImage;              // ImageToImage / Inpaint / Outpaint / Upscale / Variation
    MediaBlob    maskImage;              // Inpaint
    double       strength = 0.8;         // ImageToImage denoise strength
    std::vector<ControlNetInput> controlNets;
    std::optional<std::uint64_t> seed;
    std::optional<int>           steps;
    OptionsMap   options;
};

// Long-running job progress: fraction in [0,1] plus provider status text.
using JobProgressCallback = std::function<void(double fraction, const std::string& status)>;

struct ImageGenResult {
    std::vector<MediaBlob> images;
    std::uint64_t          seedUsed = 0;
    Error                  error;
};

struct ImageGenConfig : ProviderConfig {};

class IImageGen : public IProvider {
public:
    virtual ImageGenResult Generate(const ImageGenRequest& request,
                                    JobProgressCallback onProgress = nullptr) = 0;
};

using ImageGenFactory = std::function<std::unique_ptr<IImageGen>(const ImageGenConfig&)>;

std::unique_ptr<IImageGen> CreateImageGen(const ImageGenConfig& config, Error* error = nullptr);
std::vector<std::string>   ListImageGenProviders();
bool RegisterImageGenProvider(const std::string& name, ImageGenFactory factory,
                              bool makeDefault = false);

} // namespace UltraAI
