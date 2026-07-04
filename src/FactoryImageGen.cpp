#include "UltraAIImageGen.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using ImageGenRegistry = detail::Registry<IImageGen, ImageGenConfig>;

std::unique_ptr<IImageGen> CreateImageGen(const ImageGenConfig& config, Error* error) {
    return ImageGenRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListImageGenProviders() {
    return ImageGenRegistry::Instance().List();
}

bool RegisterImageGenProvider(const std::string& name, ImageGenFactory factory, bool makeDefault) {
    return ImageGenRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
