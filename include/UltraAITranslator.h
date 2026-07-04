// UltraAITranslator.h — batched translation and language detection.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <map>
#include <memory>

namespace UltraAI {

enum class Formality { Default, Formal, Informal };

struct TranslateRequest {
    std::vector<std::string>           texts;
    std::string                        sourceLanguage; // "" -> auto-detect per text
    std::string                        targetLanguage; // BCP-47, required
    Formality                          formality = Formality::Default;
    std::map<std::string, std::string> glossary;       // enforced term mappings
    OptionsMap                         options;
};

struct Translation {
    std::string text;
    std::string detectedSourceLanguage; // filled when auto-detected
};

struct TranslateResult {
    std::vector<Translation> translations; // one per input text
    Error                    error;
};

struct LanguageDetection {
    std::string language;    // BCP-47
    double      confidence = 0.0;
    Error       error;
};

struct TranslatorConfig : ProviderConfig {};

class ITranslator : public IProvider {
public:
    virtual TranslateResult   Translate(const TranslateRequest& request) = 0;
    virtual LanguageDetection DetectLanguage(const std::string& text) = 0;
};

using TranslatorFactory = std::function<std::unique_ptr<ITranslator>(const TranslatorConfig&)>;

std::unique_ptr<ITranslator> CreateTranslator(const TranslatorConfig& config, Error* error = nullptr);
std::vector<std::string>     ListTranslatorProviders();
bool RegisterTranslatorProvider(const std::string& name, TranslatorFactory factory,
                                bool makeDefault = false);

} // namespace UltraAI
