#include "UltraAITranslator.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using TranslatorRegistry = detail::Registry<ITranslator, TranslatorConfig>;

std::unique_ptr<ITranslator> CreateTranslator(const TranslatorConfig& config, Error* error) {
    return TranslatorRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListTranslatorProviders() {
    return TranslatorRegistry::Instance().List();
}

bool RegisterTranslatorProvider(const std::string& name, TranslatorFactory factory, bool makeDefault) {
    return TranslatorRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
