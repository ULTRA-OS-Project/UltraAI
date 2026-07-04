#include "UltraAIMusicGen.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using MusicGenRegistry = detail::Registry<IMusicGen, MusicGenConfig>;

std::unique_ptr<IMusicGen> CreateMusicGen(const MusicGenConfig& config, Error* error) {
    return MusicGenRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListMusicGenProviders() {
    return MusicGenRegistry::Instance().List();
}

bool RegisterMusicGenProvider(const std::string& name, MusicGenFactory factory, bool makeDefault) {
    return MusicGenRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
