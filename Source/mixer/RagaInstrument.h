// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// PHI rāga on a mixer track (owner, 2026-10-25): the track's MIDI played as a raga on the modelled
// sitar. The raga is the one in force for the track when that is a raga (its clip's, else its
// scene's, else its own tuning; Tunings.h), else the instrument's own choice. Keys become the
// raga's svaras by the direction the melody moves (phi::RagaPlayer), tuned as its sheet says,
// with āndolan where it calls for it; the tarab are retuned to the raga.
#include "Instruments.h"
#include "../core/PhiRaga.h"
#include "../core/PhiSitar.h"

#include <array>
#include <mutex>

namespace mixer
{
    struct RagaSettings
    {
        std::string raga = "yaman (kalyan)";   // the raga's plain name, when the track's tuning is not a raga
        int sa = 1;                            // Sa's key, 0 C to 11 B (C♯ is a common sitar Sa)
        double ornaments = 1.0;                // 0 plain svaras, 1 as the sheet says
        double jawari = 0.6;
        double tarab = 0.6;
    };

    class RagaInstrument final : public NoteInstrument
    {
    public:
        Kind kind() const override { return Kind::raga; }
        std::string name() const override { return "PHI rāga"; }
        void prepare (double sampleRate, int maxBlock) override;
        void releaseAll() override;
        void reset() override;
        std::string state() const override;
        void setState (const std::string&) override;
        // The tuning in force: a raga (index into phi::builtInRagas(), root 0..11, in 12-TET or
        // not) or -1 for none, when the instrument's own raga plays. UI thread.
        void setRaga (int raga, int root, bool equalTempered) override;

        void setSettings (const RagaSettings&);
        RagaSettings settings() const;
        std::string playing() const;   // the raga it plays now, for the window

    protected:
        void handle (const NoteEvent&) override;
        void render (float* left, float* right, int frames) override;

    private:
        void applyPending();   // audio thread: takes a new raga or settings

        struct Held
        {
            int key = -1, string = -1;
            phi::RagaPlayer::Voice voice;
            double seconds = 0.0;
        };
        mutable std::mutex lock;   // what the UI thread hands over
        RagaSettings current;
        int forced = -1, forcedRoot = 0;
        bool forcedEqual = false, changed = true;

        phi::Raga raga;            // the audio thread's own copy
        phi::RagaPlayer player;
        phi::Sitar sitar;
        std::array<Held, phi::Sitar::maxPlayed> held {};
        double rate = 48000.0, bend = 0.0;
        int notesSinceChikari = 0;
    };
}
