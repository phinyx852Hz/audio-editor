// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Ambisonics.h"
#include "AudioData.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The Ambisonic Studio: a rack of spatial devices that turns a mono, stereo, multichannel
// or ambisonic file into a higher-order ambisonic scene, then decodes it (the master bus).
// Its devices follow the Envelop for Live (E4L) toolkit: panners that place the dry input
// into the sphere (Source, Mono, Stereo, Quad, Omni, Multi, Spinner), voices that spread
// copies of it around the room (Multi-Delay, Brownian Delay, Delay Boids, AuraVerb), and
// effects on the scene itself (HOA Transform, B-Format Convolution Reverb, Spatial Slicer,
// Gain). Written from E4L's documented behaviour; no E4L code is used. Plain C++.
//
// Each device sees the dry input (passed through untouched, as in E4L) and the scene bus
// (AmbiX: ACN order, SN3D). Source devices add to the bus; scene devices change it.
// Directions as in ambi: azimuth 0 ahead, 90 right; elevation 90 up. Rendering is
// deterministic (random motion is seeded), so a preview sounds like the final render.
namespace studio
{
    enum class Kind
    {
        // Sources: read the dry input, add to the scene
        sourcePanner, monoPanner, stereoPanner, quadPanner, omniPanner, multiPanner, spinner,
        multiDelay, brownianDelay, delayBoids, auraVerb, bformatInput,
        // On the scene
        hoaTransform, convolutionReverb, spatialSlicer, gain,
    };

    struct ParamSpec
    {
        std::string id, label;
        double min = 0.0, max = 1.0, def = 0.0;
        std::string unit;                  // "°", "dB", "ms", "Hz", "%", "s"...
        std::vector<std::string> choices;  // a choice list: the value is the index
        int decimals = 1;
        std::string group;                 // shown under this heading
        std::string help;
        bool toggle = false;               // 0 or 1, shown as a check box
    };

    struct DeviceSpec
    {
        Kind kind;
        std::string id, name, description;
        bool source = true;                // false: works on the scene
        std::vector<ParamSpec> params;
        std::vector<std::string> files;    // file slots (impulse responses)
        bool hud = false;                  // draggable position: azimuth, elevation, radius
    };

    const std::vector<DeviceSpec>& devices();
    const DeviceSpec& spec (Kind);
    const DeviceSpec* specById (const std::string& id);

    struct Device
    {
        Kind kind = Kind::sourcePanner;
        bool enabled = true;
        std::map<std::string, double> values;      // missing: the default
        std::map<std::string, std::string> files;

        double get (const std::string& id) const;
        void set (const std::string& id, double v);  // clamped to the spec
    };

    Device makeDevice (Kind);

    // Whether a control matters with the device's other settings (XYZ hides azimuth, an LFO
    // that is off hides its speed...). Choices and toggles can change it.
    bool relevant (const Device&, const std::string& id);

    // The master bus: what the scene becomes.
    struct Master
    {
        double gainDb = 0.0;
        std::string decoder = "AmbiX";   // "AmbiX", "FuMa", "Headphones", an ambi::layoutNames() name, or "Custom: <name>"
        std::string hrtfPath;
        std::vector<ambi::Speaker> speakers;   // the custom array, when the decoder is one
    };

    // Your own speaker array, up to 16 channels, as text: one speaker a line, "name azimuth
    // elevation", with "LFE" after it for a subwoofer; "#" starts a comment. Azimuth as
    // everywhere (0 ahead, 90 right). False with `error` set when it cannot be used.
    constexpr int maxSpeakers = 16;
    bool parseSpeakers (const std::string& text, std::vector<ambi::Speaker>& speakers, std::string& error);
    std::string formatSpeakers (const std::vector<ambi::Speaker>&);
    // Common studio arrays to start from: rings, domes, a cube.
    std::vector<std::pair<std::string, std::vector<ambi::Speaker>>> exampleArrays();
    bool isCustom (const std::string& decoder);

    struct Scene
    {
        int order = 3;                    // 1 to 3 (4, 9 or 16 channels, as E4L's 3OA bus)
        double bpm = 120.0;               // for synced LFOs, delays and triggers
        double tailSeconds = 2.0;         // added after the audio for delays and reverbs to ring out
        std::vector<Device> chain;
        Master master;
    };

    // A rack as text (JSON), for settings and preset files. fromJson keeps `scene` and returns false on bad text.
    std::string toJson (const Scene&);
    bool fromJson (const std::string&, Scene&);

    // A sound placed in the scene at one moment, for the heads-up display.
    struct Dot
    {
        float azimuth = 0, elevation = 0, radius = 1, level = 1;
        int voice = 0;
    };

    // The positions every device drew, every `interval` seconds: [snapshot][device] -> dots.
    struct Motion
    {
        double interval = 0.05;
        std::vector<std::vector<std::vector<Dot>>> frames;
        const std::vector<Dot>* at (double seconds, size_t device) const;
    };

    // The input's channels as the source devices see them: a mono file is heard on both
    // sides. An ambisonic input (inputOrder > 0) is AmbiX; the B-Format Input device reads it.
    struct Result
    {
        AudioDataPtr scene;   // AmbiX, channelsFor (order); the input's length plus the tail
        Motion motion;
        std::string error;
    };

    // `loadFile` reads the impulse responses the devices name (empty: the built-in room is used).
    using Loader = std::function<AudioDataPtr (const std::string& path)>;
    Result render (const AudioData& input, int inputOrder, const Scene&, const Loader& loadFile = {},
                   std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    // The master bus: the scene at its gain, decoded as chosen. Empty with `error` set on failure.
    AudioDataPtr decode (const AudioData& scene, const Master&, std::string& error, std::atomic<float>* progress = nullptr);

    // The layout name to give a decoded file ("Ambisonics (3rd order)", "7.1.4", or empty for stereo headphones).
    std::string decodedLayout (const Master&, int order);

    // E4L's distance model (from ICST ambiencode): within the centre the sound turns omni,
    // beyond it falls 1.5 dB a unit. radius 0 to 1, range the distance at radius 1.
    void distanceGains (double radius, double range, double& omni, double& directional);

    // A K x K matrix that keeps the scene where `gain(az, el)` is 1 and removes it where 0.
    ambi::Matrix directionalMask (int order, const std::function<double (double azimuth, double elevation)>& gain);
    ambi::Matrix beamMask (int order, double azimuth, double elevation, double widthDegrees);
}
