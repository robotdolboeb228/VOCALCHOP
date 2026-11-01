#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <cstdint>

class VocalChopProcessor : public juce::AudioProcessor
{
public:
    VocalChopProcessor();
    ~VocalChopProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vocal Chop"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- данные для интерфейса (пишет аудио-поток, читает GUI) ---
    static constexpr int kWaveBins = 512;
    static constexpr double kWaveWindowSec = 2.0;

    std::array<std::atomic<float>, kWaveBins> waveBins {};
    std::atomic<int> waveHead { 0 };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<int> currentStep { -1 };
    std::atomic<float> displayLengthSec { 0.25f };
    std::atomic<float> displayOffsetSec { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Voice
    {
        juce::AudioBuffer<float> data;   // снимок слайса (2 канала)
        int length = 0;
        double pos = 0.0;
        double rate = 1.0;
        bool reversed = false;
        bool active = false;
        bool releasing = false;
        float relGain = 1.0f;
        float gain = 1.0f;
        int note = -1;                   // -1 = триггер от сетки
        int age = 0;
    };

    struct Params
    {
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* swing = nullptr;
        std::atomic<float>* probability = nullptr;
        std::atomic<float>* lengthMs = nullptr;
        std::atomic<float>* lengthSync = nullptr;
        std::atomic<float>* offsetMs = nullptr;
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* reverse = nullptr;
        std::atomic<float>* attackMs = nullptr;
        std::atomic<float>* releaseMs = nullptr;
        std::atomic<float>* dry = nullptr;
        std::atomic<float>* wet = nullptr;
        std::array<std::atomic<float>*, 16> steps {};
    } params;

    void trigger (int note, float velocity, double semitones,
                  int lengthSamples, int offsetSamples, bool reversed);

    static constexpr int kNumVoices = 16;
    static constexpr std::int64_t kInvalidStep = INT64_MIN;

    std::array<Voice, kNumVoices> voices;
    juce::AudioBuffer<float> ring;
    int ringSize = 0;
    int writePos = 0;
    int maxSliceSamples = 0;
    double sr = 44100.0;
    std::int64_t lastStep = kInvalidStep;
    juce::Random rng;

    float waveAccum = 0.0f;
    int waveCount = 0;
    int waveBinSamples = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChopProcessor)
};
