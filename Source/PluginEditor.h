#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Panel.h"
#include "PluginProcessor.h"

/**
 * The panel: every control on one surface, one function each. Drawing and hit
 * testing both work in the fixed 980 x 496 design space of Panel.h, with a
 * single scale transform applied on the way out, so the layout constants are
 * the only source of truth.
 */
class CloudiusEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit CloudiusEditor (CloudiusProcessor&);
    ~CloudiusEditor() override;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    enum class Kind { knob, fader, selector, latch };

    struct Ctl
    {
        Kind kind {};
        juce::RangedAudioParameter* param = nullptr;
        juce::Rectangle<float> hit;   // design space
        float travel = 150.0f;        // pixels of drag for a full sweep
        int   positions = 0;          // selectors only
        bool  vertical = false;       // selectors only: read top to bottom
    };

    void buildControls();

    float scale() const;
    juce::Point<float> toDesign (juce::Point<float> px) const;
    int controlAt (juce::Point<float> design) const;

    static int indexOf (const juce::RangedAudioParameter& p, int positions);

    CloudiusProcessor& proc;
    std::vector<Ctl> ctls;

    int   dragIdx = -1;
    float dragStartNorm = 0.0f;
    float dragStartY = 0.0f;

    int          lastTrigCount = 0;
    juce::uint32 trigFlashMs = 0;
    bool         trigHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudiusEditor)
};
