// SPDX-License-Identifier: GPL-3.0-or-later
// Editing for the Tracker. Adapted from MilkyTracker's tracker/ModuleEditor.cpp and
// tracker/PatternEditor.cpp (Copyright 2009 Peter Barth, github.com/milkytracker/MilkyTracker,
// GPL-3.0-or-later), used in PHINYX (AGPL-3.0) with credit: the same pattern layout,
// new-song set-up, order list operations, the FastTracker 2 volume and effect column
// rules and the row insert and delete, rewritten around PHINYX's TrackerEngine.
#include "TrackerPlayer.h"
#include "PatternText.h"

#include "MilkyPlay.h"
#include "version.h"

#include <QDir>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr int slotSize = 6;   // note, instrument, volume column effect and operand, effect and operand
    constexpr int maxUndo = 500;

    // MilkyTracker's ModuleEditor::allocatePattern, at the song's width.
    bool allocate (TXMPattern& p, int rows, int channels)
    {
        p.channum = (mp_ubyte) channels;
        p.rows = (mp_uword) rows;
        p.effnum = 2;
        const auto size = (size_t) (slotSize * channels * rows);
        p.patternData = new (std::nothrow) mp_ubyte[size];
        if (p.patternData == nullptr)
            return false;
        std::memset (p.patternData, 0, size);
        return true;
    }

    // Copies a pattern into a new layout of `channels` channels and `rows` rows with two effects.
    void reshape (TXMPattern& p, int rows, int channels)
    {
        const int oldSlot = 2 + p.effnum * 2;
        auto* data = new mp_ubyte[(size_t) (slotSize * channels * rows)];
        std::memset (data, 0, (size_t) (slotSize * channels * rows));
        for (int r = 0; r < std::min<int> (rows, p.rows); ++r)
            for (int c = 0; c < std::min<int> (channels, p.channum); ++c)
            {
                const auto* src = p.patternData + (r * p.channum + c) * oldSlot;
                auto* dst = data + (r * channels + c) * slotSize;
                dst[0] = src[0];
                dst[1] = src[1];
                if (p.effnum == 1)
                {
                    dst[4] = src[2];   // a lone effect goes to the effect column
                    dst[5] = src[3];
                }
                else
                    std::memcpy (dst + 2, src + 2, (size_t) std::min (4, oldSlot - 2));
            }
        delete[] p.patternData;
        p.patternData = data;
        p.rows = (mp_uword) rows;
        p.channum = (mp_ubyte) channels;
        p.effnum = 2;
    }

    int patternSize (const TXMPattern& p) { return (2 + p.effnum * 2) * p.channum * p.rows; }

    // The volume column number (0x10..0xFF, FastTracker 2's) of a cell's first effect.
    int ft2Volume (const mp_ubyte* slot)
    {
        int eff = slot[2], op = slot[3];
        PatternText::convertEffectsToFT2 (eff, op);
        return PatternText::getVolumeFromEffect (eff, op);
    }
}

//==============================================================================
bool TrackerEngine::isEditable() const
{
    return module != nullptr && module->getType() == XModule::ModuleType_XM;
}

void TrackerEngine::prepareInstruments()
{
    // From ModuleEditor::convertInstrument: every instrument owns samples i*16 to i*16+15,
    // and a note with no sample of its own plays the instrument's first.
    module->header.insnum = std::max<int> (module->header.insnum, numInstruments);
    module->header.smpnum = std::max<int> (module->header.smpnum, module->header.insnum * 16);
    for (int i = 0; i < module->header.insnum; ++i)
    {
        auto& ins = module->instr[i];
        for (int j = 0; j < 120; ++j)
            if (ins.snum[j] < i * 16 || ins.snum[j] >= i * 16 + 16)
                ins.snum[j] = (mp_sword) (i * 16);
        ins.samp = 16;
        for (int j = 0; j < 16; ++j)
        {
            auto& s = module->smp[i * 16 + j];
            if (s.sample == nullptr)
            {
                s.vol = 0xFF;
                s.pan = 0x80;
            }
            s.flags = 3;
        }
    }
}

void TrackerEngine::newSong (int channels)
{
    channels = std::clamp (channels + (channels & 1), 2, maxChannels);
    auto song = std::make_unique<XModule>();
    song->createEmptySong (true, true, channels);
    allocate (song->phead[0], 64, channels);
    song->header.patnum = 1;
    song->header.ordnum = 1;
    song->header.ord[0] = 0;

    {
        std::lock_guard lock (liveLock);
        live.reset();
        module = std::move (song);
        prepareInstruments();
        undoSteps.clear();
        redoSteps.clear();
        modified = false;
    }
    filePath.clear();
    resetChannels();
    clearLinks();
}

std::string TrackerEngine::makeEditable()
{
    if (module == nullptr)
        return "No module is open.";
    if (isEditable())
        return {};

    // As MilkyTracker opens a module that is not XM: write it as XM and read that back.
    QTemporaryFile temp (QDir::tempPath() + QStringLiteral ("/phinyx-tracker-XXXXXX.xm"));
    if (! temp.open())
        return "Could not make a temporary file to convert the module.";
    const auto tempPath = temp.fileName().toStdString();
    temp.close();

    std::lock_guard lock (liveLock);
    live.reset();
    if (module->saveExtendedModule (tempPath.c_str(), MILKYTRACKER_VERSION_STRING) != MP_OK)
        return "The module could not be converted to XM for editing.";
    auto converted = std::make_unique<XModule>();
    if (converted->loadModule (tempPath.c_str()) != MP_OK)
        return "The module could not be converted to XM for editing.";
    module = std::move (converted);
    for (int i = 0; i < module->header.patnum; ++i)
        if (auto& p = module->phead[i]; p.patternData != nullptr && (p.effnum != 2 || p.channum < module->header.channum))
            reshape (p, p.rows, std::max<int> (p.channum, module->header.channum));
    prepareInstruments();
    undoSteps.clear();
    redoSteps.clear();
    modified = true;
    return {};
}

std::string TrackerEngine::save (const std::string& path)
{
    if (module == nullptr)
        return "No module is open.";
    if (auto error = makeEditable(); ! error.empty())
        return error;
    std::lock_guard lock (liveLock);
    if (module->saveExtendedModule (path.c_str(), MILKYTRACKER_VERSION_STRING) != MP_OK)
        return "The song could not be written to " + path + ".";
    filePath = path;
    modified = false;
    return {};
}

//==============================================================================
// Undo: a copy of the song's order list and of the one pattern an edit touches.
TrackerEngine::Snapshot TrackerEngine::capture (int index) const
{
    Snapshot s;
    s.pattern = index;
    const auto& h = module->header;
    s.ordnum = h.ordnum;
    s.restart = h.restart;
    s.patnum = h.patnum;
    s.songChannels = h.channum;
    s.orders.assign (h.ord, h.ord + 256);
    if (index >= 0 && index < maxPatterns && module->phead[index].patternData != nullptr)
    {
        const auto& p = module->phead[index];
        s.rows = p.rows;
        s.effnum = p.effnum;
        s.channum = p.channum;
        s.data.assign (p.patternData, p.patternData + patternSize (p));
    }
    return s;
}

void TrackerEngine::restore (const Snapshot& s)
{
    auto& h = module->header;
    h.ordnum = (mp_uword) s.ordnum;
    h.restart = (mp_uword) s.restart;
    h.patnum = (mp_uword) s.patnum;
    h.channum = (mp_uword) s.songChannels;
    std::copy (s.orders.begin(), s.orders.end(), h.ord);
    if (s.pattern < 0 || s.pattern >= maxPatterns)
        return;
    auto& p = module->phead[s.pattern];
    delete[] p.patternData;
    p.patternData = nullptr;
    p.rows = (mp_uword) s.rows;
    p.effnum = (mp_ubyte) s.effnum;
    p.channum = (mp_ubyte) s.channum;
    if (! s.data.empty())
    {
        p.patternData = new mp_ubyte[s.data.size()];
        std::copy (s.data.begin(), s.data.end(), p.patternData);
    }
}

void TrackerEngine::remember (int index)
{
    undoSteps.push_back (capture (index));
    if ((int) undoSteps.size() > maxUndo)
        undoSteps.erase (undoSteps.begin());
    redoSteps.clear();
    modified = true;
}

bool TrackerEngine::undo()
{
    std::lock_guard lock (liveLock);
    if (module == nullptr || undoSteps.empty())
        return false;
    auto step = std::move (undoSteps.back());
    undoSteps.pop_back();
    redoSteps.push_back (capture (step.pattern));
    restore (step);
    modified = true;
    return true;
}

bool TrackerEngine::redo()
{
    std::lock_guard lock (liveLock);
    if (module == nullptr || redoSteps.empty())
        return false;
    auto step = std::move (redoSteps.back());
    redoSteps.pop_back();
    undoSteps.push_back (capture (step.pattern));
    restore (step);
    modified = true;
    return true;
}

//==============================================================================
// From ModuleEditor::getPattern: a pattern asked for is made if it is empty, and is
// at least as wide as the song.
TXMPattern* TrackerEngine::editablePattern (int index)
{
    if (module == nullptr || index < 0 || index >= maxPatterns)
        return nullptr;
    auto& p = module->phead[index];
    const int channels = module->header.channum;
    if (p.patternData == nullptr)
    {
        if (! allocate (p, 64, channels))
            return nullptr;
    }
    else if (p.effnum != 2 || p.channum < channels)
        reshape (p, p.rows, std::max<int> (p.channum, channels));
    if (module->header.patnum < index + 1)
        module->header.patnum = (mp_uword) (index + 1);
    return &p;
}

unsigned char* TrackerEngine::slotAt (int index, int row, int channel)
{
    auto* p = editablePattern (index);
    if (p == nullptr || row < 0 || row >= p->rows || channel < 0 || channel >= p->channum)
        return nullptr;
    return p->patternData + (row * p->channum + channel) * slotSize;
}

// PatternEditor::writeNote.
bool TrackerEngine::writeNote (int index, int row, int channel, int note, int instrument)
{
    if (! isEditable() || (note != keyOff && (note < 1 || note > 96)))
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* slot = slotAt (index, row, channel);
    if (slot == nullptr)
    {
        undoSteps.pop_back();
        return false;
    }
    slot[0] = (mp_ubyte) note;
    if (note != keyOff && instrument > 0)
        slot[1] = (mp_ubyte) instrument;
    return true;
}

// PatternEditor::writeInstrument, writeFT2Volume, writeEffectNumber and writeEffectOperand.
bool TrackerEngine::writeDigit (int index, int row, int channel, int column, int value)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* slot = slotAt (index, row, channel);
    auto fail = [this] { undoSteps.pop_back(); return false; };
    if (slot == nullptr || value < 0)
        return fail();

    switch (column)
    {
        case instrumentHigh:
        case instrumentLow:
        {
            if (value > 15)
                return fail();
            int i = slot[1];
            i = column == instrumentHigh ? (i & 0x0F) | (value << 4) : (i & 0xF0) | value;
            slot[1] = (mp_ubyte) std::min (i, numInstruments);
            return true;
        }

        case volumeHigh:
        case volumeLow:
        {
            if (value > 15 || (column == volumeHigh && value > 14))
                return fail();
            int volume = std::max (0, ft2Volume (slot) - 0x10);
            if (volume >= 0xF0)
                return fail();
            volume = column == volumeHigh ? (volume & 0x0F) | (value << 4) : (volume & 0xF0) | value;
            if (volume >= 0xF0)
                return fail();
            if (volume > 0x40 && volume < 0x50)
                volume = 0x40;
            // These commands take no zero operand.
            if (volume >= 0x50 && (volume & 0xF) == 0)
                switch ((volume + 0x10) >> 4)
                {
                    case 0x6: case 0x7: case 0x8: case 0x9: case 0xa: case 0xd: case 0xe:
                        volume |= 1;
                        break;
                    default:
                        break;
                }
            int eff = 0, op = 0;
            PatternText::convertVolumeToEffect (volume + 0x10, eff, op);
            slot[2] = (mp_ubyte) eff;
            slot[3] = (mp_ubyte) op;
            return true;
        }

        case effectColumn:
        {
            if (value > 35)
                return fail();
            int eff = slot[4], op = slot[5];
            PatternText::convertEffectsToFT2 (eff, op);
            int newEff = value;
            PatternText::convertEffectsFromFT2 (newEff, op);
            if (newEff == 0 && value != 0)
                return fail();   // not an XM effect
            if (newEff == 0x40)
            {
                newEff = 0x41;
                op &= 0x0F;
            }
            else if (newEff >= 0x43)
            {
                newEff = 0x42;
                op &= 0x0F;
            }
            // Effect 0 with no operand is arpeggio; it shows once an operand is typed.
            slot[4] = (mp_ubyte) (value == 0 && op == 0 ? 0 : newEff);
            slot[5] = (mp_ubyte) op;
            return true;
        }

        case operandHigh:
        case operandLow:
        {
            if (value > 15)
                return fail();
            int eff = slot[4], op = slot[5];
            PatternText::convertEffectsToFT2 (eff, op);
            op = column == operandHigh ? (op & 0x0F) | (value << 4) : (op & 0xF0) | value;
            PatternText::convertEffectsFromFT2 (eff, op);
            if (eff == 0x40)
                eff = 0x41;
            if (eff >= 0x43)
                eff = 0x42;
            slot[4] = (mp_ubyte) eff;
            slot[5] = (mp_ubyte) op;
            return true;
        }

        default:
            return fail();
    }
}

// PatternEditor::deleteCursorSlotData.
bool TrackerEngine::clearAt (int index, int row, int channel, int column)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* slot = slotAt (index, row, channel);
    if (slot == nullptr)
    {
        undoSteps.pop_back();
        return false;
    }
    if (column == volumeHigh || column == volumeLow)
        slot[2] = slot[3] = 0;
    else if (column >= effectColumn)
        slot[4] = slot[5] = 0;
    else
        slot[0] = slot[1] = 0;
    return true;
}

bool TrackerEngine::clearCell (int index, int row, int channel)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* slot = slotAt (index, row, channel);
    if (slot == nullptr)
    {
        undoSteps.pop_back();
        return false;
    }
    std::memset (slot, 0, slotSize);
    return true;
}

// PatternEditor::insertNote and insertLine.
bool TrackerEngine::insertRow (int index, int row, int channel)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* p = editablePattern (index);
    if (p == nullptr || row < 0 || row >= p->rows || channel >= p->channum)
    {
        undoSteps.pop_back();
        return false;
    }
    const int rowSize = slotSize * p->channum;
    const int offset = channel < 0 ? 0 : channel * slotSize;
    const int size = channel < 0 ? rowSize : slotSize;
    for (int i = p->rows - 1; i > row; --i)
        std::memcpy (p->patternData + i * rowSize + offset, p->patternData + (i - 1) * rowSize + offset, (size_t) size);
    std::memset (p->patternData + row * rowSize + offset, 0, (size_t) size);
    return true;
}

// PatternEditor::deleteNote and deleteLine.
bool TrackerEngine::deleteRow (int index, int row, int channel)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* p = editablePattern (index);
    if (p == nullptr || row < 0 || row >= p->rows || channel >= p->channum)
    {
        undoSteps.pop_back();
        return false;
    }
    const int rowSize = slotSize * p->channum;
    const int offset = channel < 0 ? 0 : channel * slotSize;
    const int size = channel < 0 ? rowSize : slotSize;
    for (int i = row; i < p->rows - 1; ++i)
        std::memcpy (p->patternData + i * rowSize + offset, p->patternData + (i + 1) * rowSize + offset, (size_t) size);
    std::memset (p->patternData + (p->rows - 1) * rowSize + offset, 0, (size_t) size);
    return true;
}

bool TrackerEngine::transpose (int index, int row0, int row1, int channel0, int channel1, int semitones)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* p = editablePattern (index);
    bool changed = false;
    if (p != nullptr)
        for (int r = std::max (0, row0); r <= std::min<int> (row1, p->rows - 1); ++r)
            for (int c = std::max (0, channel0); c <= std::min<int> (channel1, p->channum - 1); ++c)
            {
                auto& note = p->patternData[(r * p->channum + c) * slotSize];
                if (note >= 1 && note <= 96)
                {
                    const int moved = note + semitones;
                    if (moved >= 1 && moved <= 96)
                    {
                        note = (mp_ubyte) moved;
                        changed = true;
                    }
                }
            }
    if (! changed)
        undoSteps.pop_back();
    return changed;
}

//==============================================================================
TrackerEngine::Block TrackerEngine::copyBlock (int index, int row0, int row1, int channel0, int channel1) const
{
    Block b;
    const auto* p = pattern (index);
    if (p == nullptr)
        return b;
    row0 = std::max (0, row0);
    row1 = std::min<int> (row1, p->rows - 1);
    channel0 = std::max (0, channel0);
    channel1 = std::min<int> (channel1, p->channum - 1);
    if (row1 < row0 || channel1 < channel0)
        return b;
    b.rows = row1 - row0 + 1;
    b.channels = channel1 - channel0 + 1;
    const int sourceSlot = 2 + p->effnum * 2;
    b.cells.assign ((size_t) (b.rows * b.channels * slotSize), 0);
    for (int r = 0; r < b.rows; ++r)
        for (int c = 0; c < b.channels; ++c)
        {
            const auto* src = p->patternData + ((row0 + r) * p->channum + channel0 + c) * sourceSlot;
            std::memcpy (b.cells.data() + (r * b.channels + c) * slotSize, src, (size_t) std::min (slotSize, sourceSlot));
        }
    return b;
}

bool TrackerEngine::pasteBlock (int index, int row, int channel, const Block& b)
{
    if (! isEditable() || b.empty())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* p = editablePattern (index);
    if (p == nullptr || row < 0 || row >= p->rows || channel < 0 || channel >= p->channum)
    {
        undoSteps.pop_back();
        return false;
    }
    for (int r = 0; r < b.rows && row + r < p->rows; ++r)
        for (int c = 0; c < b.channels && channel + c < p->channum; ++c)
            std::memcpy (p->patternData + ((row + r) * p->channum + channel + c) * slotSize,
                         b.cells.data() + (r * b.channels + c) * slotSize, slotSize);
    return true;
}

bool TrackerEngine::clearBlock (int index, int row0, int row1, int channel0, int channel1)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    remember (index);
    auto* p = editablePattern (index);
    if (p == nullptr)
    {
        undoSteps.pop_back();
        return false;
    }
    for (int r = std::max (0, row0); r <= std::min<int> (row1, p->rows - 1); ++r)
        for (int c = std::max (0, channel0); c <= std::min<int> (channel1, p->channum - 1); ++c)
            std::memset (p->patternData + (r * p->channum + c) * slotSize, 0, slotSize);
    return true;
}

//==============================================================================
void TrackerEngine::setTitle (const std::string& text)
{
    if (! isEditable())
        return;
    std::lock_guard lock (liveLock);
    std::memset (module->header.name, 0, sizeof (module->header.name));
    std::memcpy (module->header.name, text.data(), std::min (text.size(), (size_t) 20));   // XM keeps 20 characters
    modified = true;
}

void TrackerEngine::setInitialTempo (int bpm)
{
    if (! isEditable())
        return;
    std::lock_guard lock (liveLock);
    module->header.speed = (mp_uword) std::clamp (bpm, 32, 255);   // MilkyPlay: speed is the BPM
    modified = true;
}

void TrackerEngine::setInitialSpeed (int ticks)
{
    if (! isEditable())
        return;
    std::lock_guard lock (liveLock);
    module->header.tempo = (mp_uword) std::clamp (ticks, 1, 31);   // and tempo the ticks per row
    modified = true;
}

// ModuleEditor::setNumChannels; patterns keep what is in channels that go, as MilkyTracker does.
void TrackerEngine::setNumChannels (int channels)
{
    if (! isEditable())
        return;
    channels = std::clamp (channels + (channels & 1), 2, maxChannels);
    std::lock_guard lock (liveLock);
    remember (-1);
    module->header.channum = (mp_uword) channels;
    for (int i = 0; i < module->header.patnum; ++i)
        if (auto& p = module->phead[i]; p.patternData != nullptr && p.channum < channels)
            reshape (p, p.rows, channels);
    undoSteps.clear();   // a wider pattern cannot be undone piecewise
}

bool TrackerEngine::setPatternRows (int index, int rows)
{
    if (! isEditable())
        return false;
    rows = std::clamp (rows, 1, 256);
    std::lock_guard lock (liveLock);
    remember (index);
    auto* p = editablePattern (index);
    if (p == nullptr || p->rows == rows)
    {
        undoSteps.pop_back();
        return false;
    }
    reshape (*p, rows, p->channum);
    return true;
}

int TrackerEngine::unusedPattern() const
{
    if (module == nullptr)
        return -1;
    for (int i = 0; i < maxPatterns; ++i)
    {
        bool used = false;
        for (int o = 0; o < module->header.ordnum && ! used; ++o)
            used = module->header.ord[o] == i;
        const auto& p = module->phead[i];
        bool blank = true;
        if (p.patternData != nullptr)
            for (int b = 0; b < patternSize (p) && blank; ++b)
                blank = p.patternData[b] == 0;
        if (! used && blank)
            return i;
    }
    return -1;
}

// ModuleEditor::insertNewOrderPosition and seqCurrentOrderPosition.
bool TrackerEngine::insertOrder (int at, int index)
{
    if (! isEditable() || index < 0 || index >= maxPatterns)
        return false;
    std::lock_guard lock (liveLock);
    auto& h = module->header;
    if (h.ordnum >= 255)
        return false;
    remember (index);
    at = std::clamp (at, 0, (int) h.ordnum);
    for (int i = h.ordnum; i > at; --i)
        h.ord[i] = h.ord[i - 1];
    h.ord[at] = (mp_ubyte) index;
    ++h.ordnum;
    editablePattern (index);
    return true;
}

bool TrackerEngine::deleteOrder (int at)
{
    if (! isEditable())
        return false;
    std::lock_guard lock (liveLock);
    auto& h = module->header;
    if (at < 0 || at >= h.ordnum || h.ordnum <= 1)
        return false;
    remember (-1);
    for (int i = at; i < h.ordnum - 1; ++i)
        h.ord[i] = h.ord[i + 1];
    --h.ordnum;
    if (h.restart >= h.ordnum)
        h.restart = 0;
    return true;
}

bool TrackerEngine::setOrderPattern (int at, int index)
{
    if (! isEditable() || index < 0 || index >= maxPatterns)
        return false;
    std::lock_guard lock (liveLock);
    auto& h = module->header;
    if (at < 0 || at >= h.ordnum || h.ord[at] == index)
        return false;
    remember (index);
    h.ord[at] = (mp_ubyte) index;
    editablePattern (index);
    return true;
}

//==============================================================================
void TrackerEngine::setInstrumentName (int instrument, const std::string& name)
{
    if (! isEditable() || instrument < 1 || instrument > module->header.insnum)
        return;
    std::lock_guard lock (liveLock);
    auto& ins = module->instr[instrument - 1];
    std::memset (ins.name, 0, sizeof (ins.name));
    std::memcpy (ins.name, name.data(), std::min (name.size(), (size_t) 22));   // XM keeps 22 characters
    modified = true;
}

bool TrackerEngine::hasSample (int instrument) const
{
    if (module == nullptr || instrument < 1 || instrument > module->header.insnum)
        return false;
    for (int j = 0; j < 96; ++j)
        if (const auto s = module->instr[instrument - 1].snum[j]; s >= 0 && module->smp[s].sample != nullptr)
            return true;
    return false;
}

std::string TrackerEngine::loadSample (int instrument, const AudioData& audio, const std::string& name)
{
    if (auto error = makeEditable(); ! error.empty())
        return error;
    if (instrument < 1 || instrument > numInstruments)
        return "Pick an instrument from 01 to 80.";
    if (audio.length() == 0 || audio.numChannels() == 0)
        return "The file has no audio.";

    // Mixed to mono and rounded to 16 bits; at most about 6 minutes at 44.1 kHz.
    const auto length = (int) std::min<Frame> (audio.length(), 1 << 24);
    std::vector<std::vector<float>> channels ((size_t) audio.numChannels(), std::vector<float> ((size_t) length));
    std::vector<float*> pointers;
    for (auto& c : channels)
        pointers.push_back (c.data());
    audio.read (0, length, pointers.data());

    std::lock_guard lock (liveLock);
    const int s = (instrument - 1) * 16;
    auto& sample = module->smp[s];
    auto* memory = module->allocSampleMem ((mp_uint32) length * 2);
    if (memory == nullptr)
        return "Not enough memory for the sample.";
    auto* out = reinterpret_cast<mp_sword*> (memory);
    for (int i = 0; i < length; ++i)
    {
        float sum = 0.0f;
        for (const auto& c : channels)
            sum += c[(size_t) i];
        out[i] = (mp_sword) std::clamp (std::lround (sum / (float) channels.size() * 32767.0f), -32768L, 32767L);
    }
    if (sample.sample != nullptr)
        module->freeSampleMem ((mp_ubyte*) sample.sample);
    sample.sample = (mp_sbyte*) memory;
    sample.samplen = (mp_uint32) length;
    sample.loopstart = sample.looplen = 0;
    sample.type = 16;   // 16-bit, no loop
    sample.vol = 0xFF;
    sample.pan = 0x80;
    sample.flags = 3;
    sample.volfade = 65535;
    XModule::convertc4spd ((mp_uint32) std::lround (audio.sampleRate()), &sample.finetune, &sample.relnote);
    std::memset (sample.name, 0, sizeof (sample.name));
    std::memcpy (sample.name, name.data(), std::min (name.size(), (size_t) 22));

    auto& ins = module->instr[instrument - 1];
    for (int j = 0; j < 120; ++j)
        ins.snum[j] = (mp_sword) s;
    ins.samp = 16;
    if (ins.name[0] == 0)
        std::memcpy (ins.name, name.data(), std::min (name.size(), (size_t) 22));
    module->header.insnum = std::max<int> (module->header.insnum, instrument);
    module->postProcessSamples();
    modified = true;
    return {};
}

//==============================================================================
void TrackerEngine::previewNote (int channel, int note, int instrument, double sampleRate)
{
    if (module == nullptr || instrument < 1 || note < 1 || (note > 96 && note != keyOff))
        return;
    std::lock_guard lock (liveLock);
    if (live == nullptr || live->halted())
    {
        live = std::make_unique<Player> (*module, sampleRate, &linked, &sink);
        live->start (false, 0, 0, -1, true);
    }
    live->playNote (std::clamp (channel, 0, std::max (0, numChannels() - 1)), note, instrument);
}

void TrackerEngine::releaseNote (int channel, int instrument)
{
    if (module == nullptr || instrument < 1)
        return;
    std::lock_guard lock (liveLock);
    if (live != nullptr)
        live->playNote (std::clamp (channel, 0, std::max (0, numChannels() - 1)), keyOff, instrument);
}

//==============================================================================
void TrackerEngine::setLinkSink (LinkSink* s) { sink = s; }

std::string TrackerEngine::setLink (int instrument, int slot)
{
    if (instrument < 1 || instrument > numInstruments || slot < 0 || slot > numSlots)
        return "Pick an instrument from 01 to 80 and a slot from 1 to 12.";
    if (slot > 0 && ! hasSample (instrument))
    {
        // A short silence: MilkyPlay plays notes only of instruments with a sample.
        AudioData::Builder silence (1, 8363.0);
        const std::vector<float> zeros (32, 0.0f);
        const float* channel = zeros.data();
        silence.append (&channel, (int) zeros.size());
        // loadSample names an unnamed instrument after its sample.
        if (auto error = loadSample (instrument, *silence.finish(), "Retro Sampler " + std::to_string (slot)); ! error.empty())
            return error;
    }
    linked[(size_t) instrument].store (slot);
    return {};
}

int TrackerEngine::link (int instrument) const
{
    return instrument >= 1 && instrument <= numInstruments ? linked[(size_t) instrument].load() : 0;
}

bool TrackerEngine::hasLinks() const
{
    for (int i = 1; i <= numInstruments; ++i)
        if (linked[(size_t) i].load() > 0)
            return true;
    return false;
}

std::vector<std::pair<int, int>> TrackerEngine::links() const
{
    std::vector<std::pair<int, int>> out;
    for (int i = 1; i <= numInstruments; ++i)
        if (const auto s = linked[(size_t) i].load(); s > 0)
            out.emplace_back (i, s);
    return out;
}

void TrackerEngine::clearLinks()
{
    for (auto& l : linked)
        l.store (0);
}
