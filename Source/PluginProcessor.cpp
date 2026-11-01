#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    // длина шага в четвертях: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32
    const double kDivQuarters[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
}

VocalChopProcessor::VocalChopProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    params.mode        = get ("mode");
    params.division    = get ("division");
    params.swing       = get ("swing");
    params.probability = get ("probability");
    params.lengthMs    = get ("lengthMs");
    params.lengthSync  = get ("lengthSync");
    params.offsetMs    = get ("offsetMs");
    params.pitch       = get ("pitch");
    params.reverse     = get ("reverse");
    params.attackMs    = get ("attackMs");
    params.releaseMs   = get ("releaseMs");
    params.dry         = get ("dry");
    params.wet         = get ("wet");

    for (int s = 0; s < 16; ++s)
        params.steps[(size_t) s] = apvts.getRawParameterValue ("step" + juce::String (s + 1));
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalChopProcessor::createLayout()
{
    using P = juce::ParameterID;
    using Range = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        P { "mode", 1 }, "Mode", juce::StringArray { "Grid", "MIDI", "Grid + MIDI" }, 2));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        P { "division", 1 }, "Rate",
        juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "swing", 1 }, "Swing", Range (0.0f, 0.75f, 0.001f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "probability", 1 }, "Probability", Range (0.0f, 1.0f, 0.001f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "lengthMs", 1 }, "Length (ms)", Range (10.0f, 2000.0f, 1.0f, 0.4f), 250.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        P { "lengthSync", 1 }, "Length = Step", true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "offsetMs", 1 }, "Offset (ms back)", Range (0.0f, 1000.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "pitch", 1 }, "Pitch (st)", Range (-24.0f, 24.0f, 0.01f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        P { "reverse", 1 }, "Reverse", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "attackMs", 1 }, "Attack (ms)", Range (0.0f, 200.0f, 0.1f, 0.5f), 2.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "releaseMs", 1 }, "Release (ms)", Range (1.0f, 2000.0f, 1.0f, 0.4f), 30.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "dry", 1 }, "Dry", Range (0.0f, 1.0f, 0.001f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { "wet", 1 }, "Wet", Range (0.0f, 1.0f, 0.001f), 1.0f));

    for (int s = 0; s < 16; ++s)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            P { "step" + juce::String (s + 1), 1 }, "Step " + juce::String (s + 1), true));

    return layout;
}

void VocalChopProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    maxSliceSamples = (int) (2.0 * sr) + 1;
    ringSize = maxSliceSamples + (int) (1.0 * sr) + 16;

    ring.setSize (2, ringSize);
    ring.clear();
    writePos = 0;
    lastStep = kInvalidStep;

    for (auto& b : waveBins)
        b.store (0.0f);

    waveHead.store (0);
    waveAccum = 0.0f;
    waveCount = 0;
    waveBinSamples = juce::jmax (1, (int) (kWaveWindowSec * sr / (double) kWaveBins));
    currentStep.store (-1);
    outputPeak.store (0.0f);

    for (auto& v : voices)
    {
        v.data.setSize (2, maxSliceSamples);
        v.data.clear();
        v.active = false;
        v.releasing = false;
        v.length = 0;
        v.note = -1;
    }
}

bool VocalChopProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void VocalChopProcessor::trigger (int note, float velocity, double semitones,
                                  int lengthSamples, int offsetSamples, bool reversed)
{
    if (ringSize == 0)
        return;

    Voice* target = nullptr;

    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
    {
        int oldest = -1;
        for (auto& v : voices)
            if (v.age > oldest) { oldest = v.age; target = &v; }
    }

    if (target == nullptr)
        return;

    lengthSamples = juce::jlimit (16, maxSliceSamples, lengthSamples);

    const int end = writePos - offsetSamples;
    const int start = end - lengthSamples;

    const float* rl = ring.getReadPointer (0);
    const float* rr = ring.getReadPointer (1);
    float* dl = target->data.getWritePointer (0);
    float* dr = target->data.getWritePointer (1);

    for (int k = 0; k < lengthSamples; ++k)
    {
        int idx = (start + k) % ringSize;
        if (idx < 0) idx += ringSize;
        dl[k] = rl[idx];
        dr[k] = rr[idx];
    }

    target->length = lengthSamples;
    target->pos = 0.0;
    target->rate = std::pow (2.0, juce::jlimit (-48.0, 48.0, semitones) / 12.0);
    target->reversed = reversed;
    target->active = true;
    target->releasing = false;
    target->relGain = 1.0f;
    target->gain = juce::jlimit (0.0f, 1.0f, velocity);
    target->note = note;
    target->age = 0;
}

void VocalChopProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    if (ringSize == 0 || numCh == 0)
        return;

    // --- параметры на блок ---
    const int mode = (int) params.mode->load();
    const bool gridOn = mode != 1;
    const bool midiOn = mode != 0;

    const int div = juce::jlimit (0, 5, (int) params.division->load());
    const double stepQ = kDivQuarters[div];
    const double swing = params.swing->load();
    const float probability = params.probability->load();
    const double pitch = params.pitch->load();
    const bool reversed = params.reverse->load() > 0.5f;
    const float dryGain = params.dry->load();
    const float wetGain = params.wet->load();

    // --- транспорт хоста ---
    bool playing = false;
    double ppq0 = 0.0;
    double bpm = 120.0;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();

            if (auto p = pos->getPpqPosition())
                ppq0 = *p;
            else
                playing = false;

            if (auto b = pos->getBpm())
                bpm = *b;
        }
    }

    if (bpm < 20.0 || bpm > 400.0)
        bpm = 120.0;

    if (! playing)
    {
        lastStep = kInvalidStep;
        currentStep.store (-1);
    }

    const double ppqPerSample = bpm / 60.0 / sr;

    int lengthSamples;
    if (params.lengthSync->load() > 0.5f)
        lengthSamples = (int) (stepQ * 60.0 / bpm * sr);
    else
        lengthSamples = (int) (params.lengthMs->load() * 0.001 * sr);

    lengthSamples = juce::jlimit (16, maxSliceSamples, lengthSamples);

    int offsetSamples = (int) (params.offsetMs->load() * 0.001 * sr);
    offsetSamples = juce::jlimit (0, juce::jmax (0, ringSize - lengthSamples - 4), offsetSamples);

    const double attackSamples = params.attackMs->load() * 0.001 * sr;
    const double relSamples = juce::jmax (1.0, params.releaseMs->load() * 0.001 * sr);
    const float relStep = (float) (1.0 / relSamples);
    const double tailSamples = juce::jmax (16.0, 0.005 * sr);

    displayLengthSec.store ((float) ((double) lengthSamples / sr));
    displayOffsetSec.store ((float) ((double) offsetSamples / sr));
    float blockPeak = 0.0f;

    float* ch0 = buffer.getWritePointer (0);
    float* ch1 = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float* ringL = ring.getWritePointer (0);
    float* ringR = ring.getWritePointer (1);

    auto midiIt = midi.begin();

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = ch0[i];
        const float inR = ch1 != nullptr ? ch1[i] : inL;

        // запись входа в кольцевой буфер
        ringL[writePos] = inL;
        ringR[writePos] = inR;
        if (++writePos >= ringSize)
            writePos = 0;

        // peak-огибающая входа для отрисовки волны
        waveAccum = juce::jmax (waveAccum, std::abs (inL), std::abs (inR));

        if (++waveCount >= waveBinSamples)
        {
            const int h = waveHead.load();
            waveBins[(size_t) h].store (waveAccum);
            waveHead.store ((h + 1) % kWaveBins);
            waveAccum = 0.0f;
            waveCount = 0;
        }

        // --- MIDI ---
        while (midiIt != midi.end() && (*midiIt).samplePosition <= i)
        {
            const auto msg = (*midiIt).getMessage();
            ++midiIt;

            if (! midiOn)
                continue;

            if (msg.isNoteOn())
            {
                trigger (msg.getNoteNumber(), msg.getFloatVelocity(),
                         (double) (msg.getNoteNumber() - 60) + pitch,
                         lengthSamples, offsetSamples, reversed);
            }
            else if (msg.isNoteOff())
            {
                for (auto& v : voices)
                    if (v.active && ! v.releasing && v.note == msg.getNoteNumber())
                    {
                        v.releasing = true;
                        v.relGain = 1.0f;
                    }
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                for (auto& v : voices)
                    if (v.active && ! v.releasing)
                    {
                        v.releasing = true;
                        v.relGain = 1.0f;
                    }
            }
        }

        // --- сетка ---
        if (gridOn && playing)
        {
            const double ppq = ppq0 + (double) i * ppqPerSample;
            const auto n = (std::int64_t) std::floor (ppq / stepQ);

            if (n != lastStep)
            {
                const double swingQ = (n & 1) != 0 ? swing * stepQ : 0.0;
                const double t = (double) n * stepQ + swingQ;

                if (ppq >= t)
                {
                    lastStep = n;

                    const int stepIdx = (int) (((n % 16) + 16) % 16);
                    currentStep.store (stepIdx);

                    if (params.steps[(size_t) stepIdx]->load() > 0.5f
                        && (probability >= 0.999f || rng.nextFloat() < probability))
                        trigger (-1, 1.0f, pitch, lengthSamples, offsetSamples, reversed);
                }
            }
        }

        // --- воспроизведение голосов ---
        float outL = 0.0f, outR = 0.0f;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            const int len = v.length;

            if (len < 2 || v.pos >= (double) (len - 1))
            {
                v.active = false;
                continue;
            }

            const int i0 = (int) v.pos;
            const int i1 = juce::jmin (i0 + 1, len - 1);
            const float fr = (float) (v.pos - (double) i0);

            const int a = v.reversed ? len - 1 - i0 : i0;
            const int b = v.reversed ? len - 1 - i1 : i1;

            const float* dl = v.data.getReadPointer (0);
            const float* dr = v.data.getReadPointer (1);

            const float sl = dl[a] + (dl[b] - dl[a]) * fr;
            const float sr_ = dr[a] + (dr[b] - dr[a]) * fr;

            float env = v.gain;

            if (attackSamples > 0.0 && (double) v.age < attackSamples)
                env *= (float) ((double) v.age / attackSamples);

            const double remaining = ((double) (len - 1) - v.pos) / v.rate;
            env *= (float) juce::jmin (1.0, remaining / tailSamples);

            if (v.releasing)
            {
                v.relGain -= relStep;

                if (v.relGain <= 0.0f)
                {
                    v.active = false;
                    continue;
                }

                env *= v.relGain;
            }

            outL += sl * env;
            outR += sr_ * env;

            v.pos += v.rate;
            ++v.age;
        }

        ch0[i] = inL * dryGain + outL * wetGain;
        blockPeak = juce::jmax (blockPeak, std::abs (ch0[i]));

        if (ch1 != nullptr)
        {
            ch1[i] = inR * dryGain + outR * wetGain;
            blockPeak = juce::jmax (blockPeak, std::abs (ch1[i]));
        }
    }

    outputPeak.store (juce::jmax (outputPeak.load(), blockPeak));

    midi.clear();
}

juce::AudioProcessorEditor* VocalChopProcessor::createEditor()
{
    return new VocalChopEditor (*this);
}

void VocalChopProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalChopProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalChopProcessor();
}
