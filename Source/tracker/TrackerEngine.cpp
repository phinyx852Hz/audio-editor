// SPDX-License-Identifier: AGPL-3.0-only
#include "TrackerEngine.h"

#include "AudioDriver_NULL.h"
#include "MasterMixer.h"
#include "Mixable.h"
#include "PlayerSTD.h"
#include "XModule.h"

#include <algorithm>
#include <cmath>

//==============================================================================
// One MilkyPlay player with its mixer, pulled a block at a time instead of by a
// sound card. The mix is taken before MilkyPlay rounds it to 16 bits, so nothing
// clips on the way into PHINYX.
class TrackerEngine::Player final : private Mixable
{
public:
    Player (XModule& m, double sampleRate)
        : module (m),
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
    }

    ~Player() override
    {
        mixer.stop();
        if (! mixer.isDeviceRemoved (&player))
            mixer.removeDevice (&player);
    }

    void start (bool repeat, int order, int row, int patternIndex)
    {
        if (! mixer.isDeviceRemoved (&player))
            mixer.removeDevice (&player);
        player.startPlaying (&module, repeat, (mp_uint32) std::max (0, order), (mp_uint32) std::max (0, row), -1, nullptr, false, patternIndex);
        mixer.addDevice (&player);
        if (! mixer.isPlaying())
            mixer.start();
        fifo.clear();
        fifoRead = 0;
    }

    void mute (int channel, bool m) { player.muteChannel (channel, m); }
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
    int rate, blockFrames;
    AudioDriver_NULL driver;
    MasterMixer mixer;
    PlayerSTD player;
    std::vector<float> fifo;
    size_t fifoRead = 0;
};

//==============================================================================
TrackerEngine::TrackerEngine() = default;

TrackerEngine::~TrackerEngine()
{
    std::lock_guard lock (liveLock);
    live.reset();
}

std::string TrackerEngine::load (const std::string& path)
{
    auto loaded = std::make_unique<XModule>();
    const auto result = loaded->loadModule (path.c_str());
    if (result != MP_OK || ! loaded->isModuleLoaded())
        return result == MP_UNSUPPORTED_FORMAT || result == MP_LOADER_FAILED ? "This is not a module MilkyTracker can read (XM, MOD, IT, S3M, MTM, 669, FAR, ...)."
                                                                             : "The module could not be read.";

    {
        std::lock_guard lock (liveLock);
        live.reset();
        module = std::move (loaded);
    }
    filePath = path;
    const auto channels = (size_t) numChannels();
    muted.assign (channels, false);
    soloed.assign (channels, false);
    resetTracks();
    return {};
}

bool TrackerEngine::isLoaded() const { return module != nullptr; }

std::string TrackerEngine::title() const
{
    if (module == nullptr)
        return {};
    char name[MP_MAXTEXT + 1] {};
    module->getTitle (name);
    std::string s (name);
    while (! s.empty() && (s.back() == ' ' || s.back() == '\0'))
        s.pop_back();
    return s;
}

std::string TrackerEngine::formatName() const
{
    if (module == nullptr)
        return {};
    switch (module->getType())
    {
        case XModule::ModuleType_XM:  return "FastTracker 2 (XM)";
        case XModule::ModuleType_MOD: return "ProTracker (MOD)";
        case XModule::ModuleType_IT:  return "Impulse Tracker (IT)";
        case XModule::ModuleType_S3M: return "Scream Tracker 3 (S3M)";
        case XModule::ModuleType_STM: return "Scream Tracker 2 (STM)";
        case XModule::ModuleType_MTM: return "MultiTracker (MTM)";
        case XModule::ModuleType_669: return "Composer 669";
        case XModule::ModuleType_FAR: return "Farandole (FAR)";
        case XModule::ModuleType_MDL: return "DigiTrakker (MDL)";
        case XModule::ModuleType_OKT: return "Oktalyzer (OKT)";
        case XModule::ModuleType_PTM: return "PolyTracker (PTM)";
        case XModule::ModuleType_ULT: return "UltraTracker (ULT)";
        case XModule::ModuleType_AMF: return "DSMI (AMF)";
        case XModule::ModuleType_DBM: return "DigiBooster Pro (DBM)";
        default:                      return "Module";
    }
}

int TrackerEngine::numChannels() const { return module != nullptr ? module->header.channum : 0; }
int TrackerEngine::numOrders() const { return module != nullptr ? module->header.ordnum : 0; }
int TrackerEngine::numPatterns() const { return module != nullptr ? module->header.patnum : 0; }
int TrackerEngine::initialTempo() const { return module != nullptr ? module->header.speed : 0; }   // MilkyPlay: speed field is the BPM
int TrackerEngine::initialSpeed() const { return module != nullptr ? module->header.tempo : 0; }   // and tempo the ticks per row

int TrackerEngine::patternAt (int order) const
{
    if (module == nullptr || order < 0 || order >= module->header.ordnum)
        return -1;
    return module->header.ord[order];
}

const TXMPattern* TrackerEngine::pattern (int index) const
{
    if (module == nullptr || index < 0 || index >= module->header.patnum)
        return nullptr;
    const auto& p = module->phead[index];
    return p.patternData != nullptr && p.rows > 0 ? &p : nullptr;
}

std::vector<std::string> TrackerEngine::instrumentNames() const
{
    std::vector<std::string> names;
    if (module == nullptr)
        return names;
    for (int i = 0; i < module->header.insnum; ++i)
    {
        std::string s (module->instr[i].name, strnlen (module->instr[i].name, MP_MAXTEXT));
        while (! s.empty() && s.back() == ' ')
            s.pop_back();
        names.push_back (s);
    }
    return names;
}

//==============================================================================
void TrackerEngine::playSong (int order, int row, double sampleRate)
{
    if (module == nullptr)
        return;
    auto p = std::make_unique<Player> (*module, sampleRate);
    p->start (true, order, row, -1);
    std::lock_guard lock (liveLock);
    live = std::move (p);
    applyMutes();
}

void TrackerEngine::playPattern (int patternIndex, int row, double sampleRate)
{
    if (module == nullptr)
        return;
    auto p = std::make_unique<Player> (*module, sampleRate);
    p->start (true, 0, row, patternIndex);
    std::lock_guard lock (liveLock);
    live = std::move (p);
    applyMutes();
}

void TrackerEngine::stop()
{
    std::unique_ptr<Player> old;
    {
        std::lock_guard lock (liveLock);
        old = std::move (live);
    }
}

bool TrackerEngine::isPlaying() const
{
    std::lock_guard lock (liveLock);
    return live != nullptr && ! live->halted();
}

void TrackerEngine::position (int& order, int& row) const
{
    std::lock_guard lock (liveLock);
    order = row = 0;
    if (live != nullptr)
        live->position (order, row);
}

int TrackerEngine::tempo() const
{
    std::lock_guard lock (liveLock);
    return live != nullptr ? live->tempo() : initialTempo();
}

int TrackerEngine::speed() const
{
    std::lock_guard lock (liveLock);
    return live != nullptr ? live->speed() : initialSpeed();
}

void TrackerEngine::render (float* left, float* right, int numFrames)
{
    std::unique_lock lock (liveLock, std::try_to_lock);
    if (! lock.owns_lock() || live == nullptr)
    {
        std::fill (left, left + numFrames, 0.0f);
        std::fill (right, right + numFrames, 0.0f);
        return;
    }
    live->pull (left, right, numFrames);
}

//==============================================================================
void TrackerEngine::setMuted (int channel, bool m)
{
    if (channel < 0 || channel >= (int) muted.size())
        return;
    muted[(size_t) channel] = m;
    std::lock_guard lock (liveLock);
    applyMutes();
}

bool TrackerEngine::isMuted (int channel) const { return channel >= 0 && channel < (int) muted.size() && muted[(size_t) channel]; }

void TrackerEngine::setSoloed (int channel, bool s)
{
    if (channel < 0 || channel >= (int) soloed.size())
        return;
    soloed[(size_t) channel] = s;
    std::lock_guard lock (liveLock);
    applyMutes();
}

bool TrackerEngine::isSoloed (int channel) const { return channel >= 0 && channel < (int) soloed.size() && soloed[(size_t) channel]; }

bool TrackerEngine::isHeard (int channel) const
{
    const bool anySolo = std::find (soloed.begin(), soloed.end(), true) != soloed.end();
    return ! isMuted (channel) && (! anySolo || isSoloed (channel));
}

// Called with liveLock held.
void TrackerEngine::applyMutes()
{
    if (live == nullptr)
        return;
    for (int c = 0; c < (int) muted.size(); ++c)
        live->mute (c, ! isHeard (c));
}

void TrackerEngine::setTrack (int channel, int t)
{
    if (channel >= 0 && channel < (int) tracks.size())
        tracks[(size_t) channel] = std::clamp (t, 0, numTracks - 1);
}

int TrackerEngine::track (int channel) const { return channel >= 0 && channel < (int) tracks.size() ? tracks[(size_t) channel] : 0; }

void TrackerEngine::resetTracks()
{
    const int n = numChannels();
    const int used = std::min (n, numTracks);
    tracks.assign ((size_t) n, 0);
    for (int c = 0; c < n; ++c)
        tracks[(size_t) c] = used > 0 ? c * used / n : 0;
}

int TrackerEngine::tracksInUse() const
{
    int highest = -1;
    for (int c = 0; c < (int) tracks.size(); ++c)
        if (isHeard (c))
            highest = std::max (highest, tracks[(size_t) c]);
    return highest + 1;
}

//==============================================================================
AudioDataPtr TrackerEngine::record (double sampleRate, bool multiChannel, std::string& error,
                                    std::atomic<float>* progress, const std::atomic<bool>* cancel) const
{
    if (module == nullptr)
    {
        error = "No module is open.";
        return nullptr;
    }

    // One pass per stereo pair: everything heard for stereo, else each track's channels.
    std::vector<std::vector<bool>> passes;
    const int channels = numChannels();
    if (! multiChannel)
    {
        std::vector<bool> heard ((size_t) channels);
        for (int c = 0; c < channels; ++c)
            heard[(size_t) c] = isHeard (c);
        passes.push_back (heard);
    }
    else
        for (int t = 0; t < tracksInUse(); ++t)
        {
            std::vector<bool> heard ((size_t) channels);
            for (int c = 0; c < channels; ++c)
                heard[(size_t) c] = isHeard (c) && track (c) == t;
            passes.push_back (heard);
        }
    if (passes.empty())
    {
        error = "Every channel is muted.";
        return nullptr;
    }

    // Each pass plays the song once from the top; it ends where the song ends or
    // jumps back (MilkyPlay halts there when not repeating), or after an hour.
    const auto maxFrames = (Frame) (sampleRate * 3600.0);
    const int orders = std::max (1, numOrders());
    std::vector<std::vector<float>> out (passes.size() * 2);
    constexpr int block = 4096;
    std::vector<float> l (block), r (block);
    for (size_t p = 0; p < passes.size(); ++p)
    {
        Player player (*module, sampleRate);
        player.start (false, 0, 0, -1);
        for (int c = 0; c < channels; ++c)
            player.mute (c, ! passes[p][(size_t) c]);

        auto& left = out[p * 2];
        auto& right = out[p * 2 + 1];
        while (! player.halted() && (Frame) left.size() < maxFrames)
        {
            if (cancel != nullptr && cancel->load())
            {
                error = "Cancelled.";
                return nullptr;
            }
            player.pull (l.data(), r.data(), block);
            left.insert (left.end(), l.begin(), l.end());
            right.insert (right.end(), r.begin(), r.end());
            if (progress != nullptr)
            {
                int order = 0, row = 0;
                player.position (order, row);
                progress->store (((float) p + std::clamp ((float) order / (float) orders, 0.0f, 1.0f)) / (float) passes.size());
            }
        }
    }

    // The passes can differ by a block at the end; trim to the longest real sound.
    size_t length = 0;
    for (const auto& ch : out)
        length = std::max (length, ch.size());
    for (auto& ch : out)
        ch.resize (length, 0.0f);
    while (length > 0)
    {
        bool silent = true;
        for (const auto& ch : out)
            silent = silent && std::abs (ch[length - 1]) < 1.0e-6f;
        if (! silent)
            break;
        --length;
    }

    AudioData::Builder builder ((int) out.size(), sampleRate);
    std::vector<const float*> pointers (out.size());
    for (size_t at = 0; at < length; at += 65536)
    {
        const auto n = (int) std::min<size_t> (65536, length - at);
        for (size_t c = 0; c < out.size(); ++c)
            pointers[c] = out[c].data() + at;
        builder.append (pointers.data(), n);
    }
    if (progress != nullptr)
        progress->store (1.0f);
    return builder.finish();
}
