#include "PluginEditor.h"

#include <cmath>
#include <functional>

namespace
{
    constexpr float kDesignW = 1683.0f;
    constexpr float kDesignH = 935.0f;

    namespace ui
    {
        const juce::Colour bg       { 0xff08080d };
        const juce::Colour panel    { 0xff0e0e15 };
        const juce::Colour edge     { 0xff252533 };
        const juce::Colour magenta  { 0xffe254f5 };
        const juce::Colour violet   { 0xff8f6df5 };
        const juce::Colour blue     { 0xff5b8cf8 };
        const juce::Colour cyan     { 0xff22d3ee };
        const juce::Colour skyBlue  { 0xff29b6f6 };
        const juce::Colour textDim  { 0xffc4c4d2 };
        const juce::Colour textFaint{ 0xff6f6f84 };

        juce::Font font (float h, bool bold = false)
        {
            return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
        }
    }

    //==========================================================================
    class ChopLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                               float a0, float a1, juce::Slider& s) override
        {
            const auto c1 = s.findColour (juce::Slider::rotarySliderFillColourId);
            const auto c2 = s.findColour (juce::Slider::thumbColourId);

            const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            const float d = juce::jmin (area.getWidth(), area.getHeight()) - 6.0f;
            const auto ctr = area.getCentre();
            const float R = d * 0.5f;
            const float arcR = R - 2.0f;
            const float bodyR = R * 0.80f;
            const float angle = a0 + pos * (a1 - a0);
            const float stroke = juce::jmax (3.0f, R * 0.07f);
            const juce::PathStrokeType::JointStyle js = juce::PathStrokeType::curved;
            const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;

            juce::Path track;
            track.addCentredArc (ctr.x, ctr.y, arcR, arcR, 0.0f, a0, a1, true);
            g.setColour (juce::Colour (0xff22222e));
            g.strokePath (track, juce::PathStrokeType (stroke, js, cap));

            if (pos > 0.003f)
            {
                juce::Path val;
                val.addCentredArc (ctr.x, ctr.y, arcR, arcR, 0.0f, a0, angle, true);

                g.setColour (c1.withAlpha (0.22f));
                g.strokePath (val, juce::PathStrokeType (stroke * 2.6f, js, cap));

                g.setGradientFill (juce::ColourGradient (c1, ctr.x - arcR, ctr.y,
                                                         c2, ctr.x + arcR, ctr.y, false));
                g.strokePath (val, juce::PathStrokeType (stroke, js, cap));
            }

            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff262632), ctr.x, ctr.y - bodyR,
                                                     juce::Colour (0xff09090e), ctr.x, ctr.y + bodyR, false));
            g.fillEllipse (ctr.x - bodyR, ctr.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

            g.setColour (juce::Colour (0xff30303f));
            g.drawEllipse (ctr.x - bodyR, ctr.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.4f);

            juce::Path pointer;
            pointer.addRoundedRectangle (-stroke * 0.4f, -bodyR + 5.0f, stroke * 0.8f, bodyR * 0.42f, stroke * 0.4f);
            g.setColour (juce::Colours::white);
            g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (ctr.x, ctr.y));
        }
    };

    //==========================================================================
    // Ручка с названием сверху и значением в «таблетке» снизу.
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
              const juce::String& titleText, juce::Colour c1, juce::Colour c2,
              std::function<juce::String (double)> fmt, juce::LookAndFeel& laf,
              juce::StringArray ring = {})
            : title (titleText), ringLabels (ring), accentA (c1), accentB (c2),
              attach (apvts, paramID, slider)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                        juce::MathConstants<float>::pi * 2.75f, true);
            slider.setColour (juce::Slider::rotarySliderFillColourId, c1);
            slider.setColour (juce::Slider::thumbColourId, c2);
            slider.setLookAndFeel (&laf);
            slider.textFromValueFunction = std::move (fmt);   // после attach: перекрываем его текст
            slider.onValueChange = [this] { repaint(); };
            addAndMakeVisible (slider);
        }

        ~Knob() override { slider.setLookAndFeel (nullptr); }

        void resized() override
        {
            const int titleH = title.isEmpty() ? 0 : 30;
            const int pillH = 40;
            const int pad = ringLabels.isEmpty() ? 0 : 66;

            auto area = getLocalBounds().withTrimmedTop (titleH).withTrimmedBottom (pillH).reduced (pad, 0);
            const int d = juce::jmin (area.getWidth(), area.getHeight());
            knobBounds = area.withSizeKeepingCentre (d, d);
            slider.setBounds (knobBounds);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (ui::textDim);
            g.setFont (ui::font (23.0f));
            g.drawText (title, 0, 0, getWidth(), 30, juce::Justification::centred);

            if (! ringLabels.isEmpty())
            {
                const int n = ringLabels.size();
                const int sel = juce::roundToInt ((float) slider.getValue());
                const float a0 = juce::MathConstants<float>::pi * 1.25f;
                const float a1 = juce::MathConstants<float>::pi * 2.75f;
                const auto ctr = knobBounds.toFloat().getCentre();
                const float radius = (float) knobBounds.getWidth() * 0.5f + 22.0f;

                g.setFont (ui::font (21.0f));

                for (int i = 0; i < n; ++i)
                {
                    const float a = a0 + (a1 - a0) * (float) i / (float) (n - 1);
                    const auto p = juce::Point<float> (ctr.x + std::sin (a) * radius,
                                                       ctr.y - std::cos (a) * radius);
                    g.setColour (i == sel ? accentA : ui::textFaint);
                    g.drawText (ringLabels[i],
                                juce::Rectangle<float> (64.0f, 26.0f).withCentre (p).toNearestInt(),
                                juce::Justification::centred);
                }
            }

            auto pill = juce::Rectangle<float> (juce::jmin (140.0f, (float) getWidth() * 0.9f), 32.0f)
                            .withCentre ({ (float) getWidth() * 0.5f, (float) getHeight() - 20.0f });

            g.setColour (juce::Colour (0xff0b0b11));
            g.fillRoundedRectangle (pill, 7.0f);
            g.setColour (juce::Colour (0xff30303f));
            g.drawRoundedRectangle (pill, 7.0f, 1.3f);

            g.setColour (accentA);
            g.setFont (ui::font (26.0f));
            g.drawText (slider.getTextFromValue (slider.getValue()), pill.toNearestInt(),
                        juce::Justification::centred);
        }

    private:
        juce::String title;
        juce::StringArray ringLabels;
        juce::Colour accentA, accentB;
        juce::Rectangle<int> knobBounds;
        juce::Slider slider;
        juce::AudioProcessorValueTreeState::SliderAttachment attach;
    };

    //==========================================================================
    class ModeSelector : public juce::Component
    {
    public:
        explicit ModeSelector (juce::AudioProcessorValueTreeState& apvts)
            : attach (*apvts.getParameter ("mode"), [this] (float v)
                      {
                          index = juce::jlimit (0, 2, juce::roundToInt (v));
                          repaint();
                      })
        {
            attach.sendInitialUpdate();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int idx = juce::jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.0f)));
            attach.setValueAsCompleteGesture ((float) idx);
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat().reduced (2.0f);
            const float r = 16.0f;

            g.setColour (juce::Colour (0xff0b0b11));
            g.fillRoundedRectangle (b, r);
            g.setColour (juce::Colour (0xff2a2a38));
            g.drawRoundedRectangle (b, r, 1.5f);

            const float segW = b.getWidth() / 3.0f;
            const char* names[] = { "GRID", "MIDI", "GRID+MIDI" };

            for (int i = 0; i < 3; ++i)
            {
                const auto seg = juce::Rectangle<float> (b.getX() + (float) i * segW, b.getY(), segW, b.getHeight());

                if (i == index)
                {
                    const auto sel = seg.reduced (3.0f);
                    g.setColour (ui::magenta.withAlpha (0.18f));
                    g.drawRoundedRectangle (sel.expanded (2.0f), r, 5.0f);
                    g.setColour (juce::Colour (0xff1a1226));
                    g.fillRoundedRectangle (sel, r - 3.0f);
                    g.setGradientFill (juce::ColourGradient (ui::magenta, sel.getX(), 0.0f,
                                                             ui::violet, sel.getRight(), 0.0f, false));
                    g.drawRoundedRectangle (sel, r - 3.0f, 2.2f);
                }
                else if (i > 0 && i - 1 != index)
                {
                    g.setColour (juce::Colour (0xff22222e));
                    g.drawLine (seg.getX(), seg.getY() + 12.0f, seg.getX(), seg.getBottom() - 12.0f, 1.2f);
                }

                g.setColour (i == index ? juce::Colours::white : ui::textDim);
                g.setFont (ui::font (27.0f));
                g.drawText (names[i], seg.toNearestInt(), juce::Justification::centred);
            }
        }

    private:
        int index = 2;
        juce::ParameterAttachment attach;
    };

    //==========================================================================
    class ReverseButton : public juce::Button
    {
    public:
        explicit ReverseButton (juce::AudioProcessorValueTreeState& apvts)
            : juce::Button ("reverse"), attach (apvts, "reverse", *this)
        {
            setClickingTogglesState (true);
        }

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto b = getLocalBounds().toFloat().reduced (4.0f);
            const float r = b.getHeight() * 0.5f;
            const bool on = getToggleState();

            if (on)
            {
                g.setColour (ui::magenta.withAlpha (0.16f));
                g.drawRoundedRectangle (b.expanded (2.0f), r, 6.0f);
                g.setColour (juce::Colour (0xff1a1226));
                g.fillRoundedRectangle (b, r);
                g.setGradientFill (juce::ColourGradient (ui::magenta, b.getX(), 0.0f,
                                                         ui::cyan, b.getRight(), 0.0f, false));
                g.drawRoundedRectangle (b, r, 2.2f);
            }
            else
            {
                g.setColour (juce::Colour (0xff0b0b11));
                g.fillRoundedRectangle (b, r);
                g.setColour (over ? juce::Colour (0xff4a4a60) : juce::Colour (0xff30303f));
                g.drawRoundedRectangle (b, r, 1.8f);
            }

            // две стрелки «назад»
            const float cy = b.getCentreY();
            const float x0 = b.getX() + b.getWidth() * 0.22f;
            g.setColour (on ? juce::Colours::white : ui::textDim);

            for (int k = 0; k < 2; ++k)
            {
                juce::Path tri;
                const float x = x0 + (float) k * 15.0f;
                tri.addTriangle (x + 15.0f, cy - 10.0f, x + 15.0f, cy + 10.0f, x, cy);
                g.fillPath (tri);
            }

            g.setFont (ui::font (27.0f));
            g.drawText ("REVERSE", b.withTrimmedLeft (b.getWidth() * 0.34f).toNearestInt(),
                        juce::Justification::centredLeft);
        }

    private:
        juce::AudioProcessorValueTreeState::ButtonAttachment attach;
    };

    //==========================================================================
    class SwitchToggle : public juce::Button
    {
    public:
        SwitchToggle (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& text)
            : juce::Button (id), label (text), attach (apvts, id, *this)
        {
            setClickingTogglesState (true);
        }

        void paintButton (juce::Graphics& g, bool, bool) override
        {
            const bool on = getToggleState();
            const auto sw = juce::Rectangle<float> (60.0f, 32.0f)
                                .withCentre ({ 40.0f, (float) getHeight() * 0.5f });

            g.setColour (on ? ui::cyan.withAlpha (0.14f) : juce::Colour (0xff0b0b11));
            g.fillRoundedRectangle (sw, 16.0f);
            g.setColour (on ? ui::cyan : juce::Colour (0xff3a3a4c));
            g.drawRoundedRectangle (sw, 16.0f, 2.0f);

            const float kx = on ? sw.getRight() - 17.0f : sw.getX() + 17.0f;
            g.setColour (on ? ui::cyan : ui::textFaint);
            g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ kx, sw.getCentreY() }));

            g.setColour (ui::textDim);
            g.setFont (ui::font (26.0f));
            g.drawText (label, getLocalBounds().withTrimmedLeft (84), juce::Justification::centredLeft);
        }

    private:
        juce::String label;
        juce::AudioProcessorValueTreeState::ButtonAttachment attach;
    };

    //==========================================================================
    class StepPad : public juce::Button, private juce::Timer
    {
    public:
        StepPad (int idx, VocalChopProcessor& p)
            : juce::Button ("step" + juce::String (idx + 1)), index (idx), proc (p),
              attach (p.apvts, "step" + juce::String (idx + 1), *this)
        {
            setClickingTogglesState (true);
            startTimerHz (30);
        }

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto b = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
            const auto c = index < 8 ? ui::magenta : ui::skyBlue;
            const bool on = getToggleState();
            const bool playing = proc.currentStep.load() == index;

            if (on)
            {
                for (int k = 3; k >= 1; --k)
                {
                    g.setColour (c.withAlpha (0.07f));
                    g.drawRoundedRectangle (b.expanded ((float) k * 1.8f), 8.0f + (float) k * 1.8f, 2.4f);
                }

                g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), 0.0f, b.getY(),
                                                         c.darker (0.15f), 0.0f, b.getBottom(), false));
                g.fillRoundedRectangle (b, 7.0f);
            }
            else
            {
                g.setColour (playing ? c.withAlpha (0.28f) : juce::Colour (0xff101018));
                g.fillRoundedRectangle (b, 7.0f);
                g.setColour (over ? juce::Colour (0xff444458) : juce::Colour (0xff2a2a38));
                g.drawRoundedRectangle (b, 7.0f, 1.4f);
            }

            if (playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (b.expanded (1.5f), 8.5f, 2.2f);
            }
        }

    private:
        void timerCallback() override
        {
            const bool pl = proc.currentStep.load() == index;

            if (pl != wasPlaying)
            {
                wasPlaying = pl;
                repaint();
            }
        }

        int index;
        VocalChopProcessor& proc;
        bool wasPlaying = false;
        juce::AudioProcessorValueTreeState::ButtonAttachment attach;
    };

    class StepRow : public juce::Component
    {
    public:
        explicit StepRow (VocalChopProcessor& p)
        {
            for (int i = 0; i < 16; ++i)
            {
                pads[(size_t) i] = std::make_unique<StepPad> (i, p);
                addAndMakeVisible (*pads[(size_t) i]);
            }
        }

        void resized() override
        {
            for (int i = 0; i < 16; ++i)
                pads[(size_t) i]->setBounds (cell (i).withY (30).withHeight (70));
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (ui::textDim);
            g.setFont (ui::font (21.0f));

            for (int i = 0; i < 16; ++i)
                g.drawText (juce::String (i + 1), cell (i).withHeight (28), juce::Justification::centred);

            const float y = 124.0f;
            const float w = (float) getWidth();
            g.setColour (juce::Colour (0xff2e2e3c));
            g.drawLine (6.0f, y, w * 0.5f - 90.0f, y, 1.4f);
            g.drawLine (w * 0.5f + 90.0f, y, w - 6.0f, y, 1.4f);
            g.drawLine (6.0f, y - 7.0f, 6.0f, y + 7.0f, 1.4f);
            g.drawLine (w - 6.0f, y - 7.0f, w - 6.0f, y + 7.0f, 1.4f);

            g.setColour (ui::textFaint);
            g.setFont (ui::font (23.0f));
            g.drawText ("16 STEPS", juce::Rectangle<int> (getWidth() / 2 - 85, (int) y - 14, 170, 28),
                        juce::Justification::centred);
        }

    private:
        juce::Rectangle<int> cell (int i) const
        {
            const float cw = (float) getWidth() / 16.0f;
            return { juce::roundToInt ((float) i * cw), 0, juce::roundToInt (cw), getHeight() };
        }

        std::array<std::unique_ptr<StepPad>, 16> pads;
    };

    //==========================================================================
    class WaveformView : public juce::Component, private juce::Timer
    {
    public:
        explicit WaveformView (VocalChopProcessor& p) : proc (p) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            const auto full = getLocalBounds().toFloat();
            const auto wave = full.withTrimmedBottom (36.0f);

            constexpr int N = VocalChopProcessor::kWaveBins;
            const int head = proc.waveHead.load();
            const float binW = wave.getWidth() / (float) N;
            const float midY = wave.getCentreY();
            const float amp = wave.getHeight() * 0.46f;

            const float win = (float) VocalChopProcessor::kWaveWindowSec;
            const float len = proc.displayLengthSec.load();
            const float off = proc.displayOffsetSec.load();

            const float xEnd = juce::jlimit (wave.getX(), wave.getRight(),
                                             wave.getRight() - off / win * wave.getWidth());
            const float xStart = juce::jlimit (wave.getX(), wave.getRight(),
                                               xEnd - len / win * wave.getWidth());
            const auto region = juce::Rectangle<float> (xStart, wave.getY() + 10.0f,
                                                        juce::jmax (3.0f, xEnd - xStart),
                                                        wave.getHeight() - 20.0f);

            auto drawBars = [&]
            {
                for (int i = 0; i < N; ++i)
                {
                    const float v = proc.waveBins[(size_t) ((head + i) % N)].load();
                    const float h = juce::jmax (1.0f, amp * std::pow (juce::jmin (1.0f, v * 1.6f), 0.6f));
                    g.fillRoundedRectangle (wave.getX() + (float) i * binW, midY - h,
                                            binW * 0.72f, h * 2.0f, binW * 0.3f);
                }
            };

            g.setGradientFill (juce::ColourGradient (ui::magenta.withAlpha (0.10f), region.getX(), 0.0f,
                                                     ui::cyan.withAlpha (0.10f), region.getRight(), 0.0f, false));
            g.fillRect (region);

            g.setColour (ui::violet.withAlpha (0.42f));
            drawBars();

            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (region.toNearestInt());
                g.setGradientFill (juce::ColourGradient (ui::magenta, region.getX(), 0.0f,
                                                         ui::cyan, region.getRight(), 0.0f, false));
                drawBars();
            }

            g.setGradientFill (juce::ColourGradient (ui::magenta, region.getX(), 0.0f,
                                                     ui::cyan, region.getRight(), 0.0f, false));
            g.drawRect (region, 1.6f);
            g.setColour (ui::magenta);
            g.fillRect (xStart - 6.0f, region.getY() - 10.0f, 12.0f, 12.0f);
            g.setColour (ui::cyan);
            g.fillRect (xEnd - 6.0f, region.getY() - 10.0f, 12.0f, 12.0f);

            g.setColour (ui::textDim);
            g.setFont (ui::font (24.0f));
            g.drawText ("slice " + juce::String (len, 3) + " s   /   offset " + juce::String (off, 3) + " s",
                        full.withTop (full.getBottom() - 34.0f).toNearestInt(),
                        juce::Justification::centred);
        }

    private:
        void timerCallback() override { repaint(); }

        VocalChopProcessor& proc;
    };

    //==========================================================================
    class MeterView : public juce::Component, private juce::Timer
    {
    public:
        explicit MeterView (VocalChopProcessor& p) : proc (p) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            constexpr int segs = 32;
            constexpr float barX = 120.0f, barW = 314.0f, barY = 20.0f, barH = 26.0f;
            constexpr float dbMin = -24.0f, dbMax = 3.0f;

            g.setColour (ui::textDim);
            g.setFont (ui::font (24.0f));
            g.drawText ("OUTPUT", juce::Rectangle<int> (0, 14, 108, 38), juce::Justification::centredRight);

            const float segW = barW / (float) segs;

            for (int i = 0; i < segs; ++i)
            {
                const float t = (float) i / (float) (segs - 1);
                const float segDb = dbMin + ((float) i + 0.5f) / (float) segs * (dbMax - dbMin);
                const auto c = ui::magenta.interpolatedWith (ui::cyan, t);

                g.setColour (segDb <= level ? c : c.withAlpha (0.14f));
                g.fillRoundedRectangle (barX + (float) i * segW + 1.0f, barY, segW - 3.0f, barH, 2.0f);
            }

            g.setColour (ui::textDim);
            g.setFont (ui::font (20.0f));
            const float marks[] = { -24.0f, -12.0f, -6.0f, -3.0f, 0.0f, 3.0f };

            for (float m : marks)
            {
                const float x = barX + (m - dbMin) / (dbMax - dbMin) * barW;
                const juce::String text = m > 0.0f ? "+" + juce::String ((int) m) : juce::String ((int) m);
                g.drawText (text, juce::Rectangle<float> (50.0f, 24.0f).withCentre ({ x, 62.0f }).toNearestInt(),
                            juce::Justification::centred);
            }

            g.setFont (ui::font (26.0f));
            g.drawText (level > -60.0f ? juce::String (level, 1) + " dB" : juce::String ("-inf dB"),
                        juce::Rectangle<int> (452, 14, 88, 38), juce::Justification::centredLeft);
        }

    private:
        void timerCallback() override
        {
            const float peak = proc.outputPeak.exchange (0.0f);
            const float db = 20.0f * std::log10 (juce::jmax (peak, 1.0e-5f));
            level = db > level ? db : juce::jmax (db, level - 1.5f);
            repaint();
        }

        VocalChopProcessor& proc;
        float level = -100.0f;
    };
}

//==============================================================================
class VocalChopEditor::Content : public juce::Component
{
public:
    explicit Content (VocalChopProcessor& p)
    {
        auto& apvts = p.apvts;

        const auto pct = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        const auto ms0 = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
        const auto ms1 = [] (double v) { return juce::String (v, 1) + " ms"; };
        const auto st  = [] (double v) { return juce::String (v >= 0.0 ? "+" : "") + juce::String (v, 2) + " st"; };

        const juce::StringArray rates { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        const auto rateFmt = [rates] (double v) { return rates[juce::jlimit (0, rates.size() - 1, juce::roundToInt (v))]; };

        auto make = [&] (std::unique_ptr<Knob>& k, const char* id, const char* title,
                         juce::Colour a, juce::Colour b, std::function<juce::String (double)> fmt,
                         juce::Rectangle<int> r, juce::StringArray ring = {})
        {
            k = std::make_unique<Knob> (apvts, id, title, a, b, std::move (fmt), laf, ring);
            addAndMakeVisible (*k);
            k->setBounds (r);
        };

        // TRIGGER
        make (rate,   "division",    "RATE",        ui::magenta, ui::violet, rateFmt, { 48, 166, 300, 276 }, rates);
        make (swing,  "swing",       "SWING",       ui::magenta, ui::violet, pct,     { 38, 468, 140, 190 });
        make (prob,   "probability", "PROBABILITY", ui::magenta, ui::violet, pct,     { 220, 468, 140, 190 });

        // SLICE
        make (length, "lengthMs",    "LENGTH",      ui::cyan,    ui::blue,   ms0,     { 1410, 166, 160, 152 });
        make (offset, "offsetMs",    "OFFSET",      ui::cyan,    ui::blue,   ms0,     { 1410, 318, 160, 146 });
        make (pitch,  "pitch",       "PITCH",       ui::cyan,    ui::blue,   st,      { 1410, 466, 160, 172 });

        // ENVELOPE & MIX
        make (attack,  "attackMs",   "ATTACK",      ui::magenta, ui::violet, ms1,     { 222, 752, 170, 174 });
        make (release, "releaseMs",  "RELEASE",     ui::violet,  ui::blue,   ms0,     { 553, 752, 170, 174 });
        make (dry,     "dry",        "DRY",         ui::violet,  ui::blue,   pct,     { 956, 752, 170, 174 });
        make (wet,     "wet",        "WET",         ui::cyan,    ui::blue,   pct,     { 1268, 752, 170, 174 });

        mode = std::make_unique<ModeSelector> (apvts);
        addAndMakeVisible (*mode);
        mode->setBounds (562, 28, 487, 55);

        meter = std::make_unique<MeterView> (p);
        addAndMakeVisible (*meter);
        meter->setBounds (1140, 20, 540, 80);

        waveform = std::make_unique<WaveformView> (p);
        addAndMakeVisible (*waveform);
        waveform->setBounds (404, 128, 900, 300);

        reverse = std::make_unique<ReverseButton> (apvts);
        addAndMakeVisible (*reverse);
        reverse->setBounds (742, 432, 224, 58);

        steps = std::make_unique<StepRow> (p);
        addAndMakeVisible (*steps);
        steps->setBounds (408, 498, 892, 150);

        lengthStep = std::make_unique<SwitchToggle> (apvts, "lengthSync", "LENGTH = STEP");
        addAndMakeVisible (*lengthStep);
        lengthStep->setBounds (1360, 648, 290, 40);
    }

    ~Content() override = default;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ui::bg);

        // --- логотип ---
        const float heights[] = { 8, 18, 30, 44, 32, 54, 38, 24, 32, 16, 9 };

        for (int i = 0; i < 11; ++i)
        {
            const float t = (float) i / 10.0f;
            g.setColour (ui::magenta.interpolatedWith (ui::violet, t));
            g.fillRoundedRectangle (36.0f + (float) i * 6.2f, 56.0f - heights[i] * 0.5f, 3.4f, heights[i], 1.7f);
        }

        const auto logoFont = ui::font (56.0f, true);
        juce::GlyphArrangement ga;
        ga.addLineOfText (logoFont, "VOCAL ", 0.0f, 0.0f);
        const float vocalW = ga.getBoundingBox (0, -1, true).getWidth();

        g.setFont (logoFont);
        g.setColour (juce::Colours::white);
        g.drawText ("VOCAL", juce::Rectangle<int> (114, 18, 400, 76), juce::Justification::centredLeft);

        g.setGradientFill (juce::ColourGradient (ui::magenta, 114.0f + vocalW, 0.0f,
                                                 ui::blue, 114.0f + vocalW + 140.0f, 0.0f, false));
        g.drawText ("CHOP", juce::Rectangle<int> ((int) (114.0f + vocalW), 18, 300, 76),
                    juce::Justification::centredLeft);

        // --- панели ---
        panelBox (g, { 16.0f, 111.0f, 365.0f, 580.0f });
        panelBox (g, { 394.0f, 111.0f, 920.0f, 580.0f });
        panelBox (g, { 1330.0f, 111.0f, 335.0f, 580.0f });
        panelBox (g, { 16.0f, 706.0f, 1650.0f, 220.0f });

        panelTitle (g, "TRIGGER", 200.0f, 144.0f, 35.0f, 122.0f, 275.0f, 362.0f, ui::magenta, ui::magenta);
        panelTitle (g, "SLICE", 1490.0f, 144.0f, 1346.0f, 1433.0f, 1547.0f, 1642.0f, ui::cyan, ui::cyan);
        panelTitle (g, "ENVELOPE  &  MIX", 833.0f, 738.0f, 44.0f, 698.0f, 990.0f, 1633.0f, ui::violet, ui::blue);

        g.setColour (juce::Colour (0xff2a2a38));
        g.drawLine (843.0f, 780.0f, 843.0f, 905.0f, 1.4f);
    }

private:
    static void panelBox (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (ui::panel);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (ui::edge);
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);
    }

    static void panelTitle (juce::Graphics& g, const juce::String& text, float cx, float cy,
                            float l0, float l1, float r0, float r1,
                            juce::Colour a, juce::Colour b)
    {
        g.setGradientFill (juce::ColourGradient (a.withAlpha (0.9f), l0, cy, b.withAlpha (0.9f), r1, cy, false));
        g.drawLine (l0, cy, l1, cy, 1.6f);
        g.drawLine (r0, cy, r1, cy, 1.6f);

        g.setGradientFill (juce::ColourGradient (a, cx - 120.0f, cy, b, cx + 120.0f, cy, false));
        g.setFont (ui::font (27.0f, true));
        g.drawText (text, juce::Rectangle<float> (300.0f, 36.0f).withCentre ({ cx, cy }).toNearestInt(),
                    juce::Justification::centred);
    }

    // laf объявлен первым: умирает последним, после всех ручек
    ChopLookAndFeel laf;

    std::unique_ptr<Knob> rate, swing, prob, length, offset, pitch, attack, release, dry, wet;
    std::unique_ptr<ModeSelector> mode;
    std::unique_ptr<MeterView> meter;
    std::unique_ptr<WaveformView> waveform;
    std::unique_ptr<ReverseButton> reverse;
    std::unique_ptr<StepRow> steps;
    std::unique_ptr<SwitchToggle> lengthStep;
};

//==============================================================================
VocalChopEditor::VocalChopEditor (VocalChopProcessor& p)
    : AudioProcessorEditor (&p), proc (p), content (std::make_unique<Content> (p))
{
    addAndMakeVisible (*content);

    setResizable (true, true);
    setResizeLimits (675, 375, 1683, 935);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    setSize (1010, 561);
}

VocalChopEditor::~VocalChopEditor() = default;

void VocalChopEditor::resized()
{
    const float s = (float) getWidth() / kDesignW;
    content->setBounds (0, 0, (int) kDesignW, (int) kDesignH);
    content->setTransform (juce::AffineTransform::scale (s));
}
