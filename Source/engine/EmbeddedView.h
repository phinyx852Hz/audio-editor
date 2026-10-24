// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace embedded
{
    // A plugin's view is an X window of its own, which follows its component only when
    // that component itself moves or resizes: after moving the editor around it, call
    // this, or the view stays behind in the corner.
    inline void follow (juce::Component& editor)
    {
        std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
        {
            if (auto* x = dynamic_cast<juce::XEmbedComponent*> (&c))
                x->updateEmbeddedBounds();
            for (auto* child : c.getChildren())
                visit (*child);
        };
        visit (editor);
    }

    // Asks an editor that cannot resize to scale itself to the room there is (from half
    // to four times its own size). Most VST3 and CLAP plugins can; one that ignores it
    // keeps its size.
    class ScaleToFit
    {
    public:
        explicit ScaleToFit (juce::Component& e)
            : editor (dynamic_cast<juce::AudioProcessorEditor*> (&e)),
              baseWidth (std::max (1, e.getWidth())), baseHeight (std::max (1, e.getHeight())) {}

        void fit (int width, int height)
        {
            if (editor == nullptr || editor->isResizable() || width <= 0 || height <= 0)
                return;
            const auto wanted = juce::jlimit (0.5f, 4.0f, std::min ((float) width / (float) baseWidth,
                                                                   (float) height / (float) baseHeight));
            if (std::abs (wanted - scale) <= 0.01f)
                return;
            scale = wanted;
            const juce::ScopedValueSetter<bool> s (scaling, true);
            editor->setScaleFactor (scale);
        }

        bool isScaling() const { return scaling; }   // the editor is resizing because it was asked to
        bool isScaled() const  { return std::abs (scale - 1.0f) > 0.01f; }

    private:
        juce::AudioProcessorEditor* editor;
        const int baseWidth, baseHeight;   // the editor's own size, at scale 1
        float scale = 1.0f;
        bool scaling = false;
    };
}
