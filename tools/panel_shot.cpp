// Offscreen render of the real editor. Dev tool: verifies the panel without a
// host. Writes panel*.png into the directory given as argv[1].
#include <juce_gui_basics/juce_gui_basics.h>

#include "Panel.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
void set (CloudiusProcessor& p, const juce::String& id, float normalised)
{
    if (auto* param = p.apvts.getParameter (id))
        param->setValueNotifyingHost (normalised);
}

void shoot (CloudiusProcessor& p, const juce::File& out)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize ((int) cld::panel::designW, (int) cld::panel::designH);

    juce::Image img (juce::Image::ARGB, editor->getWidth() * 2, editor->getHeight() * 2, true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    editor->paintEntireComponent (g, false);

    juce::FileOutputStream stream (out);
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (img, stream);
    std::printf ("%s\n", out.getFullPathName().toRawUTF8());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::String ("."));

    {   // Default: granular, everything where it powers up.
        CloudiusProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 128);
        p.prepareToPlay (48000.0, 128);
        shoot (p, outDir.getChildFile ("panel.png"));
    }

    {   // Played-in: a granular patch mid-performance, buffer part-way round.
        CloudiusProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 128);
        p.prepareToPlay (48000.0, 128);
        set (p, cld::pid::grain[0], 0.34f);   // position
        set (p, cld::pid::grain[1], 0.72f);   // size
        set (p, cld::pid::grain[2], 0.75f);   // pitch, +12 st
        set (p, cld::pid::grain[3], 0.78f);   // density
        set (p, cld::pid::grain[4], 0.4f);    // texture
        set (p, cld::pid::blend[0], 0.8f);
        set (p, cld::pid::blend[1], 0.55f);
        set (p, cld::pid::blend[2], 0.3f);
        set (p, cld::pid::blend[3], 0.45f);
        set (p, cld::pid::limiter, 1.0f);
        set (p, cld::pid::noteTrack, 1.0f);
        p.panel.head.store (0.62f);
        p.panel.envIn.store (0.21f);
        p.panel.envOut.store (0.17f);
        shoot (p, outDir.getChildFile ("panel_granular.png"));
    }

    {   // Spectral, frozen, lowest quality: every selector off its default.
        CloudiusProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 128);
        p.prepareToPlay (48000.0, 128);
        set (p, cld::pid::mode, 1.0f);        // spectral
        set (p, cld::pid::quality, 1.0f);     // 8s mono
        set (p, cld::pid::freeze, 1.0f);
        set (p, cld::pid::grain[3], 0.2f);
        set (p, cld::pid::blend[0], 1.0f);
        p.panel.bufferSeconds.store (7.42f);
        p.panel.head.store (0.4f);
        shoot (p, outDir.getChildFile ("panel_spectral.png"));
    }

    {   // Looping delay, frozen, mid-buffer.
        CloudiusProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 128);
        p.prepareToPlay (48000.0, 128);
        set (p, cld::pid::mode, 2.0f / 3.0f); // looping delay
        set (p, cld::pid::quality, 2.0f / 3.0f);
        set (p, cld::pid::freeze, 1.0f);
        set (p, cld::pid::grain[0], 0.5f);
        set (p, cld::pid::grain[2], 0.25f);   // -12 st
        set (p, cld::pid::blend[2], 0.65f);
        p.panel.bufferSeconds.store (4.09f);
        p.panel.head.store (0.28f);
        p.panel.envIn.store (0.3f);
        shoot (p, outDir.getChildFile ("panel_delay.png"));
    }

    return 0;
}
