#pragma once

#include "PluginProcessor.h"

// Интерфейс по макету: тёмная тема, неон magenta -> cyan.
// Рисуется в фиксированных координатах 1683x935 и масштабируется под размер окна.
class VocalChopEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalChopEditor (VocalChopProcessor&);
    ~VocalChopEditor() override;

    void resized() override;

private:
    class Content;

    VocalChopProcessor& proc;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopEditor)
};
