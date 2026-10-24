// SPDX-License-Identifier: AGPL-3.0-only
// Internal to the Tracker engine (TrackerEngine.cpp and TrackerEdit.cpp).
#pragma once

#include "TrackerEngine.h"

#include "AudioDriver_NULL.h"
#include "MasterMixer.h"
#include "Mixable.h"
#include "PlayerSTD.h"
#include "XModule.h"

#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
// One MilkyPlay player with its mixer, pulled a block at a time instead of by a
// sound card. The mix is taken before MilkyPlay rounds it to 16 bits, so nothing
// clips on the way into PHI. Linked instruments' notes (see TrackerEngine::setLink)
// go to a LinkSink, or are collected for a recording.
class TrackerEngine::Player final : private Mixable, private PlayerSTD::ExternalNotes
{
public:
    using Links = std::array<std::atomic<int>, numInstruments + 1>;
    using Notes = std::vector<std::array<int, 4>>;   // offset, MIDI channel, note, velocity

    Player (XModule& m, double sampleRate, const Links* l = nullptr, const std::atomic<LinkSink*>* s = nullptr, Notes* c = nullptr)
        : module (m), links (l), sink (s), collected (c),
          rate ((int) std::lround (sampleRate)),
          blockFrames ((int) ChannelMixer::beatPacketsToBufferSize ((mp_uint32) rate, 4)),
          mixer ((mp_uint32) rate, (mp_uint32) blockFrames, 1, &driver),
          player ((mp_uint32) rate)
    {
        mixer.setSampleShift (shift);
        mixer.setFilterHook (this);
        player.resetMainVolumeOnStartPlay (true);
        player.resetOnStop (false);
        player.setBufferSize ((mp_uint32) blockFrames);
        player.setResamplerType (ChannelMixer::MIXER_LERPING_RAMPING);   // MilkyTracker's default
        player.setMasterVolume (256);
        player.setPanningSeparation (256);
        player.setPlayMode (PlayerBase::PlayMode_Auto);
        player.enable (PlayerBase::PlayModeOptionPanning8xx, true);
        player.enable (PlayerBase::PlayModeOptionForcePTPitchLimit, true);
        audible.fill (true);
        heldSlot.fill (0);
        if (links != nullptr)
            player.setExternalNotes (this);
    }

    ~Player() override
    {
        player.releaseExternalNotes();   // nothing linked keeps sounding
        player.setExternalNotes (nullptr);
        mixer.stop();
        if (! mixer.isDeviceRemoved (&player))
            mixer.removeDevice (&player);
    }

    // Idle: plays nothing of the song, only notes sent with playNote().
    void start (bool repeat, int order, int row, int patternIndex, bool idle = false)
    {
        if (! mixer.isDeviceRemoved (&player))
            mixer.removeDevice (&player);
        player.startPlaying (&module, repeat, (mp_uint32) std::max (0, order), (mp_uint32) std::max (0, row), -1, nullptr, idle, patternIndex);
        mixer.addDevice (&player);
        if (! mixer.isPlaying())
            mixer.start();
        fifo.clear();
        fifoRead = 0;
    }

    void releaseLinked() { player.releaseExternalNotes(); }   // note-offs for the linked notes held

    void mute (int channel, bool m)
    {
        player.muteChannel (channel, m);
        if (channel >= 0 && channel < (int) audible.size())
            audible[(size_t) channel] = ! m;
    }
    bool idle() const { return player.isIdle(); }
    void playNote (int channel, int note, int instrument) { player.playNote ((mp_ubyte) channel, note, instrument); }
    bool halted() const { return player.hasSongHalted(); }
    void position (int& order, int& row) const
    {
        mp_sint32 o = 0, r = 0;
        player.getPosition (o, r);
        order = o;
        row = r;
    }
    int tempo() const { return player.getTempo(); }
    int speed() const { return player.getSpeed(); }

    // Fills [left, right) with the next frames.
    void pull (float* left, float* right, int numFrames)
    {
        int done = 0;
        while (done < numFrames)
        {
            if (fifoRead >= fifo.size())
            {
                fifo.clear();
                fifoRead = 0;
                base = done;        // where the ticks of this mixer buffer start
                driver.advance();   // the mixer runs and mix() below collects it
                if (fifo.empty())
                {
                    std::fill (left + done, left + numFrames, 0.0f);
                    std::fill (right + done, right + numFrames, 0.0f);
                    return;
                }
            }
            const auto available = (int) ((fifo.size() - fifoRead) / 2);
            const auto n = std::min (available, numFrames - done);
            for (int i = 0; i < n; ++i)
            {
                left[done + i] = fifo[fifoRead++];
                right[done + i] = fifo[fifoRead++];
            }
            done += n;
        }
    }

private:
    // PlayerSTD::ExternalNotes: the linked instruments
    bool plays (mp_sint32 ins) override
    {
        return links != nullptr && ins >= 1 && ins <= numInstruments && (*links)[(size_t) ins].load (std::memory_order_relaxed) > 0;
    }

    void noteOn (mp_sint32 frame, mp_sint32 chn, mp_sint32 ins, mp_sint32 note, mp_sint32 vol) override
    {
        if (chn < 0 || chn >= (int) audible.size() || ! audible[(size_t) chn] || ! plays (ins))
            return;
        const auto slot = (*links)[(size_t) ins].load (std::memory_order_relaxed);
        heldSlot[(size_t) chn] = slot;
        // XM's C-4 (49) is MIDI note 60; the volume (0 to 255 here) is the velocity.
        send (frame, slot, std::clamp (note + 11, 0, 127), std::clamp ((int) std::lround (vol * 127.0 / 255.0), 1, 127));
    }

    void noteOff (mp_sint32 frame, mp_sint32 chn, mp_sint32, mp_sint32 note) override
    {
        if (chn < 0 || chn >= (int) heldSlot.size() || heldSlot[(size_t) chn] == 0)
            return;
        send (frame, heldSlot[(size_t) chn], std::clamp (note + 11, 0, 127), 0);
        heldSlot[(size_t) chn] = 0;
    }

    void send (int frame, int channel, int note, int velocity)
    {
        const auto offset = frame < 0 ? 0 : base + frame;
        if (collected != nullptr)
            collected->push_back ({ offset, channel, note, velocity });
        else if (auto* to = sink != nullptr ? sink->load() : nullptr)
            to->linkedNote (offset, channel, note, velocity);
    }

    // MilkyPlay's mix, interleaved stereo in fixed point, before it is cut to 16 bits.
    void mix (mp_sint32* buffer, mp_uint32 numSamples) override
    {
        const float scale = 1.0f / (32768.0f * (float) (1 << shift));
        const auto count = (size_t) numSamples * 2;
        const auto at = fifo.size();
        fifo.resize (at + count);
        for (size_t i = 0; i < count; ++i)
            fifo[at + i] = (float) buffer[i] * scale;
    }

    static constexpr int shift = 1;   // MilkyPlay's headroom: full scale is 32768 << shift
    XModule& module;
    const Links* links;
    const std::atomic<LinkSink*>* sink;
    Notes* collected;
    int base = 0;
    std::array<bool, 256> audible {};
    std::array<int, 256> heldSlot {};
    int rate, blockFrames;
    AudioDriver_NULL driver;
    MasterMixer mixer;
    PlayerSTD player;
    std::vector<float> fifo;
    size_t fifoRead = 0;
};

