#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
/* The module reads its pots once per 32-frame codec block. Matching that here
   keeps parameter smoothing on the firmware's own clock rather than the
   host's, so automation behaves the same at any block size. */
constexpr int kControlChunk = 64;
}

CloudiusProcessor::CloudiusProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("In", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", cld::createLayout())
{
    for (size_t i = 0; i < (size_t) cld::numGrain; ++i)
    {
        grain_[i] = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (cld::pid::grain[i]));
        jassert (grain_[i]);
    }
    for (size_t i = 0; i < (size_t) cld::numBlend; ++i)
    {
        blend_[i] = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (cld::pid::blend[i]));
        jassert (blend_[i]);
    }

    mode_    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (cld::pid::mode));
    quality_ = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (cld::pid::quality));
    freeze_  = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter (cld::pid::freeze));
    limiter_p_ = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (cld::pid::limiter));
    note_    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (cld::pid::noteTrack));
    jassert (mode_ && quality_ && freeze_ && note_ && limiter_p_);
}

bool CloudiusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void CloudiusProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, juce::jmax (samplesPerBlock, kControlChunk));
    limiter_.prepare (sampleRate);
    scratch_.setSize (2, juce::jmax (samplesPerBlock, kControlChunk), false, true, true);
    midiPitch_ = 0.0f;
    heldNotes_ = 0;
    setLatencySamples (engine_.latencySamples());
    pushParameters();
}

void CloudiusProcessor::pushParameters()
{
    engine_.setMode (mode_->getIndex());
    engine_.setQuality (quality_->getIndex());

    cld::EngineParams p;
    p.position = grain_[0]->get();
    p.size     = grain_[1]->get();
    p.pitch    = cld::pitchSemitones (grain_[2]->get())
                 + (note_->getIndex() == 1 ? midiPitch_ : 0.0f);
    p.density  = grain_[3]->get();
    p.texture  = grain_[4]->get();
    p.dryWet   = blend_[0]->get();
    p.spread   = blend_[1]->get();
    p.feedback = blend_[2]->get();
    p.reverb   = blend_[3]->get();
    p.freeze   = freeze_->get();

    /* The firmware constrains the summed pitch to +/- 4 octaves. */
    p.pitch = juce::jlimit (-48.0f, 48.0f, p.pitch);

    engine_.setParameters (p);
}

void CloudiusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples == 0 || numIn == 0 || numOut == 0)
        return;

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, true, true);

    scratch_.copyFrom (0, 0, buffer, 0, 0, numSamples);
    scratch_.copyFrom (1, 0, buffer, numIn > 1 ? 1 : 0, 0, numSamples);

    float* l = scratch_.getWritePointer (0);
    float* r = scratch_.getWritePointer (1);

    auto midiPos = midi.begin();
    int trigs = panel.trigCount.load (std::memory_order_relaxed);

    for (int offset = 0; offset < numSamples; offset += kControlChunk)
    {
        const int n = juce::jmin (kControlChunk, numSamples - offset);

        /* The module's TRIG jack arrives over MIDI: a note-on fires a grain,
           and while any note is held the gate stays up. */
        for (; midiPos != midi.end() && (*midiPos).samplePosition < offset + n; ++midiPos)
        {
            const auto msg = (*midiPos).getMessage();
            if (msg.isNoteOn())
            {
                trigRequest_.store (true);
                midiPitch_ = (float) msg.getNoteNumber() - 60.0f;
                ++heldNotes_;
            }
            else if (msg.isNoteOff())
            {
                heldNotes_ = juce::jmax (0, heldNotes_ - 1);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                heldNotes_ = 0;
            }
        }

        if (trigRequest_.exchange (false))
        {
            engine_.trigger();
            ++trigs;
        }
        engine_.setGate (heldNotes_ > 0);

        pushParameters();
        engine_.process (l + offset, r + offset, n);
    }

    if (limiter_p_->get())
        limiter_.process (l, r, numSamples);
    else
        limiter_.reset();

    buffer.copyFrom (0, 0, scratch_, 0, 0, numSamples);
    if (numOut > 1)
        buffer.copyFrom (1, 0, scratch_, 1, 0, numSamples);
    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    panel.envIn.store  (engine_.inLevel(),  std::memory_order_relaxed);
    panel.envOut.store (engine_.outLevel(), std::memory_order_relaxed);
    panel.inPeak.store (engine_.inPeak(),   std::memory_order_relaxed);
    panel.head.store   (engine_.bufferPhase(),   std::memory_order_relaxed);
    panel.bufferSeconds.store (engine_.bufferSeconds(), std::memory_order_relaxed);
    panel.trigCount.store (trigs, std::memory_order_relaxed);
    panel.reduction.store (limiter_p_->get() ? limiter_.readReduction() : 1.0f,
                           std::memory_order_relaxed);

    midi.clear();
}

juce::AudioProcessorEditor* CloudiusProcessor::createEditor()
{
    return new CloudiusEditor (*this);
}

void CloudiusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CloudiusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.isValid())
            apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CloudiusProcessor();
}
