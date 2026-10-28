// SPDX-License-Identifier: AGPL-3.0-only
#include "Spatial.h"

#include <algorithm>
#include <cmath>

namespace mixer::spatial
{
    Gains gains (const Place& p)
    {
        // E4L's distance model (as studio::distanceGains): within 1 the sound turns omni, beyond it falls.
        const auto d = std::clamp ((double) p.distance, 0.0, (double) maxDistance);
        const auto omni = d < 1.0 ? 1.0 : std::pow (10.0, (d - 1.0) * -1.5 / 20.0);
        const auto directional = d < 1.0 ? d * d : omni;
        const auto elevation = std::clamp ((double) p.elevation, -90.0, 90.0);
        const auto half = std::clamp ((double) p.spread, 0.0, 360.0) / 2.0;
        Gains g {};
        for (int side = 0; side < 2; ++side)
        {
            const auto y = ambi::harmonics (order, p.azimuth + (side == 0 ? -half : half), elevation);
            for (int c = 0; c < channels; ++c)
                g[(size_t) side][(size_t) c] = (float) (y[(size_t) c] * (c == 0 ? omni : directional));
        }
        return g;
    }

    void encode (const float* left, const float* right, int frames, const Gains& from, const Gains& to, float* const* scene)
    {
        const auto step = 1.0f / (float) std::max (1, frames);
        for (int c = 0; c < channels; ++c)
        {
            const auto l0 = from[0][(size_t) c], r0 = from[1][(size_t) c];
            const auto dl = to[0][(size_t) c] - l0, dr = to[1][(size_t) c] - r0;
            auto* out = scene[c];
            if (dl == 0.0f && dr == 0.0f)
            {
                if (l0 == 0.0f && r0 == 0.0f)
                    continue;
                for (int k = 0; k < frames; ++k)
                    out[k] += left[k] * l0 + right[k] * r0;
                continue;
            }
            for (int k = 0; k < frames; ++k)
            {
                const auto t = step * (float) (k + 1);
                out[k] += left[k] * (l0 + dl * t) + right[k] * (r0 + dr * t);
            }
        }
    }

    Decoder::Decoder (Setup setup) : s (std::move (setup))
    {
        if (std::abs (s.yaw) > 1.0e-6)
        {
            const auto m = ambi::rotation (order, s.yaw, 0.0, 0.0);
            rotation.resize (channels);
            for (int r = 0; r < channels; ++r)
                for (int c = 0; c < channels; ++c)
                    rotation[(size_t) r][(size_t) c] = (float) m[(size_t) r][(size_t) c];
        }
        turned.resize (channels);
        const auto two = ambi::decoder (order, ambi::layout ("Stereo"));
        for (int side = 0; side < 2 && side < (int) two.size(); ++side)
            for (int c = 0; c < channels && c < (int) two[(size_t) side].size(); ++c)
                stereo[(size_t) side][(size_t) c] = (float) two[(size_t) side][(size_t) c];
        for (const auto& row : s.speakers)
        {
            if ((int) speak.size() >= maxSpeakers)
                break;
            std::array<float, channels> r {};
            for (int c = 0; c < channels && c < (int) row.size(); ++c)
                r[(size_t) c] = (float) row[(size_t) c];
            speak.push_back (r);
        }
        if (s.ears != nullptr)
            for (int ear = 0; ear < 2; ++ear)
                for (int c = 0; c < channels; ++c)
                {
                    const auto& f = (*s.ears)[(size_t) ear];
                    conv[(size_t) ear].push_back (std::make_unique<binaural::Convolver> (c < (int) f.size() ? f[(size_t) c] : std::vector<float> { 0.0f }, block));
                }
    }

    void Decoder::reset()
    {
        for (auto& p : pending)
            p.fill (0.0f);
        for (auto& r : ready)
            r.fill (0.0f);
        filled = 0;
        // The convolvers' tails ring out within a few partitions of silence.
        for (int i = 0; i < 8 && s.ears != nullptr; ++i)
            for (int ear = 0; ear < 2; ++ear)
                for (auto& cv : conv[(size_t) ear])
                {
                    scratch.fill (0.0f);
                    cv->process (scratch.data(), scratch.data());
                }
    }

    void Decoder::process (float* const* scene, int frames, float* left, float* right, float* const* feeds)
    {
        if (! rotation.empty())
            for (int k = 0; k < frames; ++k)
            {
                for (int r = 0; r < channels; ++r)
                {
                    float sum = 0.0f;
                    for (int c = 0; c < channels; ++c)
                        sum += rotation[(size_t) r][(size_t) c] * scene[c][k];
                    turned[(size_t) r] = sum;
                }
                for (int r = 0; r < channels; ++r)
                    scene[r][k] = turned[(size_t) r];
            }

        for (size_t l = 0; l < speak.size(); ++l)
        {
            auto* out = feeds[l];
            std::fill (out, out + frames, 0.0f);
            for (int c = 0; c < channels; ++c)
                if (const auto g = speak[l][(size_t) c]; g != 0.0f)
                    for (int k = 0; k < frames; ++k)
                        out[k] += scene[c][k] * g;
        }

        if (! s.headphones)
            return;
        if (s.ears == nullptr)
        {
            for (int c = 0; c < channels; ++c)
            {
                const auto gl = stereo[0][(size_t) c], gr = stereo[1][(size_t) c];
                for (int k = 0; k < frames; ++k)
                {
                    left[k] += scene[c][k] * gl;
                    right[k] += scene[c][k] * gr;
                }
            }
            return;
        }
        // Binaural: the scene in whole partitions through each channel's filters, one partition late.
        for (int at = 0; at < frames;)
        {
            const auto n = std::min (block - filled, frames - at);
            for (int c = 0; c < channels; ++c)
                std::copy (scene[c] + at, scene[c] + at + n, pending[(size_t) c].begin() + filled);
            for (int k = 0; k < n; ++k)
            {
                left[at + k] += ready[0][(size_t) (filled + k)];
                right[at + k] += ready[1][(size_t) (filled + k)];
            }
            filled += n;
            at += n;
            if (filled < block)
                continue;
            filled = 0;
            for (int ear = 0; ear < 2; ++ear)
            {
                auto& out = ready[(size_t) ear];
                out.fill (0.0f);
                for (int c = 0; c < channels; ++c)
                {
                    conv[(size_t) ear][(size_t) c]->process (pending[(size_t) c].data(), scratch.data());
                    for (int k = 0; k < block; ++k)
                        out[(size_t) k] += scratch[(size_t) k];
                }
            }
        }
    }
}
