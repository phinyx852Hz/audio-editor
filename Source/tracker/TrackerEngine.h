// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class XModule;
struct TXMPattern;

// The Tracker's sound: a module (XM, MOD, IT, S3M and the other formats MilkyTracker
// reads) played by MilkyPlay, MilkyTracker's player library. It plays live through
// PHI's audio engine (render() on the audio thread) and records into new audio,
// either stereo or one stereo pair per mixer track.
class TrackerEngine
{
public:
    static constexpr int numTracks = 8;   // the mixer's tracks; a recording has up to 8 stereo pairs
    struct LinkRenderer;                  // below, with the linked instruments

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
    // With a renderer, the linked instruments are recorded with it.
    AudioDataPtr record (double sampleRate, bool multiChannel, std::string& error,
                         std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr,
                         LinkRenderer* linked = nullptr) const;

    //==========================================================================
    // Editing, adapted from MilkyTracker's ModuleEditor and PatternEditor (TrackerEdit.cpp).
    // Everything works on XM in memory; a module in another format is converted on the
    // first edit, the way MilkyTracker opens it. Edits may happen while the song plays.

    // A cell's columns, in the order the cursor moves through them in FastTracker 2.
    enum Column { noteColumn, instrumentHigh, instrumentLow, volumeHigh, volumeLow, effectColumn, operandHigh, operandLow, numColumns };
    static constexpr int keyOff = 121;        // XModule::NOTE_OFF
    static constexpr int maxChannels = 32;
    static constexpr int maxPatterns = 256;
    static constexpr int numInstruments = 128;

    void newSong (int channels = 8);
    std::string save (const std::string& path);   // as XM; an error, or empty
    bool isModified() const { return modified; }
    bool isEditable() const;
    std::string makeEditable();                    // an error, or empty

    // Cells; each returns false when nothing changed. Instruments count from 1.
    bool writeNote (int pattern, int row, int channel, int note, int instrument);    // note 1 to 96, or keyOff
    // A digit typed at a column: 0 to 15 for the hex columns; the effect column takes
    // FastTracker 2's effect numbers (0 to 35, 0-9 then A-Z); the volume column's first
    // digit also takes its commands: 5 '-', 6 '+', 7 'D', 8 'U', 9 'S', 10 'V', 11 'P', 12 'L', 13 'R', 14 'M'.
    bool writeDigit (int pattern, int row, int channel, int column, int value);
    bool clearAt (int pattern, int row, int channel, int column);   // what Delete clears there
    bool clearCell (int pattern, int row, int channel);
    bool insertRow (int pattern, int row, int channel);            // channel -1: every channel
    bool deleteRow (int pattern, int row, int channel);
    bool transpose (int pattern, int row0, int row1, int channel0, int channel1, int semitones);

    // Blocks of cells, for copy and paste.
    struct Block
    {
        int rows = 0, channels = 0;
        std::vector<unsigned char> cells;   // note, instrument, volume effect, operand, effect, operand: 6 bytes a cell
        bool empty() const { return rows == 0 || channels == 0; }
    };
    Block copyBlock (int pattern, int row0, int row1, int channel0, int channel1) const;
    bool pasteBlock (int pattern, int row, int channel, const Block&);
    bool clearBlock (int pattern, int row0, int row1, int channel0, int channel1);

    // The song.
    void setTitle (const std::string&);
    void setInitialTempo (int bpm);
    void setInitialSpeed (int ticksPerRow);
    void setNumChannels (int);                     // 2 to 32, in pairs as FastTracker 2 has them
    bool setPatternRows (int pattern, int rows);   // 1 to 256
    int unusedPattern() const;                     // the first pattern number nothing uses, or -1
    bool insertOrder (int at, int pattern);        // a new order position before `at`
    bool deleteOrder (int at);
    bool setOrderPattern (int at, int pattern);

    // Instruments.
    void setInstrumentName (int instrument, const std::string&);
    bool hasSample (int instrument) const;
    // Puts audio (mixed to mono, 16-bit) into an instrument's first sample, tuned so
    // C-4 plays it at its own pitch.
    std::string loadSample (int instrument, const AudioData&, const std::string& name);

    bool undo();
    bool redo();
    bool canUndo() const { return ! undoSteps.empty(); }
    bool canRedo() const { return ! redoSteps.empty(); }

    // Plays a note on a channel as it is typed (or jammed), through the live player;
    // starts an idle player when nothing plays.
    void previewNote (int channel, int note, int instrument, double sampleRate);
    void releaseNote (int channel, int instrument);

    //==========================================================================
    // Linked instruments: an instrument can play through one of the Retro Sampler's
    // (Ami's) slots instead of its own sample. Its notes become MIDI notes on the slot's
    // MIDI channel (slot 3: channel 3), C-4 as MIDI note 60, the volume as velocity,
    // timed to the tick; key off and note cut let go. Everything else on the channel
    // (effects, other instruments) plays as before.
    static constexpr int numSlots = 12;

    // Where the notes go. Live, from the audio thread inside render(), `offset` counts
    // frames from the start of that render() call and can run past its end (the player
    // works ahead); notes typed in come from the UI thread with offset 0.
    struct LinkSink
    {
        virtual ~LinkSink() = default;
        virtual void linkedNote (int offset, int midiChannel, int note, int velocity) = 0;   // velocity 0: note-off
    };
    void setLinkSink (LinkSink*);   // the live player's; null: linked notes go nowhere

    // slot 1..numSlots, or 0 to play the instrument's own sample again. An instrument
    // with no sample gets a short silent one, so MilkyPlay plays its notes (and an XM
    // saved from here still has the notes' instrument).
    std::string setLink (int instrument, int slot);   // an error, or empty
    int link (int instrument) const;
    bool hasLinks() const;
    std::vector<std::pair<int, int>> links() const;   // (instrument, slot)
    void clearLinks();

    // Renders what the linked instruments play for record(): another Ami, say.
    struct LinkRenderer
    {
        virtual ~LinkRenderer() = default;
        virtual void reset() = 0;   // before each pass: silent, notes off
        // Plays the notes (offsets within this block, in order) and adds the sound to left and right.
        virtual void render (const std::vector<std::array<int, 4>>& notes, float* left, float* right, int frames) = 0;   // offset, channel, note, velocity
    };

private:
    class Player;
    void applyMutes();

    // Editing (TrackerEdit.cpp), called with liveLock held.
    struct Snapshot
    {
        int pattern = -1;   // -1: the song only
        int rows = 0, effnum = 0, channum = 0;
        std::vector<unsigned char> data;
        int ordnum = 0, restart = 0, patnum = 0, songChannels = 0;
        std::vector<unsigned char> orders;
    };
    Snapshot capture (int pattern) const;
    void restore (const Snapshot&);
    void remember (int pattern);   // before an edit: one undo step
    TXMPattern* editablePattern (int index);
    unsigned char* slotAt (int pattern, int row, int channel);
    void prepareInstruments();
    void resetChannels();

    std::vector<Snapshot> undoSteps, redoSteps;
    bool modified = false;

    std::unique_ptr<XModule> module;
    std::string filePath;
    std::unique_ptr<Player> live;
    mutable std::mutex liveLock;   // the audio thread only tries it, and plays silence when busy
    std::vector<bool> muted, soloed;
    std::vector<int> tracks;
    std::array<std::atomic<int>, numInstruments + 1> linked {};   // by instrument: the slot, or 0
    std::atomic<LinkSink*> sink { nullptr };
};
