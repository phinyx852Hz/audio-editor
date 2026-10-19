// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class XModule;
struct TXMPattern;

// The Tracker's sound: a module (XM, MOD, IT, S3M and the other formats MilkyTracker
// reads) played by MilkyPlay, MilkyTracker's player library. It plays live through
// PHINYX's audio engine (render() on the audio thread) and records into new audio,
// either stereo or one stereo pair per mixer track.
class TrackerEngine
{
public:
    static constexpr int numTracks = 8;   // the mixer's tracks; a recording has up to 8 stereo pairs

    TrackerEngine();
    ~TrackerEngine();

    std::string load (const std::string& path);   // an error, or empty
    bool isLoaded() const;
    std::string path() const { return filePath; }

    // What is in the module.
    std::string title() const;
    std::string formatName() const;   // "FastTracker 2 (XM)" and so on
    int numChannels() const;
    int numOrders() const;
    int patternAt (int order) const;   // the pattern an order position plays
    int numPatterns() const;
    const TXMPattern* pattern (int index) const;
    std::vector<std::string> instrumentNames() const;
    int initialTempo() const;   // BPM
    int initialSpeed() const;   // ticks per row

    // Live playback at the device's rate. Song plays the order list from a position;
    // pattern repeats one pattern.
    void playSong (int order, int row, double sampleRate);
    void playPattern (int pattern, int row, double sampleRate);
    void stop();
    bool isPlaying() const;
    // Where playback is (order, row), for the pattern view to follow.
    void position (int& order, int& row) const;
    int tempo() const;
    int speed() const;
    void render (float* left, float* right, int numFrames);   // audio thread

    // Channels: muted, soloed, and which mixer track (0 to 7) a multi-channel recording puts them on.
    void setMuted (int channel, bool);
    bool isMuted (int channel) const;
    void setSoloed (int channel, bool);
    bool isSoloed (int channel) const;
    bool isHeard (int channel) const;   // not muted, and soloed when any channel is
    void setTrack (int channel, int track);
    int track (int channel) const;
    void resetTracks();                 // spreads the channels evenly over the tracks
    int tracksInUse() const;            // highest track used + 1 by a heard channel

    // Records the whole song (as it plays, muting and soloing included) at a sample
    // rate: stereo, or one stereo pair per track in use. Call from a worker thread.
    AudioDataPtr record (double sampleRate, bool multiChannel, std::string& error,
                         std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr) const;

private:
    class Player;
    void applyMutes();

    std::unique_ptr<XModule> module;
    std::string filePath;
    std::unique_ptr<Player> live;
    mutable std::mutex liveLock;   // the audio thread only tries it, and plays silence when busy
    std::vector<bool> muted, soloed;
    std::vector<int> tracks;
};
