#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "CloudsEngine.h"
#include "Limiter.h"
#include "Parameters.h"

/** Everything the panel animates, published from the audio thread. */
struct PanelState
{
    std::atomic<float> envIn  { 0.0f };
    std::atomic<float> envOut { 0.0f };
    std::atomic<float> inPeak { 0.0f };
    std::atomic<float> head   { 0.0f };   // write head, 0..1
    std::atomic<float> bufferSeconds { 1.0f };

    /** Bumps on every trigger the engine takes, so the panel can flash TRIG. */
    std::atomic<int> trigCount { 0 };

    /** Limiter gain reduction, 1 = not working. */
    std::atomic<float> reduction { 1.0f };
};

class CloudiusProcessor final : public juce::AudioProcessor
{
public:
    CloudiusProcessor();
    ~CloudiusProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    /* juce::String's `const char*` ctor assumes ASCII/Latin-1, not UTF-8 — an
       accented literal needs CharPointer_UTF8 or it silently mangles. */
    const juce::String getName() const override { return juce::CharPointer_UTF8 ("Skýjað"); }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** TRIG / MIDI note-on — fires a grain, or re-aligns the loop. */
    void requestTrigger() { trigRequest_.store (true); }

    juce::AudioProcessorValueTreeState apvts;
    PanelState panel;

private:
    void pushParameters();

    cld::CloudsEngine engine_;
    cld::Limiter      limiter_;
    juce::AudioBuffer<float> scratch_;

    std::atomic<bool> trigRequest_ { false };
    float midiPitch_ = 0.0f;
    int   heldNotes_ = 0;

    std::array<juce::AudioParameterFloat*, cld::numGrain> grain_ {};
    std::array<juce::AudioParameterFloat*, cld::numBlend> blend_ {};
    juce::AudioParameterChoice* mode_    = nullptr;
    juce::AudioParameterChoice* quality_ = nullptr;
    juce::AudioParameterBool*   freeze_  = nullptr;
    juce::AudioParameterBool*   limiter_p_ = nullptr;
    juce::AudioParameterChoice* note_    = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudiusProcessor)
};
