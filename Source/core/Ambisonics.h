// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Ambisonics in AmbiX form (ACN channel order, SN3D normalisation), orders 1 to 5:
// encoding sounds into the sphere, rotating and mirroring the scene, converting
// from and to FuMa, and decoding to speaker layouts (AllRAD with max-rE) and to
// headphones (through an HRTF set). Directions are in degrees: azimuth 0 in
// front, 90 to the right, -90 to the left; elevation 90 straight up. Plain C++.
namespace ambi
{
    constexpr int maxOrder = 5;

    int channelsFor (int order);        // (order + 1)^2
    int orderFor (int channels);        // the order, or -1 when the count is not (order + 1)^2 for orders 1 to 5
    std::string channelName (int acn);  // W, Y, Z, X, then V, T, R, S, U, then "ACN 9"...

    // The real spherical harmonics (ACN, SN3D) of a direction.
    std::vector<double> harmonics (int order, double azimuth, double elevation);
    // max-rE weights per order l, for decoders and beams.
    std::vector<double> maxReWeights (int order);

    // A K x K matrix (K = channelsFor (order)) that changes a whole scene, row-major.
    using Matrix = std::vector<std::vector<double>>;
    // Turns the scene: yaw to the right, pitch tilts the front up, roll lifts the left side; in that order.
    Matrix rotation (int order, double yaw, double pitch, double roll);
    Matrix mirror (int order, bool leftRight, bool frontBack, bool upDown);
    Matrix fumaToAmbix (int order);   // orders 1 to 3 (FuMa goes no further)
    Matrix ambixToFuma (int order);

    struct Speaker
    {
        std::string name;
        double azimuth = 0.0, elevation = 0.0;
        bool lfe = false;
    };
    // Layouts in the channel order PHI plays them in (dsp::speakerLayout): Stereo to 9.1.6,
    // with two subwoofers in 5.2, 7.2 and 7.2.4 (LFE2 straight after LFE1).
    std::vector<std::string> layoutNames();
    std::vector<Speaker> layout (const std::string& name);
    // An L x K decoder for a layout (AllRAD: a dense virtual layout panned onto the real one by
    // VBAP, with max-rE weighting). LFE rows are zero. Level: one plane wave gives energy 1.
    Matrix decoder (int order, const std::vector<Speaker>&);

    struct EncodeSettings
    {
        int order = 1;
        double azimuth = 0.0, elevation = 0.0;
        double width = 60.0;   // degrees between the channels of a stereo source
    };
    // A mono or stereo file placed in the sphere: the whole file, (order + 1)^2 channels.
    AudioDataPtr encode (const AudioData&, const EncodeSettings&);

    // The scene changed by `m` over [start, end), with 10 ms fades at the edges.
    AudioDataPtr transform (const AudioData&, Frame start, Frame end, const Matrix& m);
    // Any linear mix of the channels (rows: outputs), for the whole file.
    AudioDataPtr mix (const AudioData&, const Matrix& m, std::atomic<float>* progress = nullptr);

    // Headphones: the scene through an HRTF set (SOFA file), always stereo. Empty with `error` set on failure.
    AudioDataPtr decodeBinaural (const AudioData&, const std::string& hrtfPath, std::string& error,
                                 std::atomic<float>* progress = nullptr);

    // The filters decodeBinaural uses, for a live decoder: each ACN channel's impulse response
    // for each ear at `rate`. Built once per file, order and rate. Null with `error` set on failure.
    using EarFilters = std::array<std::vector<std::vector<float>>, 2>;   // [ear][channel]
    std::shared_ptr<const EarFilters> binauralFilters (const std::string& hrtfPath, int order, double rate, std::string& error);

    // Where the sound energy is: a cols x rows map (azimuth -180 to 180 left to right,
    // elevation 90 to -90 top to bottom), each value the mean power of a max-rE beam.
    void energyMap (const float* const* channels, int numChannels, int frames, int cols, int rows, std::vector<float>& out);
}
