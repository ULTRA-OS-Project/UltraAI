#include "UltraAIEmbeddings.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using EmbeddingsRegistry = detail::Registry<IEmbeddings, EmbeddingsConfig>;

std::unique_ptr<IEmbeddings> CreateEmbeddings(const EmbeddingsConfig& config, Error* error) {
    return EmbeddingsRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListEmbeddingsProviders() {
    return EmbeddingsRegistry::Instance().List();
}

bool RegisterEmbeddingsProvider(const std::string& name, EmbeddingsFactory factory, bool makeDefault) {
    return EmbeddingsRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
