// SPDX-License-Identifier: AGPL-3.0-only
#include "Sonify.h"

#include <algorithm>
#include <cmath>

namespace sonify
{
namespace
{
    // The pitch classes of each scale, from its root.
    const std::vector<int>& intervals (Scale s)
    {
        static const std::vector<int> table[] {
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },   // chromatic
            { 0, 2, 4, 5, 7, 9, 11 },                   // major
            { 0, 2, 3, 5, 7, 8, 10 },                   // natural minor
            { 0, 2, 4, 7, 9 },                          // major pentatonic
            { 0, 3, 5, 7, 10 },                         // minor pentatonic
            { 0, 3, 5, 6, 7, 10 },                      // blues
            { 0, 2, 3, 5, 7, 9, 10 },                   // dorian
            { 0, 2, 4, 6, 8, 10 },                      // whole tone
        };
        return table[std::clamp ((int) s, 0, (int) Scale::wholeTone)];
    }

    constexpr double phi = 1.6180339887498949;

    // The phi scale: phi^0 .. phi^7 as pitches above the root, folded into one octave.
    const std::vector<double>& phiSteps()
    {
        static const std::vector<double> steps = []
        {
            std::vector<double> out;
            for (int n = 0; n < 8; ++n)
            {
                const double cents = n * std::log2 (phi) * 12.0;
                out.push_back (cents - 12.0 * std::floor (cents / 12.0));
            }
            std::sort (out.begin(), out.end());
            return out;
        }();
        return steps;
    }

    // General MIDI drums for the bottom, middle and top of the picture.
    constexpr int kick = 36, snare = 38, hat = 42;

    // The mean brightness of each row between columns x0 and x1.
    std::vector<float> rows (const Picture& p, int x0, int x1)
    {
        std::vector<float> out ((size_t) p.height, 0.0f);
        x0 = std::clamp (x0, 0, p.width - 1);
        x1 = std::clamp (x1, x0 + 1, p.width);
        for (int y = 0; y < p.height; ++y)
        {
            float sum = 0.0f;
            for (int x = x0; x < x1; ++x)
                sum += p.at (x, y);
            out[(size_t) y] = sum / (float) (x1 - x0);
        }
        return out;
    }

    uint8_t velocity (float level) { return (uint8_t) std::clamp ((int) std::lround (30.0 + 97.0 * level), 1, 127); }
}

Picture fromPixels (const uint8_t* pixels, int width, int height, int stride, int w, int h)
{
    Picture p;
    if (pixels == nullptr || width <= 0 || height <= 0 || w <= 0 || h <= 0)
        return p;
    p.width = w;
    p.height = h;
    p.light.assign ((size_t) w * (size_t) h, 0.0f);
    for (int oy = 0; oy < h; ++oy)
    {
        const int y0 = oy * height / h, y1 = std::max (y0 + 1, (oy + 1) * height / h);
        for (int ox = 0; ox < w; ++ox)
        {
            const int x0 = ox * width / w, x1 = std::max (x0 + 1, (ox + 1) * width / w);
            double sum = 0.0;
            for (int y = y0; y < y1; ++y)
            {
                const auto* row = pixels + (size_t) y * (size_t) stride;
                for (int x = x0; x < x1; ++x)
                {
                    const auto* px = row + (size_t) x * 4;
                    sum += 0.0722 * px[0] + 0.7152 * px[1] + 0.2126 * px[2];   // Rec. 709 luma from B, G, R
                }
            }
            p.light[(size_t) oy * (size_t) w + (size_t) ox] = (float) (sum / (255.0 * (y1 - y0) * (x1 - x0)));
        }
    }
    return p;
}

const char* scaleName (Scale s)
{
    static const char* names[] { "Chromatic", "Major", "Minor", "Pentatonic", "Minor Pentatonic", "Blues", "Dorian", "Whole Tone", "\u03a6 (golden)" };
    return names[std::clamp ((int) s, 0, (int) Scale::count - 1)];
}

double nearestInScale (double note, Scale s, int root)
{
    if (s == Scale::phi)
    {
        double best = note, distance = 1e9;
        const int octave = (int) std::floor ((note - root) / 12.0);
        for (int o = octave - 1; o <= octave + 1; ++o)
            for (const double step : phiSteps())
                if (const double n = root + 12.0 * o + step; std::abs (n - note) < distance)
                {
                    distance = std::abs (n - note);
                    best = n;
                }
        return best;
    }
    const auto& steps = intervals (s);
    double best = std::round (note), distance = 1e9;
    const int around = (int) std::floor (note);
    for (int n = around - 12; n <= around + 12; ++n)
    {
        const int pc = ((n - root) % 12 + 12) % 12;
        if (std::find (steps.begin(), steps.end(), pc) == steps.end())
            continue;
        if (const auto d = std::abs (n - note); d < distance)
        {
            distance = d;
            best = n;
        }
    }
    return best;
}

bool fibonacciBeat (long step)
{
    // The lower Wythoff sequence's gaps: 2 where the word has a 1, 1 where it has a 0.
    const auto n = (double) (step + 1);
    return std::floor ((n + 1.0) * phi) - std::floor (n * phi) == 2.0;
}

double pitchFor (double height, const Settings& s)
{
    const double free = s.lowest + std::clamp (height, 0.0, 1.0) * (s.highest - s.lowest);
    const double snapped = nearestInScale (free, s.scale, s.root);
    return std::clamp (free + std::clamp (s.quantize, 0.0, 1.0) * (snapped - free), 0.0, 127.0);
}

//==============================================================================
void Sonifier::setSettings (const Settings& next)
{
    auto s = next;
    s.voices = std::clamp (s.voices, 1, maxVoices);
    s.columns = std::clamp (s.columns, 1, 256);
    s.bpm = std::clamp (s.bpm, 1.0, 999.0);
    s.lowest = std::clamp (s.lowest, 0, 126);
    s.highest = std::clamp (s.highest, s.lowest + 1, 127);
    s.bendRange = std::clamp (s.bendRange, 0.5, 96.0);
    s.threshold = std::clamp (s.threshold, 0.0, 0.99);
    set = s;
}

void Sonifier::advance (const Picture& picture, double seconds, std::vector<Message>& out)
{
    if (picture.empty())
        return;
    for (int v = set.voices; v < maxVoices; ++v)   // fewer voices than before
        release (v, out);

    const double stepLength = 15.0 / set.bpm;   // a sixteenth note
    clock += std::max (0.0, seconds);
    bool stepped = false;
    if (steps == 0)   // starting: the first step at once
    {
        clock = 0.0;
        stepped = true;
        step (picture, out);
    }
    for (int n = 0; clock >= stepLength && n < 4; ++n)
    {
        clock -= stepLength;
        stepped = true;
        step (picture, out);
    }
    clock = std::min (clock, stepLength);   // a long stall doesn't come back as a burst
    if (! stepped && set.mode == Mode::lanes)
        laneStep (picture, false, out);   // glides between the steps
    collectSpots();
}

void Sonifier::step (const Picture& picture, std::vector<Message>& out)
{
    if (set.mode == Mode::scan)
        scanStep (picture, out);
    else if (set.mode == Mode::spiral)
        spiralStep (picture, out);
    else
        laneStep (picture, true, out);
    ++steps;
}

double Sonifier::scanPosition() const
{
    if (set.mode != Mode::scan || steps == 0)
        return -1.0;
    const auto column = (double) ((steps - 1) % set.columns);
    return std::clamp ((column + clock * set.bpm / 15.0) / set.columns, 0.0, 1.0);
}

void Sonifier::scanStep (const Picture& p, std::vector<Message>& out)
{
    const int column = (int) (steps % set.columns);
    const int x0 = column * p.width / set.columns, x1 = std::max (x0 + 1, (column + 1) * p.width / set.columns);
    const auto profile = rows (p, x0, x1);
    const int h = p.height;

    // The bright spots down the column: peaks above the threshold, placed between rows.
    struct Peak { double y; float level; };
    std::vector<Peak> found;
    for (int y = 0; y < h; ++y)
    {
        const float v = profile[(size_t) y];
        if (v < (float) set.threshold || (y > 0 && profile[(size_t) y - 1] > v) || (y + 1 < h && profile[(size_t) y + 1] >= v))
            continue;
        double at = y;
        if (y > 0 && y + 1 < h)
        {
            const double a = profile[(size_t) y - 1], c = profile[(size_t) y + 1], curve = a - 2.0 * v + c;
            if (curve < 0.0)
                at += std::clamp (0.5 * (a - c) / curve, -0.5, 0.5);
        }
        found.push_back ({ at, v });
    }
    std::sort (found.begin(), found.end(), [] (const Peak& a, const Peak& b) { return a.level > b.level; });
    std::vector<Peak> chosen;
    const double apart = std::max (1.0, h / 16.0);
    for (const auto& f : found)
        if ((int) chosen.size() < set.voices
            && std::none_of (chosen.begin(), chosen.end(), [&] (const Peak& c) { return std::abs (c.y - f.y) < apart; }))
            chosen.push_back (f);
    std::sort (chosen.begin(), chosen.end(), [] (const Peak& a, const Peak& b) { return a.y > b.y; });   // lowest note first

    const float x = (float) ((x0 + x1) * 0.5 / p.width);
    for (int v = 0; v < set.voices; ++v)
    {
        if (v >= (int) chosen.size())
        {
            release (v, out);
            continue;
        }
        const auto& c = chosen[(size_t) v];
        const double height = h > 1 ? 1.0 - c.y / (h - 1) : 0.5;
        play (v, pitchFor (height, set), c.level, out);
        voice[v].x = x;
        voice[v].y = (float) ((c.y + 0.5) / h);
    }
    drumStep (profile, out);
}

void Sonifier::laneStep (const Picture& p, bool onStep, std::vector<Message>& out)
{
    const int h = p.height;
    for (int v = 0; v < set.voices; ++v)
    {
        auto& vc = voice[v];
        const int x0 = v * p.width / set.voices, x1 = std::max (x0 + 1, (v + 1) * p.width / set.voices);
        const auto profile = rows (p, x0, x1);

        // Where the lane's light is: the centre of what is above the threshold.
        float energy = 0.0f;
        double weight = 0.0, sum = 0.0;
        for (int y = 0; y < h; ++y)
        {
            const float l = profile[(size_t) y];
            energy = std::max (energy, l);
            if (l >= (float) set.threshold)
            {
                const double w = l - set.threshold + 1e-3;
                weight += w;
                sum += w * y;
            }
        }
        if (weight <= 0.0)
        {
            // A little below the threshold before letting go, so a flicker doesn't chatter.
            if (vc.note >= 0 && (onStep || energy < (float) set.threshold * 0.8f))
                release (v, out);
            vc.energy = energy;
            continue;
        }
        const double y = sum / weight;
        const double target = pitchFor (h > 1 ? 1.0 - y / (h - 1) : 0.5, set);
        vc.x = (float) ((x0 + x1) * 0.5 / p.width);
        vc.y = (float) ((y + 0.5) / h);

        if (onStep)
        {
            const bool onset = energy - vc.energy > 0.15f;   // a flash strikes the note again
            if (onset)
                release (v, out);
            play (v, target, energy, out);
        }
        else if (vc.note >= 0 && set.quantize < 0.999)
        {
            // Between the steps a held note glides after the light, as far as the bend reaches.
            vc.target = target;
            bend (v, std::clamp (target - vc.note, -set.bendRange, set.bendRange), out);
        }
        vc.energy = energy;
    }
    if (onStep)
        drumStep (rows (p, 0, p.width), out);
}

void Sonifier::spiralStep (const Picture& p, std::vector<Message>& out)
{
    // Vogel's sunflower: point k of `count` lies at radius sqrt(k / count), turned by the
    // golden angle each time; the columns setting is how many points before the centre again.
    const long count = std::max (8, set.columns * 8);
    const long k = steps % count;
    const double angle = k * 3.14159265358979323846 * (3.0 - std::sqrt (5.0));
    const double r = 0.5 * std::sqrt ((k + 0.5) / count);
    const double fx = 0.5 + r * std::cos (angle), fy = 0.5 + r * std::sin (angle);
    const int cx = std::clamp ((int) (fx * p.width), 0, p.width - 1), cy = std::clamp ((int) (fy * p.height), 0, p.height - 1);

    // The light around the point, 3 by 3.
    float sum = 0.0f;
    int taken = 0;
    for (int y = std::max (0, cy - 1); y <= std::min (p.height - 1, cy + 1); ++y)
        for (int x = std::max (0, cx - 1); x <= std::min (p.width - 1, cx + 1); ++x)
        {
            sum += p.at (x, y);
            ++taken;
        }
    const float level = sum / (float) std::max (1, taken);

    // The voices take turns, so each note rings while the next ones sound.
    const int v = (int) (steps % set.voices);
    if (level >= (float) set.threshold)
    {
        release (v, out);
        play (v, pitchFor (1.0 - fy, set), level, out);
        voice[v].x = (float) fx;
        voice[v].y = (float) fy;
    }
    else
        release (v, out);
    drumStep (rows (p, 0, p.width), out);
}

void Sonifier::drumStep (const std::vector<float>& profile, std::vector<Message>& out)
{
    for (const int d : drumsHeld)
        out.push_back ({ (uint8_t) (0x80 | drumChannel), (uint8_t) d, 0 });
    drumsHeld.clear();
    if (set.drums && lastDrumProfile.size() == profile.size() && ! profile.empty() && (! set.fibonacciDrums || fibonacciBeat (steps)))
    {
        // Thirds from the bottom: kick, snare, hi-hat.
        const int h = (int) profile.size();
        const float limit = 0.04f + 0.16f * (float) set.threshold;
        for (int band = 0; band < 3; ++band)
        {
            const int top = (2 - band) * h / 3, bottom = std::max (top + 1, (3 - band) * h / 3);
            float change = 0.0f;
            for (int y = top; y < bottom; ++y)
                change += std::abs (profile[(size_t) y] - lastDrumProfile[(size_t) y]);
            change /= (float) (bottom - top);
            if (change < limit)
                continue;
            const int note = band == 0 ? kick : band == 1 ? snare : hat;
            out.push_back ({ (uint8_t) (0x90 | drumChannel), (uint8_t) note, velocity (std::min (1.0f, change * 4.0f)) });
            drumsHeld.push_back (note);
        }
    }
    lastDrumProfile = profile;
}

void Sonifier::play (int v, double target, float level, std::vector<Message>& out)
{
    auto& vc = voice[v];
    const int note = std::clamp ((int) std::lround (target), 0, 127);
    vc.target = target;
    vc.level = level;
    if (vc.note == note)
    {
        bend (v, target - note, out);   // held: only the pitch between the notes moves
        return;
    }
    release (v, out);
    bend (v, target - note, out);
    out.push_back ({ (uint8_t) (0x90 | v), (uint8_t) note, velocity (level) });
    vc.note = note;
}

void Sonifier::release (int v, std::vector<Message>& out)
{
    auto& vc = voice[v];
    if (vc.note >= 0)
        out.push_back ({ (uint8_t) (0x80 | v), (uint8_t) vc.note, 0 });
    vc.note = -1;
}

void Sonifier::bend (int channel, double semitones, std::vector<Message>& out)
{
    const int value = std::clamp (8192 + (int) std::lround (semitones / set.bendRange * 8191.0), 0, 16383);
    if (lastBend[channel] == value)
        return;
    lastBend[channel] = value;
    out.push_back ({ (uint8_t) (0xE0 | channel), (uint8_t) (value & 0x7F), (uint8_t) (value >> 7) });
}

void Sonifier::stop (std::vector<Message>& out)
{
    for (int v = 0; v < maxVoices; ++v)
        release (v, out);
    for (const int d : drumsHeld)
        out.push_back ({ (uint8_t) (0x80 | drumChannel), (uint8_t) d, 0 });
    drumsHeld.clear();
    for (int c = 0; c < 16; ++c)
        if (lastBend[c] >= 0 && lastBend[c] != 8192)
            bend (c, 0.0, out);
    lastDrumProfile.clear();
    clock = 0.0;
    steps = 0;
    spots.clear();
}

void Sonifier::collectSpots()
{
    spots.clear();
    for (int v = 0; v < set.voices; ++v)
        if (voice[v].note >= 0)
            spots.push_back ({ voice[v].x, voice[v].y, voice[v].level });
}
}
