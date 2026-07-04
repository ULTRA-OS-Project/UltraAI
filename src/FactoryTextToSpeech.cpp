#include "UltraAITextToSpeech.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using TextToSpeechRegistry = detail::Registry<ITextToSpeech, TextToSpeechConfig>;

std::unique_ptr<ITextToSpeech> CreateTextToSpeech(const TextToSpeechConfig& config, Error* error) {
    return TextToSpeechRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListTextToSpeechProviders() {
    return TextToSpeechRegistry::Instance().List();
}

bool RegisterTextToSpeechProvider(const std::string& name, TextToSpeechFactory factory, bool makeDefault) {
    return TextToSpeechRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
