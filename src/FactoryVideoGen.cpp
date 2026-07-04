#include "UltraAIVideoGen.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using VideoGenRegistry = detail::Registry<IVideoGen, VideoGenConfig>;

std::unique_ptr<IVideoGen> CreateVideoGen(const VideoGenConfig& config, Error* error) {
    return VideoGenRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListVideoGenProviders() {
    return VideoGenRegistry::Instance().List();
}

bool RegisterVideoGenProvider(const std::string& name, VideoGenFactory factory, bool makeDefault) {
    return VideoGenRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
