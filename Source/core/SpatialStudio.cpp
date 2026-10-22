// SPDX-License-Identifier: AGPL-3.0-only
#include "SpatialStudio.h"
#include "Binaural.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <sstream>

namespace studio
{
    namespace
    {
        constexpr double degrees = M_PI / 180.0;
        constexpr int segment = 2048;   // frames rendered at a time (and one motion snapshot)
        constexpr int control = 64;     // frames between position updates

        //======================================================================
        // The device table.
        ParamSpec num (std::string id, std::string label, double min, double max, double def, std::string unit,
                       int decimals, std::string group, std::string help = {})
        {
            ParamSpec p;
            p.id = std::move (id); p.label = std::move (label); p.min = min; p.max = max; p.def = def;
            p.unit = std::move (unit); p.decimals = decimals; p.group = std::move (group); p.help = std::move (help);
            return p;
        }

        ParamSpec choice (std::string id, std::string label, std::vector<std::string> choices, int def, std::string group, std::string help = {})
        {
            ParamSpec p;
            p.id = std::move (id); p.label = std::move (label); p.min = 0; p.max = (double) choices.size() - 1; p.def = def;
            p.choices = std::move (choices); p.decimals = 0; p.group = std::move (group); p.help = std::move (help);
            return p;
        }

        ParamSpec toggle (std::string id, std::string label, bool def, std::string group, std::string help = {})
        {
            ParamSpec p;
            p.id = std::move (id); p.label = std::move (label); p.min = 0; p.max = 1; p.def = def ? 1 : 0;
            p.decimals = 0; p.toggle = true; p.group = std::move (group); p.help = std::move (help);
            return p;
        }

        const std::vector<std::string> syncNames { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
        const double syncBeats[] { 0.25, 0.5, 1, 2, 4, 8, 16, 32 };

        // An LFO: off, cycling round (azimuth only) or a sine; its speed free (Hz x rate) or in time with the tempo.
        void addLfo (std::vector<ParamSpec>& ps, const std::string& prefix, const std::string& group, bool canCycle, double depth, double depthMax)
        {
            ps.push_back (choice (prefix + "Lfo", "LFO", canCycle ? std::vector<std::string> { "Off", "Cycle", "Sin" } : std::vector<std::string> { "Off", "Sin" }, 0, group,
                                  canCycle ? "Cycle: round and round the listener. Sin: swings either side by Depth" : "Swings either side by Depth"));
            ps.push_back (choice (prefix + "Time", "Time mode", { "Freq", "Sync" }, 0, group, "Freq: LFO Freq x Rate. Sync: a note length at the tempo"));
            ps.push_back (num (prefix + "Freq", "LFO freq", 0.01, 10.0, 0.25, "Hz", 2, group));
            ps.push_back (num (prefix + "Rate", "LFO rate", -1.0, 1.0, 1.0, "", 2, group, "Multiplies the frequency; negative turns the other way"));
            ps.push_back (choice (prefix + "Sync", "Sync", syncNames, 4, group));
            ps.push_back (num (prefix + "Depth", "Depth", 0.0, depthMax, depth, "°", 1, group));
        }

        void addPosition (std::vector<ParamSpec>& ps, const std::string& group, double radius = 0.1)
        {
            ps.push_back (num ("azimuth", "Azim", -180, 180, 0, "°", 1, group, "0 ahead, positive clockwise (to the right)"));
            ps.push_back (num ("elevation", "Elev", -90, 90, 0, "°", 1, group, "Positive overhead, negative below"));
            ps.push_back (num ("radius", "Radius", 0, 1, radius, "", 3, group, "Distance from the listener"));
            ps.push_back (num ("range", "Range", 1, 40, 10, "", 1, group, "How far radius 1 is: more range, quieter when far"));
        }

        std::vector<DeviceSpec> makeSpecs()
        {
            std::vector<DeviceSpec> all;
            {
                DeviceSpec d { Kind::sourcePanner, "sourcePanner", "Source Panner",
                               "Places a mono or stereo sound in the sphere, in polar (azimuth, elevation, radius) or XYZ coordinates, with distance.", true, {}, {}, true };
                d.params.push_back (choice ("stereo", "Input", { "Mono", "Stereo" }, 1, "Source", "Mono sums left and right 3 dB down at one place; Stereo places each side"));
                d.params.push_back (choice ("coords", "Coordinates", { "Polar", "XYZ" }, 0, "Source"));
                addPosition (d.params, "Position");
                d.params.push_back (num ("x", "X", -1, 1, 0, "", 3, "Position", "Left to right"));
                d.params.push_back (num ("y", "Y", -1, 1, 0.1, "", 3, "Position", "Behind to in front"));
                d.params.push_back (num ("z", "Z", -1, 1, 0, "", 3, "Position", "Below to overhead"));
                d.params.push_back (num ("spreadAzimuth", "Azim spread", 0, 180, 60, "°", 1, "Stereo spread"));
                d.params.push_back (num ("spreadElevation", "Elev spread", -90, 90, 0, "°", 1, "Stereo spread"));
                d.params.push_back (num ("spreadRadius", "Radius spread", -1, 1, 0, "", 3, "Stereo spread"));
                d.params.push_back (num ("spreadX", "X spread", 0, 2, 0.4, "", 3, "Stereo spread"));
                d.params.push_back (num ("spreadY", "Y spread", -2, 2, 0, "", 3, "Stereo spread"));
                d.params.push_back (num ("spreadZ", "Z spread", -2, 2, 0, "", 3, "Stereo spread"));
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::monoPanner, "monoPanner", "Mono Panner",
                               "Places a mono sound (stereo is folded down) and moves it with azimuth and elevation LFOs.", true, {}, {}, true };
                addPosition (d.params, "Position");
                addLfo (d.params, "az", "Azim LFO", true, 45, 180);
                addLfo (d.params, "el", "Elev LFO", false, 20, 90);
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::stereoPanner, "stereoPanner", "Stereo Panner",
                               "Places the left and right of a stereo sound around a moving centre, with spread and LFOs on position and spread.", true, {}, {}, true };
                addPosition (d.params, "Position");
                d.params.push_back (num ("spreadAzimuth", "Azim spread", 0, 360, 60, "°", 1, "Spread"));
                d.params.push_back (num ("spreadElevation", "Elev spread", -180, 180, 0, "°", 1, "Spread"));
                addLfo (d.params, "az", "Azim LFO", true, 45, 180);
                addLfo (d.params, "el", "Elev LFO", false, 20, 90);
                addLfo (d.params, "sp", "Spread LFO", false, 60, 180);
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::quadPanner, "quadPanner", "Quad Panner",
                               "Upmixes stereo: two copies of each side, spread LR (sides), X (opposite corners) or W (front to back).", true, {}, {}, true };
                addPosition (d.params, "Position");
                d.params.push_back (choice ("spreadMode", "Spread mode", { "LR", "X", "W" }, 0, "Spread",
                                            "LR: the left copies on one side, right on the other. X: copies on opposite corners. W: 0 all in front, 90 even, 180 all behind"));
                d.params.push_back (num ("spreadAzimuth", "Azim spread", 0, 180, 90, "°", 1, "Spread"));
                d.params.push_back (num ("spreadElevation", "Elev spread", -180, 180, 0, "°", 1, "Spread"));
                addLfo (d.params, "az", "Azim LFO", true, 45, 180);
                addLfo (d.params, "el", "Elev LFO", false, 20, 90);
                addLfo (d.params, "sp", "Spread LFO", false, 60, 180);
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::omniPanner, "omniPanner", "Omni Panner",
                               "Sends one channel to W only: everywhere at once, for bass and sounds that should not move.", true, {}, {}, false };
                d.params.push_back (choice ("source", "Source", { "L", "R", "Mix" }, 2, "Source", "Mix is 6 dB down"));
                d.params.push_back (toggle ("invert", "Invert", false, "Source"));
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::multiPanner, "multiPanner", "Multi-Panner",
                               "Places up to 16 channels of the file as a group: a global position (with LFOs) plus each source's offset, scaled.", true, {}, {}, true };
                addPosition (d.params, "Global");
                addLfo (d.params, "az", "Global azim LFO", true, 45, 180);
                addLfo (d.params, "el", "Global elev LFO", false, 20, 90);
                d.params.push_back (num ("scaleAzimuth", "Azim scale", -2, 2, 1, "", 2, "Scale"));
                d.params.push_back (num ("scaleElevation", "Elev scale", -2, 2, 1, "", 2, "Scale"));
                d.params.push_back (num ("scaleRadius", "Radius scale", -2, 2, 1, "", 2, "Scale"));
                for (int i = 1; i <= 16; ++i)
                {
                    const auto n = std::to_string (i), g = "Input " + n;
                    d.params.push_back (toggle ("on" + n, "Active", true, g));
                    d.params.push_back (num ("az" + n, "Azim", -180, 180, i % 2 == 1 ? -30 : 30, "°", 1, g));
                    d.params.push_back (num ("el" + n, "Elev", -90, 90, 0, "°", 1, g));
                    d.params.push_back (num ("rad" + n, "Radius", -1, 1, 0, "", 3, g));
                }
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::spinner, "spinner", "Spinner",
                               "Spins a mono sound round the listener and swings it up and down.", true, {}, {}, false };
                d.params.push_back (choice ("spinRange", "Range", { "0.1 Hz", "1 Hz", "4 Hz", "10 Hz" }, 1, "Horizontal spin", "The fastest spin; Rate is a share of it"));
                d.params.push_back (num ("spinRate", "Rate", -1, 1, 0.25, "", 3, "Horizontal spin", "Positive clockwise"));
                d.params.push_back (choice ("vertRange", "Range", { "0.1 Hz", "1 Hz", "4 Hz", "10 Hz" }, 0, "Vertical oscillation"));
                d.params.push_back (num ("vertRate", "Rate", -1, 1, 0.5, "", 3, "Vertical oscillation"));
                d.params.push_back (num ("minElevation", "Min elev", -90, 90, -20, "°", 1, "Vertical oscillation"));
                d.params.push_back (num ("maxElevation", "Max elev", -90, 90, 40, "°", 1, "Vertical oscillation"));
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::multiDelay, "multiDelay", "Multi-Delay",
                               "A cascading delay in 3D: each echo a step further round, in a spiral or ping-ponging both ways.", true, {}, {}, false };
                d.params.push_back (num ("input", "Input", -60, 12, 0, "dB", 1, "Delay", "Like a send: turning it down leaves the tails ringing"));
                d.params.push_back (choice ("timeMode", "Time mode", { "Time", "Sync" }, 0, "Delay"));
                d.params.push_back (num ("time", "Time", 10, 2000, 250, "ms", 0, "Delay"));
                d.params.push_back (num ("sixteenths", "Sixteenths", 1, 16, 3, "", 0, "Delay"));
                d.params.push_back (num ("feedback", "Feedback", 0, 95, 20, "%", 0, "Delay"));
                d.params.push_back (num ("decay", "Decay", -24, 0, -3, "dB", 1, "Delay", "Each echo this much quieter than the last"));
                d.params.push_back (choice ("azimuthMode", "Azimuth", { "Free", "Lock" }, 0, "Voices", "Lock: follow the panner before it in the rack"));
                d.params.push_back (num ("azimuth", "Azim", -180, 180, 0, "°", 1, "Voices"));
                d.params.push_back (num ("elevation", "Elev", -90, 90, 0, "°", 1, "Voices"));
                d.params.push_back (num ("spread", "Spread", -180, 180, 45, "°", 1, "Voices", "Degrees between one echo and the next"));
                d.params.push_back (num ("voices", "Voices", 1, 16, 6, "", 0, "Voices"));
                d.params.push_back (choice ("pattern", "Pattern", { "Spiral", "Pong" }, 0, "Voices"));
                d.params.push_back (num ("output", "Output", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::brownianDelay, "brownianDelay", "Brownian Delay",
                               "Eight copies of the sound wandering at random inside a box or a wedge, delayed and frequency-shifted.", true, {}, {}, false };
                d.params.push_back (choice ("shape", "Space", { "Box", "Pie" }, 0, "Space"));
                d.params.push_back (num ("sizeX", "X", 0, 1, 0.8, "", 2, "Space"));
                d.params.push_back (num ("sizeY", "Y", 0, 1, 0.8, "", 2, "Space"));
                d.params.push_back (num ("sizeZ", "Z", 0, 1, 0.3, "", 2, "Space"));
                d.params.push_back (num ("pieAzimuth", "Pie azim", -180, 180, 0, "°", 1, "Space"));
                d.params.push_back (num ("pieSize", "Pie size", 10, 360, 120, "°", 1, "Space"));
                d.params.push_back (num ("delay", "Delay time", 0, 2000, 400, "ms", 0, "Voices", "The longest delay; one voice is never delayed"));
                d.params.push_back (num ("shift", "Freq shift", 0, 500, 0, "Hz", 1, "Voices", "The largest frequency shift"));
                d.params.push_back (choice ("voiceMode", "Voices", { "Fixed", "Random" }, 0, "Voices", "Random: new delays and shifts every quarter note"));
                d.params.push_back (num ("step", "Step", 0, 1, 0.3, "", 2, "Motion", "How far the voices wander"));
                d.params.push_back (num ("glide", "Glide", 0, 1, 0.7, "", 2, "Motion", "How smoothly"));
                d.params.push_back (toggle ("distance", "Dist", true, "Motion", "Quieter further from the centre"));
                d.params.push_back (num ("seed", "Seed", 1, 999, 1, "", 0, "Motion", "Another seed, another wander"));
                d.params.push_back (num ("input", "Input", -60, 12, 0, "dB", 1, "Level"));
                d.params.push_back (num ("output", "Output", -60, 12, -3, "dB", 1, "Level"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::delayBoids, "delayBoids", "Delay Boids",
                               "A flock of delayed copies flying round the room: separation, alignment, cohesion, and a pull to a point.", true, {}, {}, false };
                d.params.push_back (num ("boids", "Boids", 2, 16, 8, "", 0, "Boids"));
                d.params.push_back (num ("speed", "Speed", 0, 2, 0.5, "", 2, "Boids"));
                d.params.push_back (num ("separation", "Separation", 0, 1, 0.5, "", 2, "Boids"));
                d.params.push_back (num ("alignment", "Alignment", 0, 1, 0.3, "", 2, "Boids"));
                d.params.push_back (num ("cohesion", "Cohesion", 0, 1, 0.3, "", 2, "Boids"));
                d.params.push_back (num ("attract", "Attract to point", 0, 1, 0.2, "", 2, "Boids"));
                d.params.push_back (num ("pointAzimuth", "Point azim", -180, 180, 0, "°", 1, "Boids"));
                d.params.push_back (num ("pointElevation", "Point elev", -90, 90, 0, "°", 1, "Boids"));
                d.params.push_back (num ("minDelay", "Min", 1, 2000, 80, "ms", 0, "Delay"));
                d.params.push_back (num ("maxDelay", "Max", 1, 4000, 900, "ms", 0, "Delay"));
                d.params.push_back (num ("feedback", "Feedback", 0, 90, 25, "%", 0, "Delay"));
                d.params.push_back (num ("cutoff", "Filter", 200, 20000, 6000, "Hz", 0, "Filter", "Low-pass on the delayed voices"));
                d.params.push_back (toggle ("trigger", "Trigger updates", true, "Trigger update", "New delay times on the beat"));
                d.params.push_back (choice ("measures", "Measures", { "1", "2", "4", "8" }, 0, "Trigger update"));
                d.params.push_back (choice ("subdiv", "Subdiv", { "1/1", "1/2", "1/4", "1/8", "1/16" }, 2, "Trigger update"));
                d.params.push_back (num ("probability", "Probability", 0, 100, 50, "%", 0, "Trigger update"));
                d.params.push_back (num ("seed", "Seed", 1, 999, 1, "", 0, "Trigger update"));
                d.params.push_back (num ("input", "Input", -60, 12, 0, "dB", 1, "Mix"));
                d.params.push_back (num ("output", "Wet", -60, 12, -6, "dB", 1, "Mix"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::auraVerb, "auraVerb", "AuraVerb",
                               "A first-order spatial reverb, 100% wet, with two-band decay, EQ, compressor and gate. Put a panner before it for the dry sound.", true, {}, {}, false };
                d.params.push_back (choice ("stereo", "In", { "Mono", "Stereo" }, 1, "In"));
                d.params.push_back (num ("predelay", "Pre-delay", 0, 250, 20, "ms", 0, "Reverb"));
                d.params.push_back (num ("t60Low", "T60 LF", 0.1, 20, 3.0, "s", 2, "Reverb", "Decay time below the crossover"));
                d.params.push_back (num ("t60Mid", "T60 MF", 0.1, 20, 2.2, "s", 2, "Reverb", "Decay time above it"));
                d.params.push_back (num ("xyzW", "XYZ/W", 0, 2, 1, "", 2, "Reverb", "Directional against omni: higher feels more spread out"));
                d.params.push_back (num ("crossover", "LFXO", 50, 1000, 200, "Hz", 0, "Reverb"));
                d.params.push_back (num ("hfDamp", "HFDamp", 1000, 20000, 6000, "Hz", 0, "Reverb"));
                d.params.push_back (num ("eq1Freq", "EQ 1 freq", 40, 18000, 300, "Hz", 0, "EQ"));
                d.params.push_back (num ("eq1Gain", "EQ 1 gain", -18, 18, 0, "dB", 1, "EQ"));
                d.params.push_back (num ("eq2Freq", "EQ 2 freq", 40, 18000, 4000, "Hz", 0, "EQ"));
                d.params.push_back (num ("eq2Gain", "EQ 2 gain", -18, 18, 0, "dB", 1, "EQ"));
                d.params.push_back (num ("compThreshold", "Threshold", -60, 0, 0, "dB", 1, "Compressor"));
                d.params.push_back (num ("compRatio", "Ratio", 1, 20, 2, ":1", 1, "Compressor"));
                d.params.push_back (num ("compAttack", "Attack", 0.1, 200, 10, "ms", 1, "Compressor"));
                d.params.push_back (num ("compRelease", "Release", 5, 2000, 150, "ms", 0, "Compressor"));
                d.params.push_back (num ("gateThreshold", "Threshold", -100, 0, -100, "dB", 1, "Gate"));
                d.params.push_back (num ("gateHold", "Hold", 0, 1000, 50, "ms", 0, "Gate"));
                d.params.push_back (num ("gateAttack", "Attack", 0.1, 100, 2, "ms", 1, "Gate"));
                d.params.push_back (num ("gateRelease", "Release", 5, 2000, 200, "ms", 0, "Gate"));
                d.params.push_back (num ("output", "Out", -60, 12, -6, "dB", 1, "Out"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::bformatInput, "bformatInput", "B-Format Input",
                               "Plays an ambisonic file into the scene (the B-Format Sampler): AmbiX or FuMa, any order up to the scene's.", true, {}, {}, false };
                d.params.push_back (choice ("format", "Format", { "AmbiX", "FuMa" }, 0, "Input", "FuMa: Furse-Malham order and weights (old B-format)"));
                d.params.push_back (num ("gain", "Volume", -60, 12, 0, "dB", 1, "Input"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::hoaTransform, "hoaTransform", "HOA Transform",
                               "Rotates the whole scene (yaw, pitch, roll) and weights its orders: all higher orders down leaves only W.", false, {}, {}, false };
                d.params.push_back (num ("yaw", "Yaw", -180, 180, 0, "°", 1, "Rotate", "About the vertical axis, to the right"));
                d.params.push_back (num ("pitch", "Pitch", -180, 180, 0, "°", 1, "Rotate", "Tilts the front up"));
                d.params.push_back (num ("roll", "Roll", -180, 180, 0, "°", 1, "Rotate", "Lifts the left side"));
                for (int l = 0; l <= 3; ++l)
                    d.params.push_back (num ("order" + std::to_string (l), "Order " + std::to_string (l), 0, 1, 1, "", 2, "Order"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::convolutionReverb, "convolutionReverb", "B-Format Convolution Reverb",
                               "Reverb from a B-format impulse response (one 4-channel file or W, X, Y, Z files; none: a built-in hall), sized and turned.", false, {}, { "W", "X", "Y", "Z" }, false };
                d.params.push_back (num ("size", "Size", 0.25, 2, 1, "x", 2, "Impulse response", "Shortens or lengthens the reverb"));
                d.params.push_back (num ("yaw", "Yaw", -180, 180, 0, "°", 1, "Rotate"));
                d.params.push_back (num ("pitch", "Pitch", -180, 180, 0, "°", 1, "Rotate"));
                d.params.push_back (num ("roll", "Roll", -180, 180, 0, "°", 1, "Rotate"));
                d.params.push_back (choice ("dist", "Dist", { "Lock", "Free" }, 0, "Mix",
                                            "Lock: the late reverb stays level however far the panner puts the sound. Free: it follows the scene's level"));
                d.params.push_back (num ("mix", "Dry/Wet", 0, 100, 30, "%", 0, "Mix"));
                d.params.push_back (num ("earlyMs", "Early ends at", 10, 300, 80, "ms", 0, "Mix"));
                d.params.push_back (num ("early", "Early", -60, 12, 0, "dB", 1, "Mix"));
                d.params.push_back (num ("late", "Late", -60, 12, 0, "dB", 1, "Mix"));
                for (const auto& [stage, name] : { std::pair { "dry", "Dry EQ" }, std::pair { "wet", "Wet EQ" }, std::pair { "earlyEq", "Early EQ" }, std::pair { "lateEq", "Late EQ" } })
                {
                    d.params.push_back (num (std::string (stage) + "Low", "Low cut", 20, 2000, 20, "Hz", 0, name));
                    d.params.push_back (num (std::string (stage) + "High", "High cut", 1000, 20000, 20000, "Hz", 0, name));
                }
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::spatialSlicer, "spatialSlicer", "Spatial Slicer",
                               "Cuts a slice of the sphere in and out in time: the scene narrows to one direction on triggers in time with the tempo.", false, {}, {}, false };
                d.params.push_back (num ("azimuth", "Azim", -180, 180, 0, "°", 1, "Spatial envelope"));
                d.params.push_back (num ("elevation", "Elev", -90, 90, 0, "°", 1, "Spatial envelope"));
                d.params.push_back (num ("spread", "Spread", 10, 180, 60, "°", 1, "Spatial envelope", "How wide the slice is"));
                d.params.push_back (num ("amount", "Amount", 0, 100, 100, "%", 0, "Spatial envelope"));
                d.params.push_back (toggle ("timed", "Time envelope", true, "Time envelope", "Off: the slice is always on"));
                d.params.push_back (choice ("measures", "Measures", { "1", "2", "4", "8" }, 0, "Time envelope"));
                d.params.push_back (choice ("subdiv", "Subdiv", { "1/4", "1/8", "1/16", "1/32" }, 1, "Time envelope"));
                d.params.push_back (num ("probability", "Probability", 0, 100, 50, "%", 0, "Time envelope"));
                d.params.push_back (num ("offset", "Offset", 0, 31, 0, "steps", 0, "Time envelope"));
                d.params.push_back (num ("duration", "Duration", 5, 100, 50, "%", 0, "Time envelope", "How much of each step the slice lasts"));
                d.params.push_back (num ("seed", "Seed", 1, 999, 1, "", 0, "Time envelope"));
                all.push_back (d);
            }
            {
                DeviceSpec d { Kind::gain, "gain", "Gain / Meter", "Turns the scene up or down (E4L Meter).", false, {}, {}, false };
                d.params.push_back (num ("gain", "Gain", -60, 12, 0, "dB", 1, "Level"));
                all.push_back (d);
            }
            return all;
        }

        //======================================================================
        // Small DSP.
        struct Rng
        {
            uint64_t s;
            explicit Rng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
            uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
            double uniform() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
            double bipolar() { return uniform() * 2.0 - 1.0; }
        };

        double hash01 (uint64_t a, uint64_t b)
        {
            Rng r (a * 1000003ull + b);
            r.next();
            return r.uniform();
        }

        // RBJ biquads.
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

            float process (float x)
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return (float) y;
            }

            void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
            {
                b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
            }

            void lowPass (double rate, double hz, double q = 0.7071)
            {
                hz = std::clamp (hz, 10.0, rate * 0.49);
                const double w = 2 * M_PI * hz / rate, c = std::cos (w), a = std::sin (w) / (2 * q);
                set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
            }

            void highPass (double rate, double hz, double q = 0.7071)
            {
                hz = std::clamp (hz, 5.0, rate * 0.49);
                const double w = 2 * M_PI * hz / rate, c = std::cos (w), a = std::sin (w) / (2 * q);
                set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
            }

            void peak (double rate, double hz, double db, double q = 1.0)
            {
                hz = std::clamp (hz, 10.0, rate * 0.49);
                const double A = std::pow (10.0, db / 40.0), w = 2 * M_PI * hz / rate, c = std::cos (w), a = std::sin (w) / (2 * q);
                set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
            }
        };

        // A band limit: a high-pass and a low-pass, either left out when it is at the edge of its range.
        struct BandLimit
        {
            Biquad hp, lp;
            bool useHp = false, useLp = false;

            void set (double rate, double low, double high)
            {
                useHp = low > 20.5;
                useLp = high < 19999.0 && high < rate * 0.45;
                if (useHp) hp.highPass (rate, low);
                if (useLp) lp.lowPass (rate, high);
            }

            float process (float x)
            {
                if (useHp) x = hp.process (x);
                if (useLp) x = lp.process (x);
                return x;
            }
        };

        // An analytic signal from two allpass chains (90 degrees apart over most of the band),
        // turned to shift every frequency by the same amount.
        struct FrequencyShifter
        {
            struct Allpass
            {
                double a, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                double process (double x)
                {
                    const double y = a * (x + y2) - x2;
                    x2 = x1; x1 = x; y2 = y1; y1 = y;
                    return y;
                }
            };
            std::array<Allpass, 4> re { Allpass { 0.6923878 }, Allpass { 0.9360654322959 }, Allpass { 0.9882295226860 }, Allpass { 0.9987488452737 } };
            std::array<Allpass, 4> im { Allpass { 0.4021921162426 }, Allpass { 0.8561710882420 }, Allpass { 0.9722909545651 }, Allpass { 0.9952884791278 } };
            double delayed = 0, phase = 0;

            float process (float x, double hz, double rate)
            {
                if (hz == 0.0)
                    return x;
                double r = x, i = x;
                for (auto& a : re) r = a.process (r);
                for (auto& a : im) i = a.process (i);
                const double out = delayed * std::cos (phase) - i * std::sin (phase);
                delayed = r;
                phase = std::fmod (phase + 2 * M_PI * hz / rate, 2 * M_PI);
                return (float) out;
            }
        };

        // A delay line read at fractional positions.
        struct DelayLine
        {
            std::vector<float> buffer;
            size_t write = 0;

            void resize (size_t n) { buffer.assign (std::max<size_t> (n, 4), 0.0f); write = 0; }
            void push (float x) { buffer[write] = x; write = (write + 1) % buffer.size(); }
            // `delay` samples ago (1 is the newest sample).
            float tap (double delay) const
            {
                delay = std::clamp (delay, 1.0, (double) buffer.size() - 2);
                const auto whole = (size_t) delay;
                const auto frac = (float) (delay - (double) whole);
                const auto n = buffer.size();
                const auto a = buffer[(write + n - whole) % n], b = buffer[(write + n - whole - 1) % n];
                return a + (b - a) * frac;
            }
        };

        //======================================================================
        struct Bus
        {
            int frames = segment;
            Frame start = 0;
            double rate = 48000, bpm = 120;
            int order = 3, inputOrder = 0;
            std::vector<std::vector<float>> dry;   // the input's channels
            std::vector<std::vector<float>> hoa;   // the scene
            std::vector<float> left, right;        // what the panners hear
            double panAzimuth = 0, panElevation = 0;   // where the last panner put its sound (for Lock)
            bool panned = false;

            double seconds (int offset) const { return (double) (start + offset) / rate; }
            double beats (int offset) const { return seconds (offset) * bpm / 60.0; }
        };

        int orderOfChannel (int acn) { return (int) std::floor (std::sqrt ((double) acn + 0.5)); }

        std::vector<double> gainsFor (int order, double azimuth, double elevation, double radius, double range, bool distance)
        {
            auto y = ambi::harmonics (order, azimuth, elevation);
            if (distance)
            {
                double omni = 1, directional = 1;
                distanceGains (radius, range, omni, directional);
                for (size_t k = 0; k < y.size(); ++k)
                    y[k] *= k == 0 ? omni : directional;
            }
            return y;
        }

        // Adds a mono signal into the scene, its gains sliding from the last block's to these.
        struct Encoder
        {
            std::vector<double> current;

            void add (Bus& bus, int offset, int n, const float* in, const std::vector<double>& target, double level = 1.0)
            {
                if (current.size() != target.size())
                {
                    current = target;
                    for (auto& g : current)
                        g *= level;
                }
                for (size_t k = 0; k < target.size() && k < bus.hoa.size(); ++k)
                {
                    const double from = current[k], to = target[k] * level, step = (to - from) / n;
                    auto* out = bus.hoa[k].data() + offset;
                    double g = from;
                    for (int i = 0; i < n; ++i)
                    {
                        g += step;
                        out[i] += (float) (in[i] * g);
                    }
                    current[k] = to;
                }
            }
        };

        // Where a sound is, as polar coordinates.
        struct Place
        {
            double azimuth = 0, elevation = 0, radius = 1;
        };

        double wrap (double degreesValue)
        {
            degreesValue = std::fmod (degreesValue + 180.0, 360.0);
            return (degreesValue < 0 ? degreesValue + 360.0 : degreesValue) - 180.0;
        }

        Place fromXyz (double x, double y, double z)
        {
            Place p;
            p.radius = std::sqrt (x * x + y * y + z * z);
            p.azimuth = std::atan2 (x, y) / degrees;
            p.elevation = std::atan2 (z, std::sqrt (x * x + y * y)) / degrees;
            return p;
        }

        //======================================================================
        class Processor
        {
        public:
            explicit Processor (const Device& d) : device (d) {}
            virtual ~Processor() = default;
            virtual void process (Bus&) = 0;
            std::vector<Dot> dots;   // where it put its sounds, at the end of the last segment

        protected:
            double p (const std::string& id) const { return device.get (id); }
            int choiceOf (const std::string& id) const { return (int) std::lround (device.get (id)); }
            double db (const std::string& id) const { return std::pow (10.0, device.get (id) / 20.0); }
            const Device device;
        };

        // An LFO's offset at a moment, from its parameters (prefix "az", "el" or "sp").
        double lfoValue (const Device& d, const std::string& prefix, double seconds, double bpm, bool canCycle)
        {
            const int mode = (int) std::lround (d.get (prefix + "Lfo"));
            if (mode == 0)
                return 0.0;
            const double rate = d.get (prefix + "Rate");
            double cycles = 0.0;
            if ((int) std::lround (d.get (prefix + "Time")) == 0)
                cycles = d.get (prefix + "Freq") * rate * seconds;
            else
            {
                const auto beats = syncBeats[std::clamp ((int) std::lround (d.get (prefix + "Sync")), 0, 7)];
                cycles = (rate > 0 ? 1.0 : rate < 0 ? -1.0 : 0.0) * seconds * bpm / 60.0 / beats;
            }
            if (canCycle && mode == 1)
                return 360.0 * cycles;
            return d.get (prefix + "Depth") * std::sin (2.0 * M_PI * cycles);
        }

        //======================================================================
        // Source, Mono, Stereo and Quad Panners, and the Spinner: sounds at moving places.
        class Panner final : public Processor
        {
        public:
            Panner (const Device& d, int sceneOrder) : Processor (d), order (sceneOrder)
            {
                if (d.kind == Kind::sourcePanner)
                    voices = choiceOf ("stereo") == 1 ? 2 : 1;
                else if (d.kind == Kind::stereoPanner)
                    voices = 2;
                else if (d.kind == Kind::quadPanner)
                    voices = 4;
                encoders.resize ((size_t) voices);
                mono.resize (control);
            }

            void process (Bus& bus) override
            {
                const double level = db ("gain");
                std::vector<Place> places;
                for (int offset = 0; offset < bus.frames; offset += control)
                {
                    const int n = std::min (control, bus.frames - offset);
                    places = placesAt (bus.seconds (offset + n), bus.bpm);
                    const bool distance = device.kind != Kind::spinner;
                    const double range = distance ? p ("range") : 1.0;
                    for (int v = 0; v < voices; ++v)
                    {
                        const float* in = nullptr;
                        if (voices == 1)
                        {
                            for (int i = 0; i < n; ++i)
                                mono[(size_t) i] = 0.70710678f * (bus.left[(size_t) (offset + i)] + bus.right[(size_t) (offset + i)]);
                            in = mono.data();
                        }
                        else
                            in = ((voices == 2 ? v : v / 2) == 0 ? bus.left : bus.right).data() + offset;
                        const auto& pl = places[(size_t) v];
                        encoders[(size_t) v].add (bus, offset, n, in, gainsFor (order, pl.azimuth, pl.elevation, pl.radius, range, distance),
                                                  level * (voices == 4 ? 0.70710678 : 1.0));
                    }
                }
                dots.clear();
                for (int v = 0; v < voices; ++v)
                    dots.push_back ({ (float) places[(size_t) v].azimuth, (float) places[(size_t) v].elevation,
                                      (float) std::min (1.0, places[(size_t) v].radius), 1.0f, voices == 4 ? v / 2 : v });
                const auto centre = placesAt (bus.seconds (bus.frames), bus.bpm, true);
                bus.panAzimuth = centre.front().azimuth;
                bus.panElevation = centre.front().elevation;
                bus.panned = true;
            }

        private:
            std::vector<Place> placesAt (double t, double bpm, bool centreOnly = false) const
            {
                if (device.kind == Kind::spinner)
                {
                    static const double ranges[] { 0.1, 1, 4, 10 };
                    const double spin = ranges[std::clamp (choiceOf ("spinRange"), 0, 3)] * p ("spinRate");
                    const double vert = ranges[std::clamp (choiceOf ("vertRange"), 0, 3)] * p ("vertRate");
                    const double lo = std::min (p ("minElevation"), p ("maxElevation")), hi = std::max (p ("minElevation"), p ("maxElevation"));
                    return { { wrap (360.0 * spin * t), lo + (hi - lo) * 0.5 * (1.0 - std::cos (2 * M_PI * vert * t)), 1.0 } };
                }
                if (device.kind == Kind::sourcePanner)
                {
                    Place c;
                    double sAz = p ("spreadAzimuth"), sEl = p ("spreadElevation"), sR = p ("spreadRadius");
                    if (choiceOf ("coords") == 1)
                    {
                        const double x = p ("x"), y = p ("y"), z = p ("z");
                        c = fromXyz (x, y, z);
                        if (voices == 1 || centreOnly)
                            return { c };
                        const double dx = p ("spreadX") / 2, dy = p ("spreadY") / 2, dz = p ("spreadZ") / 2;
                        return { fromXyz (x - dx, y - dy, z - dz), fromXyz (x + dx, y + dy, z + dz) };
                    }
                    c = { p ("azimuth"), p ("elevation"), p ("radius") };
                    if (voices == 1 || centreOnly)
                        return { c };
                    return { { wrap (c.azimuth - sAz / 2), std::clamp (c.elevation - sEl / 2, -90.0, 90.0), std::clamp (c.radius - sR / 2, 0.0, 1.0) },
                             { wrap (c.azimuth + sAz / 2), std::clamp (c.elevation + sEl / 2, -90.0, 90.0), std::clamp (c.radius + sR / 2, 0.0, 1.0) } };
                }

                // Mono, Stereo, Quad: a centre moved by the LFOs.
                const Place c { wrap (p ("azimuth") + lfoValue (device, "az", t, bpm, true)),
                                std::clamp (p ("elevation") + lfoValue (device, "el", t, bpm, false), -90.0, 90.0), p ("radius") };
                if (voices == 1 || centreOnly)
                    return { c };
                const double sAz = std::max (0.0, p ("spreadAzimuth") + lfoValue (device, "sp", t, bpm, false)), sEl = p ("spreadElevation");
                const auto at = [&] (double az, double el) { return Place { wrap (c.azimuth + az), std::clamp (c.elevation + el, -90.0, 90.0), c.radius }; };
                if (voices == 2)
                    return { at (-sAz / 2, -sEl / 2), at (sAz / 2, sEl / 2) };

                // Quad: two copies of left, then two of right.
                const double s = std::min (sAz, 180.0);
                switch (choiceOf ("spreadMode"))
                {
                    case 0:   // LR: front and back at 0, both at the side at 180
                    {
                        const double half = 90.0 - s / 2;
                        return { at (-90 + half, -sEl / 2), at (-90 - half, -sEl / 2), at (90 - half, sEl / 2), at (90 + half, sEl / 2) };
                    }
                    case 1:   // X: opposite corners
                    {
                        const double half = 90.0 - s / 2;
                        return { at (-90 + half, -sEl / 2), at (90 + half, -sEl / 2), at (90 - half, sEl / 2), at (-90 - half, sEl / 2) };
                    }
                    default:  // W: all ahead at 0, even at 90, all behind at 180
                    {
                        const double inner = s <= 90 ? s / 2 : 45 + (s - 90) * 1.5;
                        const double outer = s <= 90 ? s * 1.5 : 135 + (s - 90) / 2;
                        return { at (-outer, -sEl / 2), at (-inner, -sEl / 2), at (inner, sEl / 2), at (outer, sEl / 2) };
                    }
                }
            }

            int order, voices = 1;
            std::vector<Encoder> encoders;
            std::vector<float> mono;
        };

        //======================================================================
        class OmniPanner final : public Processor
        {
        public:
            using Processor::Processor;
            void process (Bus& bus) override
            {
                const int source = choiceOf ("source");
                const float g = (float) (db ("gain") * (p ("invert") > 0.5 ? -1.0 : 1.0));
                auto& w = bus.hoa[0];
                for (int i = 0; i < bus.frames; ++i)
                {
                    const float x = source == 0 ? bus.left[(size_t) i] : source == 1 ? bus.right[(size_t) i]
                                                                                    : 0.5f * (bus.left[(size_t) i] + bus.right[(size_t) i]);
                    w[(size_t) i] += g * x;
                }
                dots = { { 0, 90, 0, 1, 0 } };
            }
        };

        //======================================================================
        class MultiPanner final : public Processor
        {
        public:
            MultiPanner (const Device& d, int sceneOrder) : Processor (d), order (sceneOrder), encoders (16) {}

            void process (Bus& bus) override
            {
                const int sources = std::min<int> (16, (int) bus.dry.size());
                const double level = db ("gain");
                for (int offset = 0; offset < bus.frames; offset += control)
                {
                    const int n = std::min (control, bus.frames - offset);
                    const double t = bus.seconds (offset + n);
                    const double az = p ("azimuth") + lfoValue (device, "az", t, bus.bpm, true);
                    const double el = p ("elevation") + lfoValue (device, "el", t, bus.bpm, false);
                    dots.clear();
                    for (int s = 0; s < sources; ++s)
                    {
                        const auto id = std::to_string (s + 1);
                        if (p ("on" + id) < 0.5)
                            continue;
                        const Place pl { wrap (az + p ("scaleAzimuth") * p ("az" + id)),
                                         std::clamp (el + p ("scaleElevation") * p ("el" + id), -90.0, 90.0),
                                         std::clamp (p ("radius") + p ("scaleRadius") * p ("rad" + id), 0.0, 1.0) };
                        // A file that is ambisonic is not placed source by source: it is one sound, its W.
                        const auto* in = (bus.inputOrder > 0 ? bus.left : bus.dry[(size_t) s]).data() + offset;
                        encoders[(size_t) s].add (bus, offset, n, in, gainsFor (order, pl.azimuth, pl.elevation, pl.radius, p ("range"), true), level);
                        dots.push_back ({ (float) pl.azimuth, (float) pl.elevation, (float) pl.radius, 1.0f, s });
                    }
                    bus.panAzimuth = wrap (az);
                    bus.panElevation = el;
                    bus.panned = true;
                }
            }

        private:
            int order;
            std::vector<Encoder> encoders;
        };

        //======================================================================
        class MultiDelay final : public Processor
        {
        public:
            MultiDelay (const Device& d, int sceneOrder, double rate, double bpm) : Processor (d), order (sceneOrder)
            {
                delay = choiceOf ("timeMode") == 1 ? p ("sixteenths") * 15.0 / bpm * rate : p ("time") * 0.001 * rate;
                delay = std::max (1.0, delay);
                voices = (int) std::lround (p ("voices"));
                line.resize ((size_t) (delay * (voices + 2)) + 8);
                encoders.resize ((size_t) voices);
                taps.assign ((size_t) voices, std::vector<float> (control));
            }

            void process (Bus& bus) override
            {
                const float in = (float) db ("input"), fb = (float) (p ("feedback") / 100.0);
                const double decay = db ("decay"), out = db ("output");
                const bool lock = choiceOf ("azimuthMode") == 1 && bus.panned;
                for (int offset = 0; offset < bus.frames; offset += control)
                {
                    const int n = std::min (control, bus.frames - offset);
                    for (int i = 0; i < n; ++i)
                    {
                        const auto x = 0.5f * (bus.left[(size_t) (offset + i)] + bus.right[(size_t) (offset + i)]) * in;
                        line.push (x + fb * line.tap (delay));
                        for (int v = 0; v < voices; ++v)
                            taps[(size_t) v][(size_t) i] = line.tap (delay * (v + 1) + 1);
                    }
                    const double base = lock ? bus.panAzimuth : p ("azimuth");
                    const double el = lock ? bus.panElevation : p ("elevation");
                    dots.clear();
                    double g = out;
                    for (int v = 0; v < voices; ++v)
                    {
                        const double az = wrap (base + angleOf (v));
                        encoders[(size_t) v].add (bus, offset, n, taps[(size_t) v].data(), ambi::harmonics (order, az, el), g);
                        dots.push_back ({ (float) az, (float) el, 1.0f, (float) std::min (1.0, g), v });
                        g *= decay;
                    }
                }
            }

        private:
            double angleOf (int v) const
            {
                const double spread = p ("spread");
                if (choiceOf ("pattern") == 0)
                    return spread * (v + 1);
                return (v % 2 == 0 ? 1.0 : -1.0) * spread * (v / 2 + 1);
            }

            int order, voices = 1;
            double delay = 1;
            DelayLine line;
            std::vector<Encoder> encoders;
            std::vector<std::vector<float>> taps;
        };

        //======================================================================
        // Voices that wander: shared by the Brownian Delay.
        class BrownianDelay final : public Processor
        {
        public:
            static constexpr int voices = 8;

            BrownianDelay (const Device& d, int sceneOrder, double sampleRate) : Processor (d), order (sceneOrder), rate (sampleRate), rng ((uint64_t) p ("seed"))
            {
                line.resize ((size_t) (rate * 2.2) + 16);
                encoders.resize (voices);
                shifters.resize (voices);
                buffers.assign (voices, std::vector<float> (control));
                for (int v = 0; v < voices; ++v)
                {
                    pos[(size_t) v] = { rng.bipolar() * 0.5, rng.bipolar() * 0.5, rng.bipolar() * 0.2 };
                    shown[(size_t) v] = pos[(size_t) v];
                }
                reroll();
                for (int v = 0; v < voices; ++v)
                    currentDelay[(size_t) v] = targetDelay[(size_t) v];
            }

            void process (Bus& bus) override
            {
                const float in = (float) db ("input");
                const double out = db ("output");
                const bool random = choiceOf ("voiceMode") == 1;
                for (int offset = 0; offset < bus.frames; offset += control)
                {
                    const int n = std::min (control, bus.frames - offset);
                    const auto quarter = (int64_t) std::floor (bus.beats (offset));
                    if (random && quarter != lastQuarter)
                        reroll();
                    lastQuarter = quarter;

                    for (int i = 0; i < n; ++i)
                    {
                        line.push (0.5f * (bus.left[(size_t) (offset + i)] + bus.right[(size_t) (offset + i)]) * in);
                        for (int v = 0; v < voices; ++v)
                        {
                            auto& dl = currentDelay[(size_t) v];
                            dl += (targetDelay[(size_t) v] - dl) * 0.0005;
                            const auto x = line.tap (dl + 1);
                            buffers[(size_t) v][(size_t) i] = shifters[(size_t) v].process (x, shift[(size_t) v], rate);
                        }
                    }

                    elapsed += n;
                    if (elapsed >= rate * 0.05)
                    {
                        elapsed = 0;
                        wander();
                    }
                    dots.clear();
                    for (int v = 0; v < voices; ++v)
                    {
                        const auto& q = shown[(size_t) v];
                        const auto pl = fromXyz (q[0], q[1], q[2]);
                        encoders[(size_t) v].add (bus, offset, n, buffers[(size_t) v].data(),
                                                  gainsFor (order, pl.azimuth, pl.elevation, pl.radius, 5.0, p ("distance") > 0.5), out / std::sqrt (2.0));
                        dots.push_back ({ (float) pl.azimuth, (float) pl.elevation, (float) std::min (1.0, pl.radius), 1.0f, v });
                    }
                }
            }

        private:
            void reroll()
            {
                const double maxDelay = p ("delay") * 0.001 * rate, maxShift = p ("shift");
                for (int v = 0; v < voices; ++v)
                {
                    targetDelay[(size_t) v] = v == 0 ? 0.0 : rng.uniform() * maxDelay;
                    shift[(size_t) v] = v == 0 ? 0.0 : rng.uniform() * maxShift;
                }
            }

            void wander()
            {
                const double step = p ("step") * 0.12, glide = std::clamp (1.0 - p ("glide") * 0.97, 0.02, 1.0);
                for (int v = 0; v < voices; ++v)
                {
                    auto& q = pos[(size_t) v];
                    for (auto& c : q)
                        c += rng.bipolar() * step;
                    confine (q);
                    for (int c = 0; c < 3; ++c)
                        shown[(size_t) v][(size_t) c] += (q[(size_t) c] - shown[(size_t) v][(size_t) c]) * glide;
                }
            }

            void confine (std::array<double, 3>& q) const
            {
                if (choiceOf ("shape") == 0)
                {
                    const double size[] { p ("sizeX"), p ("sizeY"), p ("sizeZ") };
                    for (int c = 0; c < 3; ++c)
                        q[(size_t) c] = std::clamp (q[(size_t) c], -size[c], size[c]);
                    return;
                }
                // Pie: a wedge round the pie azimuth, out to radius 1, Z within its size.
                auto pl = fromXyz (q[0], q[1], 0.0);
                const double half = p ("pieSize") / 2, centre = p ("pieAzimuth");
                const double off = std::clamp (wrap (pl.azimuth - centre), -half, half);
                const double r = std::clamp (std::sqrt (q[0] * q[0] + q[1] * q[1]), 0.05, 1.0);
                const double az = (centre + off) * degrees;
                q = { r * std::sin (az), r * std::cos (az), std::clamp (q[2], -p ("sizeZ"), p ("sizeZ")) };
            }

            int order;
            double rate;
            Rng rng;
            DelayLine line;
            std::vector<Encoder> encoders;
            std::vector<FrequencyShifter> shifters;
            std::vector<std::vector<float>> buffers;
            std::array<std::array<double, 3>, voices> pos {}, shown {};
            std::array<double, voices> targetDelay {}, currentDelay {}, shift {};
            int64_t lastQuarter = -1;
            double elapsed = 0;
        };

        //======================================================================
        class DelayBoids final : public Processor
        {
        public:
            DelayBoids (const Device& d, int sceneOrder, double sampleRate) : Processor (d), order (sceneOrder), rate (sampleRate), rng ((uint64_t) p ("seed"))
            {
                count = (int) std::lround (p ("boids"));
                line.resize ((size_t) (rate * 4.2) + 16);
                encoders.resize ((size_t) count);
                filters.resize ((size_t) count);
                buffers.assign ((size_t) count, std::vector<float> (control));
                boids.resize ((size_t) count);
                for (auto& b : boids)
                {
                    b.p = { rng.bipolar() * 0.6, rng.bipolar() * 0.6, rng.bipolar() * 0.3 };
                    b.v = { rng.bipolar() * 0.1, rng.bipolar() * 0.1, rng.bipolar() * 0.05 };
                }
                for (auto& f : filters)
                    f.lowPass (rate, p ("cutoff"));
                delays.resize ((size_t) count);
                current.resize ((size_t) count);
                for (int i = 0; i < count; ++i)
                    current[(size_t) i] = delays[(size_t) i] = pickDelay();
            }

            void process (Bus& bus) override
            {
                const float in = (float) db ("input"), fb = (float) (p ("feedback") / 100.0);
                const double out = db ("output") / std::sqrt ((double) count);
                static const double subdivBeats[] { 4, 2, 1, 0.5, 0.25 };
                const double stepBeats = subdivBeats[std::clamp (choiceOf ("subdiv"), 0, 4)];
                for (int offset = 0; offset < bus.frames; offset += control)
                {
                    const int n = std::min (control, bus.frames - offset);
                    if (p ("trigger") > 0.5)
                    {
                        const auto step = (int64_t) std::floor (bus.beats (offset) / stepBeats);
                        if (step != lastStep)
                        {
                            lastStep = step;
                            for (auto& dl : delays)
                                if (rng.uniform() * 100.0 < p ("probability"))
                                    dl = pickDelay();
                        }
                    }
                    for (int i = 0; i < n; ++i)
                    {
                        float sum = 0;
                        for (int b = 0; b < count; ++b)
                        {
                            auto& dl = current[(size_t) b];
                            dl += (delays[(size_t) b] - dl) * 0.0003;
                            const auto x = filters[(size_t) b].process (line.tap (dl + 1));
                            buffers[(size_t) b][(size_t) i] = x;
                            sum += x;
                        }
                        line.push (0.5f * (bus.left[(size_t) (offset + i)] + bus.right[(size_t) (offset + i)]) * in + fb * sum / (float) count);
                    }
                    fly ((double) n / rate);
                    dots.clear();
                    for (int b = 0; b < count; ++b)
                    {
                        const auto& q = boids[(size_t) b].p;
                        const auto pl = fromXyz (q[0], q[1], q[2]);
                        encoders[(size_t) b].add (bus, offset, n, buffers[(size_t) b].data(), gainsFor (order, pl.azimuth, pl.elevation, pl.radius, 4.0, true), out);
                        dots.push_back ({ (float) pl.azimuth, (float) pl.elevation, (float) std::min (1.0, pl.radius), 1.0f, b });
                    }
                }
            }

        private:
            struct Boid { std::array<double, 3> p, v; };

            double pickDelay()
            {
                const double lo = std::min (p ("minDelay"), p ("maxDelay")), hi = std::max (p ("minDelay"), p ("maxDelay"));
                return (lo + rng.uniform() * (hi - lo)) * 0.001 * rate;
            }

            // Reynolds' flocking, inside the unit ball.
            void fly (double dt)
            {
                const double speed = p ("speed");
                const double pa = p ("pointAzimuth") * degrees, pe = p ("pointElevation") * degrees;
                const std::array<double, 3> point { 0.6 * std::cos (pe) * std::sin (pa), 0.6 * std::cos (pe) * std::cos (pa), 0.6 * std::sin (pe) };
                std::array<double, 3> centre {}, heading {};
                for (const auto& b : boids)
                    for (int c = 0; c < 3; ++c)
                    {
                        centre[(size_t) c] += b.p[(size_t) c] / count;
                        heading[(size_t) c] += b.v[(size_t) c] / count;
                    }
                for (auto& b : boids)
                {
                    std::array<double, 3> a {};
                    for (const auto& o : boids)
                    {
                        if (&o == &b)
                            continue;
                        std::array<double, 3> d { b.p[0] - o.p[0], b.p[1] - o.p[1], b.p[2] - o.p[2] };
                        const double dist2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] + 1e-4;
                        if (dist2 < 0.09)
                            for (int c = 0; c < 3; ++c)
                                a[(size_t) c] += p ("separation") * d[(size_t) c] / dist2 * 0.02;
                    }
                    for (int c = 0; c < 3; ++c)
                    {
                        a[(size_t) c] += p ("cohesion") * (centre[(size_t) c] - b.p[(size_t) c]) * 0.5;
                        a[(size_t) c] += p ("alignment") * (heading[(size_t) c] - b.v[(size_t) c]) * 0.5;
                        a[(size_t) c] += p ("attract") * (point[(size_t) c] - b.p[(size_t) c]) * 0.8;
                        a[(size_t) c] += rng.bipolar() * 0.05;
                        b.v[(size_t) c] += a[(size_t) c] * dt * 4.0;
                    }
                    const double vs = std::sqrt (b.v[0] * b.v[0] + b.v[1] * b.v[1] + b.v[2] * b.v[2]);
                    const double maxV = 0.05 + speed * 0.6;
                    if (vs > maxV)
                        for (auto& c : b.v)
                            c *= maxV / vs;
                    for (int c = 0; c < 3; ++c)
                        b.p[(size_t) c] += b.v[(size_t) c] * dt * (0.2 + speed);
                    const double r = std::sqrt (b.p[0] * b.p[0] + b.p[1] * b.p[1] + b.p[2] * b.p[2]);
                    if (r > 1.0)
                        for (int c = 0; c < 3; ++c)
                        {
                            b.p[(size_t) c] /= r;
                            b.v[(size_t) c] = -b.v[(size_t) c] * 0.5;
                        }
                }
            }

            int order, count = 8;
            double rate;
            Rng rng;
            DelayLine line;
            std::vector<Encoder> encoders;
            std::vector<Biquad> filters;
            std::vector<std::vector<float>> buffers;
            std::vector<Boid> boids;
            std::vector<double> delays, current;
            int64_t lastStep = std::numeric_limits<int64_t>::min();
        };

        //======================================================================
        // AuraVerb: an eight-line feedback delay network, each line heard from a corner of a
        // cube around the listener (first order), with two-band decay, EQ, compressor and gate.
        class AuraVerb final : public Processor
        {
        public:
            static constexpr int lines = 8;

            AuraVerb (const Device& d, double sampleRate) : Processor (d), rate (sampleRate)
            {
                static const int lengths[lines] { 1103, 1307, 1499, 1693, 1873, 2069, 2269, 2459 };
                const double t60Low = p ("t60Low"), t60Mid = p ("t60Mid");
                for (int i = 0; i < lines; ++i)
                {
                    const double len = lengths[i] * rate / 48000.0;
                    delayLength[(size_t) i] = len;
                    delay[(size_t) i].resize ((size_t) len + 4);
                    gainLow[(size_t) i] = std::pow (10.0, -3.0 * len / (t60Low * rate));
                    gainMid[(size_t) i] = std::pow (10.0, -3.0 * len / (t60Mid * rate));
                    damp[(size_t) i].lowPass (rate, p ("hfDamp"));
                }
                splitCoeff = 1.0 - std::exp (-2.0 * M_PI * p ("crossover") / rate);
                pre.resize ((size_t) (0.26 * rate) + 8);
                preDelay = std::max (1.0, p ("predelay") * 0.001 * rate);
                for (int c = 0; c < 4; ++c)
                {
                    eq1[(size_t) c].peak (rate, p ("eq1Freq"), p ("eq1Gain"));
                    eq2[(size_t) c].peak (rate, p ("eq2Freq"), p ("eq2Gain"));
                }
                compAttack = std::exp (-1.0 / (std::max (0.1, p ("compAttack")) * 0.001 * rate));
                compRelease = std::exp (-1.0 / (std::max (1.0, p ("compRelease")) * 0.001 * rate));
                gateAttack = 1.0 / std::max (1.0, p ("gateAttack") * 0.001 * rate);
                gateRelease = 1.0 / std::max (1.0, p ("gateRelease") * 0.001 * rate);
                gateHold = p ("gateHold") * 0.001 * rate;
            }

            void process (Bus& bus) override
            {
                const bool stereo = choiceOf ("stereo") == 1;
                const double xyz = p ("xyzW"), out = db ("output");
                const double threshold = p ("compThreshold"), ratio = std::max (1.0, p ("compRatio"));
                const double gateThreshold = p ("gateThreshold");
                const double corner = 1.0 / std::sqrt (3.0);
                for (int i = 0; i < bus.frames; ++i)
                {
                    const float l = bus.left[(size_t) i], r = bus.right[(size_t) i];
                    pre.push (stereo ? l : 0.5f * (l + r));
                    const float inL = pre.tap (preDelay), inR = stereo ? r : inL;

                    std::array<double, lines> o {};
                    for (int k = 0; k < lines; ++k)
                    {
                        const double x = delay[(size_t) k].tap (delayLength[(size_t) k]);
                        lowState[(size_t) k] += (x - lowState[(size_t) k]) * splitCoeff;
                        const double low = lowState[(size_t) k], mid = x - low;
                        o[(size_t) k] = damp[(size_t) k].process ((float) (low * gainLow[(size_t) k] + mid * gainMid[(size_t) k]));
                    }
                    // Hadamard mixing (fast, 8 x 8), normalised.
                    std::array<double, lines> m = o;
                    for (int h = 1; h < lines; h *= 2)
                        for (int a = 0; a < lines; a += h * 2)
                            for (int b = a; b < a + h; ++b)
                            {
                                const double x = m[(size_t) b], y = m[(size_t) (b + h)];
                                m[(size_t) b] = x + y;
                                m[(size_t) (b + h)] = x - y;
                            }
                    for (int k = 0; k < lines; ++k)
                        delay[(size_t) k].push ((float) (m[(size_t) k] / std::sqrt ((double) lines) + ((k % 2 == 0) ? inL : inR) * ((k / 2) % 2 == 0 ? 0.5 : -0.5)));

                    // First order from the cube's corners: (+-1, +-1, +-1).
                    double w = 0, x = 0, y = 0, z = 0;
                    for (int k = 0; k < lines; ++k)
                    {
                        const double fx = (k & 1) ? 1 : -1, fy = (k & 2) ? 1 : -1, fz = (k & 4) ? 1 : -1;
                        w += o[(size_t) k];
                        x += o[(size_t) k] * fx * corner;   // front
                        y += o[(size_t) k] * fy * corner;   // left
                        z += o[(size_t) k] * fz * corner;   // up
                    }
                    std::array<double, 4> acn { w, y * xyz, z * xyz, x * xyz };
                    for (int c = 0; c < 4; ++c)
                        acn[(size_t) c] = eq2[(size_t) c].process (eq1[(size_t) c].process ((float) (acn[(size_t) c] * 0.35)));

                    // Compressor and gate, linked on W.
                    const double level = std::abs (acn[0]);
                    envelope = level > envelope ? compAttack * envelope + (1 - compAttack) * level : compRelease * envelope + (1 - compRelease) * level;
                    const double envDb = 20.0 * std::log10 (envelope + 1e-12);
                    const double reduction = envDb > threshold ? (envDb - threshold) * (1.0 - 1.0 / ratio) : 0.0;
                    const double comp = std::pow (10.0, -reduction / 20.0);
                    if (envDb >= gateThreshold)
                        held = gateHold;
                    else
                        held = std::max (0.0, held - 1);
                    const double gateTarget = held > 0 || gateThreshold <= -99.9 ? 1.0 : 0.0;
                    gate += gateTarget > gate ? std::min (gateAttack, gateTarget - gate) : -std::min (gateRelease, gate - gateTarget);
                    const double g = comp * gate * out;
                    for (int c = 0; c < 4 && c < (int) bus.hoa.size(); ++c)
                        bus.hoa[(size_t) c][(size_t) i] += (float) (acn[(size_t) c] * g);
                    lastReduction = reduction;
                }
                dots.clear();
                for (int k = 0; k < lines; ++k)
                {
                    const double fx = (k & 1) ? 1 : -1, fy = (k & 2) ? 1 : -1, fz = (k & 4) ? 1 : -1;
                    const auto pl = fromXyz (-fy, fx, fz);
                    dots.push_back ({ (float) pl.azimuth, (float) pl.elevation, 1.0f, (float) std::clamp (envelope * 4.0, 0.1, 1.0), k });
                }
            }

        private:
            double rate;
            std::array<DelayLine, lines> delay;
            std::array<double, lines> delayLength {}, gainLow {}, gainMid {}, lowState {};
            std::array<Biquad, lines> damp;
            std::array<Biquad, 4> eq1, eq2;
            double splitCoeff = 0.1, preDelay = 1;
            DelayLine pre;
            double compAttack = 0, compRelease = 0, envelope = 0, lastReduction = 0;
            double gateAttack = 1, gateRelease = 1, gateHold = 0, held = 0, gate = 1;
        };

        //======================================================================
        class BFormatInput final : public Processor
        {
        public:
            BFormatInput (const Device& d, int order, int inputOrder) : Processor (d)
            {
                const int k = ambi::channelsFor (order);
                if (inputOrder <= 0)
                    return;
                const int in = ambi::channelsFor (inputOrder);
                ambi::Matrix convert;
                if (choiceOf ("format") == 1 && inputOrder <= 3)
                    convert = ambi::fumaToAmbix (inputOrder);
                matrix.assign ((size_t) k, std::vector<double> ((size_t) in, 0.0));
                for (int r = 0; r < std::min (k, in); ++r)
                    for (int c = 0; c < in; ++c)
                        matrix[(size_t) r][(size_t) c] = convert.empty() ? (r == c ? 1.0 : 0.0) : convert[(size_t) r][(size_t) c];
            }

            void process (Bus& bus) override
            {
                if (matrix.empty())
                    return;
                const auto g = (float) db ("gain");
                for (size_t r = 0; r < matrix.size(); ++r)
                    for (size_t c = 0; c < matrix[r].size() && c < bus.dry.size(); ++c)
                    {
                        const auto m = (float) matrix[r][c] * g;
                        if (m == 0.0f)
                            continue;
                        for (int i = 0; i < bus.frames; ++i)
                            bus.hoa[r][(size_t) i] += m * bus.dry[c][(size_t) i];
                    }
            }

        private:
            ambi::Matrix matrix;
        };

        //======================================================================
        // Mixes the scene's channels by a matrix (HOA Transform), in place.
        void applyMatrix (Bus& bus, const ambi::Matrix& m, std::vector<std::vector<float>>& scratch)
        {
            const auto k = bus.hoa.size();
            scratch.assign (k, std::vector<float> ((size_t) bus.frames, 0.0f));
            for (size_t r = 0; r < k; ++r)
                for (size_t c = 0; c < k; ++c)
                {
                    const auto g = (float) m[r][c];
                    if (g == 0.0f)
                        continue;
                    for (int i = 0; i < bus.frames; ++i)
                        scratch[r][(size_t) i] += g * bus.hoa[c][(size_t) i];
                }
            bus.hoa.swap (scratch);
        }

        class HoaTransform final : public Processor
        {
        public:
            HoaTransform (const Device& d, int order) : Processor (d)
            {
                matrix = ambi::rotation (order, p ("yaw"), p ("pitch"), p ("roll"));
                for (size_t r = 0; r < matrix.size(); ++r)
                {
                    const double w = p ("order" + std::to_string (std::min (3, orderOfChannel ((int) r))));
                    for (auto& v : matrix[r])
                        v *= w;
                }
            }

            void process (Bus& bus) override { applyMatrix (bus, matrix, scratch); }

        private:
            ambi::Matrix matrix;
            std::vector<std::vector<float>> scratch;
        };

        class GainStage final : public Processor
        {
        public:
            using Processor::Processor;
            void process (Bus& bus) override
            {
                const auto g = (float) db ("gain");
                for (auto& c : bus.hoa)
                    for (auto& x : c)
                        x *= g;
            }
        };

        //======================================================================
        class SpatialSlicer final : public Processor
        {
        public:
            SpatialSlicer (const Device& d, int order, double rate) : Processor (d)
            {
                mask = beamMask (order, p ("azimuth"), p ("elevation"), p ("spread"));
                ramp = 1.0 / (0.005 * rate);
            }

            void process (Bus& bus) override
            {
                const auto k = bus.hoa.size();
                std::vector<float> env ((size_t) bus.frames);
                static const double subdivBeats[] { 1, 0.5, 0.25, 0.125 };
                static const int measureCounts[] { 1, 2, 4, 8 };
                const double stepBeats = subdivBeats[std::clamp (choiceOf ("subdiv"), 0, 3)];
                const auto steps = (int64_t) std::lround (measureCounts[std::clamp (choiceOf ("measures"), 0, 3)] * 4 / stepBeats);
                const bool timed = p ("timed") > 0.5;
                const double amount = p ("amount") / 100.0;
                for (int i = 0; i < bus.frames; ++i)
                {
                    double target = 1.0;
                    if (timed)
                    {
                        const double beat = bus.beats (i) / stepBeats;
                        const auto step = (int64_t) std::floor (beat);
                        const auto inPattern = ((step - (int64_t) p ("offset")) % steps + steps) % steps;
                        const bool on = hash01 ((uint64_t) p ("seed"), (uint64_t) inPattern) * 100.0 < p ("probability");
                        target = on && beat - (double) step < p ("duration") / 100.0 ? 1.0 : 0.0;
                    }
                    level += std::clamp (target - level, -ramp, ramp);
                    env[(size_t) i] = (float) (level * amount);
                }
                // out = in + env * (mask * in - in)
                std::vector<std::vector<float>> masked (k, std::vector<float> ((size_t) bus.frames, 0.0f));
                for (size_t r = 0; r < k; ++r)
                    for (size_t c = 0; c < k; ++c)
                    {
                        const auto g = (float) mask[r][c];
                        if (std::abs (g) < 1e-7f)
                            continue;
                        for (int i = 0; i < bus.frames; ++i)
                            masked[r][(size_t) i] += g * bus.hoa[c][(size_t) i];
                    }
                for (size_t r = 0; r < k; ++r)
                    for (int i = 0; i < bus.frames; ++i)
                        bus.hoa[r][(size_t) i] += env[(size_t) i] * (masked[r][(size_t) i] - bus.hoa[r][(size_t) i]);
                dots = { { (float) p ("azimuth"), (float) p ("elevation"), 1.0f, (float) std::max (0.15, level), 0 } };
            }

        private:
            double ramp = 0.01, level = 0;
            ambi::Matrix mask;
        };

        //======================================================================
        // A first-order impulse response for when none is given: early reflections from
        // all round, then a dense, diffuse tail. Seeded, so it is always the same room.
        std::array<std::vector<float>, 4> builtInHall (double rate)
        {
            const auto n = (size_t) (rate * 2.6);
            std::array<std::vector<float>, 4> ir;
            for (auto& c : ir)
                c.assign (n, 0.0f);
            Rng rng (77);
            const double t60 = 2.2;
            for (int r = 0; r < 24; ++r)
            {
                const auto at = (size_t) ((0.008 + 0.07 * rng.uniform()) * rate);
                const double az = rng.uniform() * 360 - 180, el = rng.uniform() * 60 - 20;
                const auto y = ambi::harmonics (1, az, el);
                const double g = 0.6 * std::pow (10.0, -3.0 * (double) at / rate / t60) * (rng.uniform() < 0.5 ? -1 : 1);
                for (int c = 0; c < 4; ++c)
                    ir[(size_t) c][at] += (float) (g * y[(size_t) c]);
            }
            Biquad soften[4];
            for (auto& b : soften)
                b.lowPass (rate, 7000);
            for (size_t i = (size_t) (0.02 * rate); i < n; ++i)
            {
                const double t = (double) i / rate;
                const double env = std::pow (10.0, -3.0 * t / t60) * std::min (1.0, (t - 0.02) / 0.06) * 0.25;
                for (int c = 0; c < 4; ++c)
                    ir[(size_t) c][i] += soften[c].process ((float) (rng.bipolar() * env * (c == 0 ? 1.0 : 0.577)));
            }
            return ir;
        }

        class ConvolutionReverb final : public Processor
        {
        public:
            ConvolutionReverb (const Device& d, double rate, int fileOrder, const Loader& load, std::string& error)
                : Processor (d), inputOrder (fileOrder)
            {
                auto ir = loadImpulse (rate, load, error);
                // Size: stretch or squeeze the response in time.
                const double size = p ("size");
                if (std::abs (size - 1.0) > 1e-3)
                    for (auto& c : ir)
                    {
                        std::vector<float> s ((size_t) ((double) c.size() * size));
                        for (size_t i = 0; i < s.size(); ++i)
                        {
                            const double at = (double) i / size;
                            const auto a = (size_t) at;
                            const auto f = (float) (at - (double) a);
                            const auto x0 = a < c.size() ? c[a] : 0.0f, x1 = a + 1 < c.size() ? c[a + 1] : 0.0f;
                            s[i] = (x0 + (x1 - x0) * f) / (float) std::sqrt (size);
                        }
                        c = std::move (s);
                    }
                // Turn it.
                const auto turn = ambi::rotation (1, p ("yaw"), p ("pitch"), p ("roll"));
                std::array<std::vector<float>, 4> turned;
                for (int r = 0; r < 4; ++r)
                {
                    turned[(size_t) r].assign (ir[0].size(), 0.0f);
                    for (int c = 0; c < 4; ++c)
                        for (size_t i = 0; i < ir[(size_t) c].size() && i < turned[(size_t) r].size(); ++i)
                            turned[(size_t) r][i] += (float) turn[(size_t) r][(size_t) c] * ir[(size_t) c][i];
                }
                // Split into early and late, with a short crossfade.
                const auto split = (size_t) (p ("earlyMs") * 0.001 * rate), fade = (size_t) (0.004 * rate);
                for (int c = 0; c < 4; ++c)
                {
                    std::vector<float> early (turned[(size_t) c]), late (turned[(size_t) c]);
                    for (size_t i = 0; i < early.size(); ++i)
                    {
                        const double t = i < split ? 1.0 : i >= split + fade ? 0.0 : 1.0 - (double) (i - split) / (double) fade;
                        early[i] *= (float) t;
                        late[i] *= (float) (1.0 - t);
                    }
                    early.resize (std::min (early.size(), split + fade + 1));
                    earlyConv[(size_t) c] = std::make_unique<binaural::Convolver> (early, segment);
                    lateConv[(size_t) c] = std::make_unique<binaural::Convolver> (late, segment);
                }
                const auto stage = [&] (const std::string& name, BandLimit& b) { b.set (rate, p (name + "Low"), p (name + "High")); };
                for (auto& b : dryEq)
                    stage ("dry", b);
                stage ("wet", wetEq);
                for (auto& b : earlyEq) stage ("earlyEq", b);
                for (auto& b : lateEq) stage ("lateEq", b);
                const double mix = p ("mix") / 100.0;
                dryGain = std::cos (mix * M_PI / 2);
                wetGain = std::sin (mix * M_PI / 2);
            }

            void process (Bus& bus) override
            {
                std::vector<float> in ((size_t) segment, 0.0f), early ((size_t) segment), late ((size_t) segment);
                const bool lock = choiceOf ("dist") == 0;
                for (int i = 0; i < bus.frames; ++i)
                {
                    // Lock: the sound before any panner turned it down for distance. Free: the scene's W.
                    const float x = lock ? (inputOrder > 0 ? bus.dry[0][(size_t) i] : 0.5f * (bus.left[(size_t) i] + bus.right[(size_t) i]))
                                         : bus.hoa[0][(size_t) i];
                    in[(size_t) i] = wetEq.process (x);
                }
                for (size_t r = 0; r < bus.hoa.size(); ++r)
                    for (int i = 0; i < bus.frames; ++i)
                        bus.hoa[r][(size_t) i] = (float) (dryGain * dryEq[std::min<size_t> (r, 15)].process (bus.hoa[r][(size_t) i]));
                const double eg = db ("early") * wetGain, lg = db ("late") * wetGain;
                for (int c = 0; c < 4 && c < (int) bus.hoa.size(); ++c)
                {
                    earlyConv[(size_t) c]->process (in.data(), early.data());
                    lateConv[(size_t) c]->process (in.data(), late.data());
                    for (int i = 0; i < bus.frames; ++i)
                        bus.hoa[(size_t) c][(size_t) i] += (float) (eg * earlyEq[(size_t) c].process (early[(size_t) i])
                                                                    + lg * lateEq[(size_t) c].process (late[(size_t) i]));
                }
            }

        private:
            std::array<std::vector<float>, 4> loadImpulse (double rate, const Loader& load, std::string& error)
            {
                std::array<std::vector<float>, 4> ir;
                const auto read = [&] (const AudioDataPtr& a, int channel, std::vector<float>& out)
                {
                    out.assign ((size_t) std::min<Frame> (a->length(), (Frame) (rate * 20)), 0.0f);
                    std::vector<std::vector<float>> all ((size_t) a->numChannels(), std::vector<float> (out.size()));
                    std::vector<float*> ptrs;
                    for (auto& c : all)
                        ptrs.push_back (c.data());
                    a->read (0, (int) out.size(), ptrs.data());
                    out = all[(size_t) std::min (channel, a->numChannels() - 1)];
                    if (std::abs (a->sampleRate() - rate) > 1.0)
                    {
                        // Linear resampling is enough for a reverb tail.
                        const double ratio = a->sampleRate() / rate;
                        std::vector<float> r ((size_t) ((double) out.size() / ratio));
                        for (size_t i = 0; i < r.size(); ++i)
                        {
                            const double at = (double) i * ratio;
                            const auto j = (size_t) at;
                            const auto f = (float) (at - (double) j);
                            r[i] = out[j] + ((j + 1 < out.size() ? out[j + 1] : 0.0f) - out[j]) * f;
                        }
                        out = std::move (r);
                    }
                };
                static const char* names[] { "W", "X", "Y", "Z" };   // FuMa letters: W, X front, Y left, Z up
                static const int acnOf[] { 0, 3, 1, 2 };
                bool any = false;
                if (load)
                {
                    const auto first = device.files.count ("W") ? device.files.at ("W") : std::string();
                    const auto one = first.empty() ? AudioDataPtr() : load (first);
                    if (one != nullptr && one->numChannels() >= 4)
                    {
                        // One B-format file: taken as AmbiX (W Y Z X).
                        for (int c = 0; c < 4; ++c)
                            read (one, c, ir[(size_t) c]);
                        any = true;
                    }
                    else
                        for (int f = 0; f < 4; ++f)
                        {
                            const auto it = device.files.find (names[f]);
                            if (it == device.files.end() || it->second.empty())
                                continue;
                            const auto a = load (it->second);
                            if (a == nullptr)
                            {
                                error = "Could not read the impulse response " + it->second;
                                continue;
                            }
                            // FuMa's W is 3 dB down.
                            read (a, 0, ir[(size_t) acnOf[f]]);
                            if (f == 0)
                                for (auto& x : ir[0])
                                    x *= 1.41421356f;
                            any = true;
                        }
                }
                if (! any)
                    return builtInHall (rate);
                size_t longest = 0;
                for (auto& c : ir)
                    longest = std::max (longest, c.size());
                for (auto& c : ir)
                    c.resize (std::max<size_t> (longest, 1), 0.0f);
                return ir;
            }

            int inputOrder;
            std::array<std::unique_ptr<binaural::Convolver>, 4> earlyConv, lateConv;
            std::array<BandLimit, 16> dryEq;
            BandLimit wetEq;
            std::array<BandLimit, 4> earlyEq, lateEq;
            double dryGain = 1, wetGain = 0;
        };

        //======================================================================
        std::unique_ptr<Processor> makeProcessor (const Device& d, int order, int inputOrder, double rate, double bpm,
                                                  const Loader& load, std::string& error)
        {
            switch (d.kind)
            {
                case Kind::sourcePanner:
                case Kind::monoPanner:
                case Kind::stereoPanner:
                case Kind::quadPanner:
                case Kind::spinner:           return std::make_unique<Panner> (d, order);
                case Kind::omniPanner:        return std::make_unique<OmniPanner> (d);
                case Kind::multiPanner:       return std::make_unique<MultiPanner> (d, order);
                case Kind::multiDelay:        return std::make_unique<MultiDelay> (d, order, rate, bpm);
                case Kind::brownianDelay:     return std::make_unique<BrownianDelay> (d, order, rate);
                case Kind::delayBoids:        return std::make_unique<DelayBoids> (d, order, rate);
                case Kind::auraVerb:          return std::make_unique<AuraVerb> (d, rate);
                case Kind::bformatInput:      return std::make_unique<BFormatInput> (d, order, inputOrder);
                case Kind::hoaTransform:      return std::make_unique<HoaTransform> (d, order);
                case Kind::convolutionReverb: return std::make_unique<ConvolutionReverb> (d, rate, inputOrder, load, error);
                case Kind::spatialSlicer:     return std::make_unique<SpatialSlicer> (d, order, rate);
                case Kind::gain:              return std::make_unique<GainStage> (d);
            }
            return {};
        }

        //======================================================================
        // A little JSON: enough for a rack.
        std::string quote (const std::string& s)
        {
            std::string out = "\"";
            for (const auto c : s)
            {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if (c == '\n') out += "\\n";
                else if ((unsigned char) c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); out += b; }
                else out += c;
            }
            return out + "\"";
        }

        std::string number (double v)
        {
            char b[32];
            std::snprintf (b, sizeof b, "%.10g", v);
            return b;
        }

        struct Json
        {
            enum Type { null, boolean, num, str, arr, obj } type = null;
            double n = 0;
            bool b = false;
            std::string s;
            std::vector<Json> items;
            std::vector<std::pair<std::string, Json>> members;

            const Json* operator[] (const std::string& key) const
            {
                for (const auto& [k, v] : members)
                    if (k == key)
                        return &v;
                return nullptr;
            }
        };

        struct Parser
        {
            const std::string& t;
            size_t i = 0;
            bool ok = true;

            void space() { while (i < t.size() && std::isspace ((unsigned char) t[i])) ++i; }
            bool eat (char c) { space(); if (i < t.size() && t[i] == c) { ++i; return true; } return false; }

            std::string string()
            {
                std::string out;
                if (! eat ('"')) { ok = false; return out; }
                while (i < t.size() && t[i] != '"')
                {
                    if (t[i] == '\\' && i + 1 < t.size())
                    {
                        const char e = t[++i];
                        if (e == 'n') out += '\n';
                        else if (e == 't') out += '\t';
                        else if (e == 'u' && i + 4 < t.size()) { out += (char) std::stoi (t.substr (i + 1, 4), nullptr, 16); i += 4; }
                        else out += e;
                        ++i;
                    }
                    else
                        out += t[i++];
                }
                if (i >= t.size()) ok = false;
                ++i;
                return out;
            }

            Json value()
            {
                Json j;
                space();
                if (i >= t.size()) { ok = false; return j; }
                const char c = t[i];
                if (c == '{')
                {
                    ++i;
                    j.type = Json::obj;
                    if (eat ('}')) return j;
                    do
                    {
                        auto key = string();
                        if (! eat (':')) { ok = false; return j; }
                        j.members.emplace_back (std::move (key), value());
                    } while (ok && eat (','));
                    if (! eat ('}')) ok = false;
                }
                else if (c == '[')
                {
                    ++i;
                    j.type = Json::arr;
                    if (eat (']')) return j;
                    do j.items.push_back (value()); while (ok && eat (','));
                    if (! eat (']')) ok = false;
                }
                else if (c == '"') { j.type = Json::str; j.s = string(); }
                else if (t.compare (i, 4, "true") == 0) { j.type = Json::boolean; j.b = true; i += 4; }
                else if (t.compare (i, 5, "false") == 0) { j.type = Json::boolean; i += 5; }
                else if (t.compare (i, 4, "null") == 0) { i += 4; }
                else
                {
                    size_t used = 0;
                    try { j.n = std::stod (t.substr (i, 40), &used); } catch (...) { ok = false; }
                    j.type = Json::num;
                    i += used;
                    if (used == 0) ok = false;
                }
                return j;
            }
        };
    }

    //==========================================================================
    const std::vector<DeviceSpec>& devices()
    {
        static const auto all = makeSpecs();
        return all;
    }

    const DeviceSpec& spec (Kind k)
    {
        for (const auto& d : devices())
            if (d.kind == k)
                return d;
        return devices().front();
    }

    const DeviceSpec* specById (const std::string& id)
    {
        for (const auto& d : devices())
            if (d.id == id)
                return &d;
        return nullptr;
    }

    double Device::get (const std::string& id) const
    {
        if (const auto it = values.find (id); it != values.end())
            return it->second;
        for (const auto& p : spec (kind).params)
            if (p.id == id)
                return p.def;
        return 0.0;
    }

    void Device::set (const std::string& id, double v)
    {
        for (const auto& p : spec (kind).params)
            if (p.id == id)
            {
                values[id] = std::clamp (v, p.min, p.max);
                return;
            }
    }

    Device makeDevice (Kind k)
    {
        Device d;
        d.kind = k;
        return d;
    }

    bool relevant (const Device& d, const std::string& id)
    {
        const auto is = [&] (const std::string& other, int value) { return (int) std::lround (d.get (other)) == value; };
        const auto starts = [&] (const char* prefix) { return id.rfind (prefix, 0) == 0; };
        // LFOs: az, el and sp.
        for (const auto* prefix : { "az", "el", "sp" })
        {
            const std::string p = prefix;
            if (id == p + "Time" || id == p + "Freq" || id == p + "Rate" || id == p + "Sync" || id == p + "Depth")
            {
                if (spec (d.kind).params.end() == std::find_if (spec (d.kind).params.begin(), spec (d.kind).params.end(),
                                                                [&] (const ParamSpec& s) { return s.id == p + "Lfo"; }))
                    break;
                if (is (p + "Lfo", 0)) return false;
                if (id == p + "Sync") return is (p + "Time", 1);
                if (id == p + "Freq") return is (p + "Time", 0);
                if (id == p + "Depth") return ! (p == "az" && is ("azLfo", 1));
                return true;
            }
        }
        switch (d.kind)
        {
            case Kind::sourcePanner:
            {
                const bool xyz = is ("coords", 1), stereo = is ("stereo", 1);
                if (id == "x" || id == "y" || id == "z") return xyz;
                if (id == "azimuth" || id == "elevation" || id == "radius") return ! xyz;
                if (id == "spreadX" || id == "spreadY" || id == "spreadZ") return xyz && stereo;
                if (starts ("spread")) return ! xyz && stereo;
                return true;
            }
            case Kind::multiDelay:
                if (id == "sixteenths") return is ("timeMode", 1);
                if (id == "time") return is ("timeMode", 0);
                if (id == "azimuth" || id == "elevation") return is ("azimuthMode", 0);
                return true;
            case Kind::brownianDelay:
                if (id == "pieAzimuth" || id == "pieSize") return is ("shape", 1);
                if (id == "sizeX" || id == "sizeY") return is ("shape", 0);
                return true;
            case Kind::delayBoids:
                if (id == "measures" || id == "subdiv" || id == "probability") return is ("trigger", 1);
                return true;
            case Kind::spatialSlicer:
                if (id == "measures" || id == "subdiv" || id == "probability" || id == "offset" || id == "duration" || id == "seed") return is ("timed", 1);
                return true;
            case Kind::multiPanner:
                for (int i = 1; i <= 16; ++i)
                {
                    const auto n = std::to_string (i);
                    if (id == "az" + n || id == "el" + n || id == "rad" + n)
                        return is ("on" + n, 1);
                }
                return true;
            case Kind::monoPanner: case Kind::stereoPanner: case Kind::quadPanner: case Kind::omniPanner: case Kind::spinner:
            case Kind::auraVerb: case Kind::bformatInput: case Kind::hoaTransform: case Kind::convolutionReverb: case Kind::gain:
                return true;
        }
        return true;
    }

    //==========================================================================
    std::string toJson (const Scene& scene)
    {
        std::ostringstream o;
        o << "{\"order\":" << scene.order << ",\"bpm\":" << number (scene.bpm) << ",\"tail\":" << number (scene.tailSeconds)
          << ",\"master\":{\"gainDb\":" << number (scene.master.gainDb) << ",\"decoder\":" << quote (scene.master.decoder)
          << ",\"hrtf\":" << quote (scene.master.hrtfPath) << "},\"chain\":[";
        for (size_t i = 0; i < scene.chain.size(); ++i)
        {
            const auto& d = scene.chain[i];
            o << (i ? "," : "") << "{\"device\":" << quote (spec (d.kind).id) << ",\"enabled\":" << (d.enabled ? "true" : "false") << ",\"values\":{";
            bool first = true;
            for (const auto& [k, v] : d.values)
            {
                o << (first ? "" : ",") << quote (k) << ":" << number (v);
                first = false;
            }
            o << "},\"files\":{";
            first = true;
            for (const auto& [k, v] : d.files)
            {
                o << (first ? "" : ",") << quote (k) << ":" << quote (v);
                first = false;
            }
            o << "}}";
        }
        o << "]}";
        return o.str();
    }

    bool fromJson (const std::string& text, Scene& scene)
    {
        Parser parser { text };
        const auto root = parser.value();
        if (! parser.ok || root.type != Json::obj)
            return false;
        Scene s;
        if (const auto* v = root["order"]) s.order = std::clamp ((int) v->n, 1, 3);
        if (const auto* v = root["bpm"]) s.bpm = std::clamp (v->n, 20.0, 400.0);
        if (const auto* v = root["tail"]) s.tailSeconds = std::clamp (v->n, 0.0, 60.0);
        if (const auto* m = root["master"])
        {
            if (const auto* v = (*m)["gainDb"]) s.master.gainDb = v->n;
            if (const auto* v = (*m)["decoder"]) s.master.decoder = v->s;
            if (const auto* v = (*m)["hrtf"]) s.master.hrtfPath = v->s;
        }
        if (const auto* chain = root["chain"])
            for (const auto& item : chain->items)
            {
                const auto* id = item["device"];
                const auto* ds = id != nullptr ? specById (id->s) : nullptr;
                if (ds == nullptr)
                    continue;
                auto d = makeDevice (ds->kind);
                if (const auto* e = item["enabled"]) d.enabled = e->b;
                if (const auto* vs = item["values"])
                    for (const auto& [k, v] : vs->members)
                        d.set (k, v.n);
                if (const auto* fs = item["files"])
                    for (const auto& [k, v] : fs->members)
                        d.files[k] = v.s;
                s.chain.push_back (std::move (d));
            }
        scene = std::move (s);
        return true;
    }

    //==========================================================================
    const std::vector<Dot>* Motion::at (double seconds, size_t device) const
    {
        if (frames.empty() || interval <= 0.0)
            return nullptr;
        const auto i = (size_t) std::clamp<double> (std::floor (seconds / interval), 0.0, (double) frames.size() - 1);
        return device < frames[i].size() ? &frames[i][device] : nullptr;
    }

    void distanceGains (double radius, double range, double& omni, double& directional)
    {
        const double dist = std::max (0.0, radius) * std::max (1.0, range);
        if (dist < 1.0)
        {
            omni = 1.0;
            directional = dist * dist;
        }
        else
            omni = directional = std::pow (10.0, (dist - 1.0) * -1.5 / 20.0);
    }

    ambi::Matrix directionalMask (int order, const std::function<double (double, double)>& gain)
    {
        const auto k = (size_t) ambi::channelsFor (order);
        // Nearly even points over the sphere.
        const int count = 900;
        const double golden = M_PI * (3.0 - std::sqrt (5.0));
        ambi::Matrix m (k, std::vector<double> (k, 0.0));
        for (int i = 0; i < count; ++i)
        {
            const double z = 1.0 - (i + 0.5) * 2.0 / count, r = std::sqrt (std::max (0.0, 1.0 - z * z));
            const double x = r * std::cos (golden * i), y = r * std::sin (golden * i);   // x front, y left
            const double az = -std::atan2 (y, x) / degrees, el = std::asin (z) / degrees;
            const double g = gain (az, el);
            if (g == 0.0)
                continue;
            const auto h = ambi::harmonics (order, az, el);
            for (size_t a = 0; a < k; ++a)
                for (size_t b = 0; b < k; ++b)
                    m[a][b] += g * h[a] * h[b] * (2 * orderOfChannel ((int) b) + 1) / count;
        }
        return m;
    }

    ambi::Matrix beamMask (int order, double azimuth, double elevation, double widthDegrees)
    {
        const auto toVec = [] (double az, double el)
        {
            return std::array<double, 3> { std::cos (el * degrees) * std::cos (-az * degrees), std::cos (el * degrees) * std::sin (-az * degrees), std::sin (el * degrees) };
        };
        const auto centre = toVec (azimuth, elevation);
        const double half = std::clamp (widthDegrees, 1.0, 360.0) / 2.0;
        return directionalMask (order, [&] (double az, double el)
        {
            const auto v = toVec (az, el);
            const double angle = std::acos (std::clamp (v[0] * centre[0] + v[1] * centre[1] + v[2] * centre[2], -1.0, 1.0)) / degrees;
            // Full inside, a soft edge a quarter of the width wide.
            const double edge = std::max (5.0, half * 0.5);
            if (angle <= half - edge / 2) return 1.0;
            if (angle >= half + edge / 2) return 0.0;
            return 0.5 + 0.5 * std::cos (M_PI * (angle - (half - edge / 2)) / edge);
        });
    }

    //==========================================================================
    Result render (const AudioData& input, int inputOrder, const Scene& scene, const Loader& load,
                   std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        Result result;
        const int order = std::clamp (scene.order, 1, 3);
        const int k = ambi::channelsFor (order);
        const double rate = input.sampleRate();
        const Frame total = input.length() + (Frame) std::llround (std::max (0.0, scene.tailSeconds) * rate);
        result.motion.interval = segment / rate;

        std::vector<std::unique_ptr<Processor>> chain;
        for (const auto& d : scene.chain)
            chain.push_back (d.enabled ? makeProcessor (d, order, inputOrder, rate, scene.bpm, load, result.error) : nullptr);

        Bus bus;
        bus.rate = rate;
        bus.bpm = std::max (1.0, scene.bpm);
        bus.order = order;
        bus.inputOrder = inputOrder;
        bus.dry.assign ((size_t) input.numChannels(), std::vector<float> ((size_t) segment));
        bus.hoa.assign ((size_t) k, std::vector<float> ((size_t) segment));
        std::vector<float*> ptrs;
        for (auto& c : bus.dry)
            ptrs.push_back (c.data());

        AudioData::Builder out (k, rate);
        std::vector<const float*> outPtrs ((size_t) k);
        for (Frame pos = 0; pos < total; pos += segment)
        {
            if (cancel != nullptr && cancel->load())
            {
                result.error = "Cancelled";
                return result;
            }
            bus.start = pos;
            bus.frames = segment;
            input.read (pos, segment, ptrs.data());
            // The panners hear the left and right (a mono file on both sides; an ambisonic file's W).
            bus.left = bus.dry[0];
            bus.right = inputOrder > 0 || bus.dry.size() < 2 ? bus.dry[0] : bus.dry[1];
            for (auto& c : bus.hoa)
                std::fill (c.begin(), c.end(), 0.0f);
            bus.panned = false;

            std::vector<std::vector<Dot>> snapshot (chain.size());
            for (size_t i = 0; i < chain.size(); ++i)
                if (chain[i] != nullptr)
                {
                    chain[i]->process (bus);
                    snapshot[i] = chain[i]->dots;
                }
            result.motion.frames.push_back (std::move (snapshot));

            const auto n = (int) std::min<Frame> (segment, total - pos);
            for (int c = 0; c < k; ++c)
                outPtrs[(size_t) c] = bus.hoa[(size_t) c].data();
            out.append (outPtrs.data(), n);
            if (progress != nullptr)
                progress->store ((float) ((double) (pos + n) / (double) total));
        }
        result.scene = out.finish();
        return result;
    }

    AudioDataPtr decode (const AudioData& scene, const Master& master, std::string& error, std::atomic<float>* progress)
    {
        const int order = ambi::orderFor (scene.numChannels());
        if (order < 1)
        {
            error = "The scene is not ambisonic";
            return {};
        }
        const auto k = (size_t) scene.numChannels();
        const double g = std::pow (10.0, master.gainDb / 20.0);
        const auto scaled = [g] (ambi::Matrix m) { for (auto& r : m) for (auto& v : r) v *= g; return m; };
        ambi::Matrix identity (k, std::vector<double> (k, 0.0));
        for (size_t i = 0; i < k; ++i)
            identity[i][i] = 1.0;

        if (master.decoder == "AmbiX")
            return ambi::mix (scene, scaled (identity), progress);
        if (master.decoder == "FuMa")
            return ambi::mix (scene, scaled (ambi::ambixToFuma (order)), progress);
        if (master.decoder == "Headphones")
        {
            if (std::abs (g - 1.0) < 1e-9)
                return ambi::decodeBinaural (scene, master.hrtfPath, error, progress);
            const auto louder = ambi::mix (scene, scaled (identity));
            return ambi::decodeBinaural (*louder, master.hrtfPath, error, progress);
        }
        const auto speakers = ambi::layout (master.decoder);
        if (speakers.empty())
        {
            error = "Unknown decoder " + master.decoder;
            return {};
        }
        return ambi::mix (scene, scaled (ambi::decoder (order, speakers)), progress);
    }

    std::string decodedLayout (const Master& master, int order)
    {
        static const char* names[] { "", "Ambisonics (1st order)", "Ambisonics (2nd order)", "Ambisonics (3rd order)" };
        if (master.decoder == "AmbiX" || master.decoder == "FuMa")
            return names[std::clamp (order, 1, 3)];
        if (master.decoder == "Headphones")
            return {};
        return master.decoder;
    }
}
