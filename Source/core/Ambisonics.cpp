// SPDX-License-Identifier: AGPL-3.0-only
#include "Ambisonics.h"
#include "Binaural.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

namespace ambi
{
    namespace
    {
        constexpr double degrees = M_PI / 180.0;
        using Vec = std::array<double, 3>;   // x front, y left, z up (AmbiX)

        Vec toVector (double azimuth, double elevation)
        {
            const auto phi = -azimuth * degrees, theta = elevation * degrees;
            return { std::cos (theta) * std::cos (phi), std::cos (theta) * std::sin (phi), std::sin (theta) };
        }

        void toAngles (const Vec& v, double& azimuth, double& elevation)
        {
            const auto length = std::sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            azimuth = -std::atan2 (v[1], v[0]) / degrees;
            elevation = length > 0.0 ? std::asin (std::clamp (v[2] / length, -1.0, 1.0)) / degrees : 0.0;
        }

        double dot (const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
        Vec cross (const Vec& a, const Vec& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
        Vec minus (const Vec& a, const Vec& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }

        // Nearly even points over the sphere.
        std::vector<Vec> fibonacci (int count)
        {
            std::vector<Vec> points;
            const double golden = M_PI * (3.0 - std::sqrt (5.0));
            for (int i = 0; i < count; ++i)
            {
                const auto z = 1.0 - (i + 0.5) * 2.0 / count;
                const auto r = std::sqrt (std::max (0.0, 1.0 - z * z));
                points.push_back ({ r * std::cos (golden * i), r * std::sin (golden * i), z });
            }
            return points;
        }

        double factorial (int n)
        {
            double f = 1.0;
            for (int i = 2; i <= n; ++i)
                f *= i;
            return f;
        }

        // Legendre polynomial P_l(x).
        double legendre (int l, double x)
        {
            if (l == 0) return 1.0;
            double p0 = 1.0, p1 = x;
            for (int n = 2; n <= l; ++n)
            {
                const auto p2 = ((2 * n - 1) * x * p1 - (n - 1) * p0) / n;
                p0 = p1;
                p1 = p2;
            }
            return p1;
        }

        int orderOf (int acn) { return (int) std::floor (std::sqrt ((double) acn)); }

        // Solves A x = B for each column of B (A: n x n), by Gaussian elimination.
        Matrix solve (Matrix a, Matrix b)
        {
            const auto n = a.size();
            for (size_t col = 0; col < n; ++col)
            {
                auto pivot = col;
                for (auto r = col + 1; r < n; ++r)
                    if (std::abs (a[r][col]) > std::abs (a[pivot][col]))
                        pivot = r;
                std::swap (a[col], a[pivot]);
                std::swap (b[col], b[pivot]);
                const auto d = a[col][col];
                if (std::abs (d) < 1e-12)
                    continue;
                for (auto r = 0u; r < n; ++r)
                {
                    if (r == col)
                        continue;
                    const auto f = a[r][col] / d;
                    if (f == 0.0)
                        continue;
                    for (size_t c = col; c < n; ++c)
                        a[r][c] -= f * a[col][c];
                    for (size_t c = 0; c < b[r].size(); ++c)
                        b[r][c] -= f * b[col][c];
                }
            }
            for (size_t r = 0; r < n; ++r)
                if (std::abs (a[r][r]) > 1e-12)
                    for (auto& v : b[r])
                        v /= a[r][r];
            return b;
        }

        // The matrix that moves every direction d of a scene to f(d): least squares over many directions,
        // exact because a turned or mirrored order-N scene is again an order-N scene.
        template <typename Map>
        Matrix directionMap (int order, Map f)
        {
            const auto k = (size_t) channelsFor (order);
            const auto points = fibonacci ((int) k * 6 + 30);
            Matrix ata (k, std::vector<double> (k, 0.0)), atb (k, std::vector<double> (k, 0.0));
            for (const auto& p : points)
            {
                double az = 0.0, el = 0.0;
                toAngles (p, az, el);
                const auto a = harmonics (order, az, el);
                toAngles (f (p), az, el);
                const auto b = harmonics (order, az, el);
                for (size_t i = 0; i < k; ++i)
                    for (size_t j = 0; j < k; ++j)
                    {
                        ata[i][j] += a[i] * a[j];
                        atb[i][j] += a[i] * b[j];
                    }
            }
            // ata * M^T = atb
            const auto mt = solve (ata, atb);
            Matrix m (k, std::vector<double> (k, 0.0));
            for (size_t i = 0; i < k; ++i)
                for (size_t j = 0; j < k; ++j)
                    m[i][j] = std::abs (mt[j][i]) < 1e-9 ? 0.0 : mt[j][i];
            return m;
        }

        std::vector<std::vector<float>> readAll (const AudioData& d, Frame start, int n)
        {
            std::vector<std::vector<float>> in ((size_t) d.numChannels(), std::vector<float> ((size_t) n, 0.0f));
            std::vector<float*> ptrs;
            for (auto& v : in)
                ptrs.push_back (v.data());
            d.read (start, n, ptrs.data());
            return in;
        }

        void append (AudioData::Builder& out, std::vector<std::vector<float>>& channels, int n)
        {
            std::vector<const float*> ptrs;
            for (auto& v : channels)
                ptrs.push_back (v.data());
            out.append (ptrs.data(), n);
        }
    }

    //==========================================================================
    int channelsFor (int order) { return (order + 1) * (order + 1); }

    int orderFor (int channels)
    {
        for (int n = 1; n <= maxOrder; ++n)
            if (channelsFor (n) == channels)
                return n;
        return -1;
    }

    std::string channelName (int acn)
    {
        static const char* names[] { "W", "Y", "Z", "X", "V", "T", "R", "S", "U", "Q", "O", "M", "K", "L", "N", "P" };
        return acn >= 0 && acn < 16 ? names[acn] : "ACN " + std::to_string (acn);
    }

    std::vector<double> harmonics (int order, double azimuth, double elevation)
    {
        const auto phi = -azimuth * degrees, x = std::sin (elevation * degrees);
        const auto c = std::sqrt (std::max (0.0, 1.0 - x * x));
        std::vector<double> y ((size_t) channelsFor (order), 0.0);
        for (int m = 0; m <= order; ++m)
        {
            // P_m^m, then upwards in l (no Condon-Shortley phase).
            double pmm = 1.0;
            for (int i = 1; i <= m; ++i)
                pmm *= (2 * i - 1) * c;
            double prev = 0.0, p = pmm;
            for (int l = m; l <= order; ++l)
            {
                if (l == m + 1)
                {
                    prev = p;
                    p = x * (2 * m + 1) * pmm;
                }
                else if (l > m + 1)
                {
                    const auto next = ((2 * l - 1) * x * p - (l + m - 1) * prev) / (l - m);
                    prev = p;
                    p = next;
                }
                const auto norm = std::sqrt ((m == 0 ? 1.0 : 2.0) * factorial (l - m) / factorial (l + m));
                y[(size_t) (l * l + l + m)] = norm * p * std::cos (m * phi);
                if (m > 0)
                    y[(size_t) (l * l + l - m)] = norm * p * std::sin (m * phi);
            }
        }
        return y;
    }

    std::vector<double> maxReWeights (int order)
    {
        std::vector<double> w;
        const auto angle = 137.9 * degrees / (order + 1.51);
        for (int l = 0; l <= order; ++l)
            w.push_back (legendre (l, std::cos (angle)));
        return w;
    }

    Matrix rotation (int order, double yaw, double pitch, double roll)
    {
        return directionMap (order, [&] (const Vec& v)
        {
            double az = 0.0, el = 0.0;
            toAngles (v, az, el);
            auto r = toVector (az + yaw, el);
            const auto p = pitch * degrees, q = roll * degrees;
            r = { r[0] * std::cos (p) - r[2] * std::sin (p), r[1], r[0] * std::sin (p) + r[2] * std::cos (p) };
            r = { r[0], r[1] * std::cos (q) - r[2] * std::sin (q), r[1] * std::sin (q) + r[2] * std::cos (q) };
            return r;
        });
    }

    Matrix mirror (int order, bool leftRight, bool frontBack, bool upDown)
    {
        return directionMap (order, [&] (const Vec& v)
        {
            return Vec { frontBack ? -v[0] : v[0], leftRight ? -v[1] : v[1], upDown ? -v[2] : v[2] };
        });
    }

    Matrix fumaToAmbix (int order)
    {
        order = std::clamp (order, 1, 3);
        // FuMa channels W X Y Z R S T U V K L M N O P Q; for each ACN, its FuMa channel and the factor.
        static const std::pair<int, double> table[] {
            { 0, M_SQRT2 }, { 2, 1.0 }, { 3, 1.0 }, { 1, 1.0 },
            { 8, 2.0 / std::sqrt (3.0) }, { 6, 2.0 / std::sqrt (3.0) }, { 4, 1.0 }, { 5, 2.0 / std::sqrt (3.0) }, { 7, 2.0 / std::sqrt (3.0) },
            { 15, std::sqrt (8.0 / 5.0) }, { 13, 3.0 / std::sqrt (5.0) }, { 11, std::sqrt (45.0 / 32.0) }, { 9, 1.0 },
            { 10, std::sqrt (45.0 / 32.0) }, { 12, 3.0 / std::sqrt (5.0) }, { 14, std::sqrt (8.0 / 5.0) } };
        const auto k = (size_t) channelsFor (order);
        Matrix m (k, std::vector<double> (k, 0.0));
        for (size_t acn = 0; acn < k; ++acn)
            m[acn][(size_t) table[acn].first] = table[acn].second;
        return m;
    }

    Matrix ambixToFuma (int order)
    {
        const auto forward = fumaToAmbix (order);
        Matrix m (forward.size(), std::vector<double> (forward.size(), 0.0));
        for (size_t i = 0; i < forward.size(); ++i)
            for (size_t j = 0; j < forward.size(); ++j)
                if (forward[i][j] != 0.0)
                    m[j][i] = 1.0 / forward[i][j];
        return m;
    }

    //==========================================================================
    std::vector<std::string> layoutNames() { return { "Stereo", "Quad", "5.0", "5.1", "5.2", "7.1", "7.2", "7.1.2", "7.1.4", "7.2.4", "9.1.6" }; }

    std::vector<Speaker> layout (const std::string& name)
    {
        if (name == "Stereo") return { { "L", -30, 0 }, { "R", 30, 0 } };
        if (name == "Quad")   return { { "L", -45, 0 }, { "R", 45, 0 }, { "Ls", -135, 0 }, { "Rs", 135, 0 } };
        if (name == "5.0")    return { { "L", -30, 0 }, { "R", 30, 0 }, { "C", 0, 0 }, { "Ls", -110, 0 }, { "Rs", 110, 0 } };
        if (name == "5.1")    return { { "L", -30, 0 }, { "R", 30, 0 }, { "C", 0, 0 }, { "LFE", 0, 0, true }, { "Ls", -110, 0 }, { "Rs", 110, 0 } };
        if (name == "5.2")    return { { "L", -30, 0 }, { "R", 30, 0 }, { "C", 0, 0 }, { "LFE1", 0, 0, true }, { "LFE2", 0, 0, true },
                                       { "Ls", -110, 0 }, { "Rs", 110, 0 } };
        const bool twoSubs = name == "7.2" || name == "7.2.4";
        std::vector<Speaker> s { { "L", -30, 0 }, { "R", 30, 0 }, { "C", 0, 0 }, { twoSubs ? "LFE1" : "LFE", 0, 0, true } };
        if (twoSubs)
            s.push_back ({ "LFE2", 0, 0, true });
        s.insert (s.end(), { { "Lb", -150, 0 }, { "Rb", 150, 0 }, { "Ls", -90, 0 }, { "Rs", 90, 0 } });
        if (name == "9.1.6")
        {
            s.insert (s.end(), { { "Lw", -60, 0 }, { "Rw", 60, 0 }, { "Ltf", -45, 45 }, { "Rtf", 45, 45 },
                                 { "Ltm", -90, 45 }, { "Rtm", 90, 45 }, { "Ltb", -135, 45 }, { "Rtb", 135, 45 } });
            return s;
        }
        if (name == "7.1.2" || name == "7.1.4" || name == "7.2.4")
        {
            s.push_back ({ "Ltf", -45, 45 });
            s.push_back ({ "Rtf", 45, 45 });
        }
        if (name == "7.1.4" || name == "7.2.4")
        {
            s.push_back ({ "Ltb", -135, 45 });
            s.push_back ({ "Rtb", 135, 45 });
        }
        return s;
    }

    Matrix decoder (int order, const std::vector<Speaker>& speakers)
    {
        const auto k = (size_t) channelsFor (order);
        std::vector<Vec> points;
        std::vector<int> realIndex;   // per point: the speaker, or -1 for an imaginary one
        for (size_t i = 0; i < speakers.size(); ++i)
            if (! speakers[i].lfe)
            {
                points.push_back (toVector (speakers[i].azimuth, speakers[i].elevation));
                realIndex.push_back ((int) i);
            }
        const auto real = points.size();
        Matrix d (speakers.size(), std::vector<double> (k, 0.0));
        const auto weights = maxReWeights (order);
        const auto virtuals = fibonacci (240);

        // Two speakers cannot surround anything: there each is a virtual microphone (a max-rE beam)
        // of first order aimed twice as wide as its speaker, which keeps left and right apart.
        if (real <= 2)
        {
            for (size_t p = 0; p < real; ++p)
            {
                const auto& s = speakers[(size_t) realIndex[p]];
                // First order only: higher orders make beams so narrow the middle drops out.
                const auto y = harmonics (1, s.azimuth * 2.0, s.elevation);
                const auto first = maxReWeights (1);
                for (size_t c = 0; c < 4; ++c)
                    d[(size_t) realIndex[p]][c] = y[c] * first[(size_t) orderOf ((int) c)] * (2 * orderOf ((int) c) + 1);
            }
        }
        else
        {

        // Imaginary speakers fill what the layout leaves open: below and above, and wide horizontal gaps.
        double lowest = 90.0, highest = -90.0;
        std::vector<double> ring;
        for (const auto& s : speakers)
            if (! s.lfe)
            {
                lowest = std::min (lowest, s.elevation);
                highest = std::max (highest, s.elevation);
                if (std::abs (s.elevation) < 30.0)
                    ring.push_back (s.azimuth);
            }
        if (lowest > -45.0) { points.push_back ({ 0, 0, -1 }); realIndex.push_back (-1); }
        if (highest < 45.0) { points.push_back ({ 0, 0, 1 }); realIndex.push_back (-1); }
        std::sort (ring.begin(), ring.end());
        for (size_t i = 0; i < ring.size(); ++i)
        {
            const auto a = ring[i], b = i + 1 < ring.size() ? ring[i + 1] : ring[0] + 360.0;
            if (b - a > 150.0)
                for (int part = 1; part <= 2; ++part)
                {
                    points.push_back (toVector (a + (b - a) * part / 3.0, 0.0));
                    realIndex.push_back (-1);
                }
        }

        // The hull's faces: every triangle with all the other speakers on one side.
        std::vector<std::array<size_t, 3>> faces;
        for (size_t i = 0; i < points.size(); ++i)
            for (size_t j = i + 1; j < points.size(); ++j)
                for (size_t l = j + 1; l < points.size(); ++l)
                {
                    const auto n = cross (minus (points[j], points[i]), minus (points[l], points[i]));
                    if (dot (n, n) < 1e-10)
                        continue;
                    bool above = false, below = false;
                    for (size_t o = 0; o < points.size(); ++o)
                    {
                        const auto side = dot (n, minus (points[o], points[i]));
                        above = above || side > 1e-9;
                        below = below || side < -1e-9;
                    }
                    if (! (above && below))
                        faces.push_back ({ i, j, l });
                }

        // VBAP gains for each virtual speaker; imaginary speakers' energy is shared out.
        for (const auto& v : virtuals)
        {
            std::vector<double> gains (points.size(), 0.0);
            for (const auto& f : faces)
            {
                const auto& a = points[f[0]];
                const auto& b = points[f[1]];
                const auto& c = points[f[2]];
                const auto det = dot (a, cross (b, c));
                if (std::abs (det) < 1e-9)
                    continue;
                const double g[3] { dot (v, cross (b, c)) / det, dot (a, cross (v, c)) / det, dot (a, cross (b, v)) / det };
                if (g[0] < -1e-6 || g[1] < -1e-6 || g[2] < -1e-6)
                    continue;
                for (int t = 0; t < 3; ++t)
                    gains[f[(size_t) t]] = std::max (0.0, g[t]);
                break;
            }
            double total = 0.0, imaginary = 0.0;
            for (size_t p = 0; p < points.size(); ++p)
            {
                total += gains[p] * gains[p];
                if (realIndex[p] < 0)
                    imaginary += gains[p] * gains[p];
            }
            if (total <= 0.0)
                continue;
            double az = 0.0, el = 0.0;
            toAngles (v, az, el);
            const auto y = harmonics (order, az, el);
            for (size_t p = 0; p < real; ++p)
            {
                const auto g = std::sqrt ((gains[p] * gains[p] + imaginary / (double) real) / total);
                for (size_t c = 0; c < k; ++c)
                    d[(size_t) realIndex[p]][c] += g * y[c] * weights[(size_t) orderOf ((int) c)] * (2 * orderOf ((int) c) + 1);
            }
        }

        }

        // One plane wave, from anywhere, comes out with energy 1 on average.
        double energy = 0.0;
        for (const auto& v : virtuals)
        {
            double az = 0.0, el = 0.0;
            toAngles (v, az, el);
            const auto y = harmonics (order, az, el);
            for (const auto& row : d)
            {
                double s = 0.0;
                for (size_t c = 0; c < k; ++c)
                    s += row[c] * y[c];
                energy += s * s;
            }
        }
        const auto scale = energy > 0.0 ? 1.0 / std::sqrt (energy / (double) virtuals.size()) : 0.0;
        for (auto& row : d)
            for (auto& v : row)
                v *= scale;
        return d;
    }

    //==========================================================================
    AudioDataPtr encode (const AudioData& d, const EncodeSettings& s)
    {
        const auto order = std::clamp (s.order, 1, maxOrder);
        const auto k = (size_t) channelsFor (order);
        const auto inChannels = std::min (d.numChannels(), 2);
        std::vector<std::vector<double>> gains;
        for (int ch = 0; ch < inChannels; ++ch)
        {
            const auto az = inChannels == 1 ? s.azimuth : s.azimuth + (ch == 0 ? -0.5 : 0.5) * std::clamp (s.width, 0.0, 360.0);
            gains.push_back (harmonics (order, az, s.elevation));
        }

        AudioData::Builder out ((int) k, d.sampleRate());
        std::vector<std::vector<float>> block (k);
        for (Frame pos = 0; pos < d.length();)
        {
            const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, d.length() - pos);
            const auto in = readAll (d, pos, n);
            for (size_t c = 0; c < k; ++c)
            {
                block[c].assign ((size_t) n, 0.0f);
                for (int src = 0; src < inChannels; ++src)
                {
                    const auto g = (float) gains[(size_t) src][c];
                    for (int i = 0; i < n; ++i)
                        block[c][(size_t) i] += g * in[(size_t) src][(size_t) i];
                }
            }
            append (out, block, n);
            pos += n;
        }
        return out.finish();
    }

    AudioDataPtr transform (const AudioData& d, Frame start, Frame end, const Matrix& m)
    {
        const auto k = (size_t) d.numChannels();
        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());
        const auto edge = std::max<Frame> (1, (Frame) (d.sampleRate() * 0.01));

        AudioData::Builder out ((int) k, d.sampleRate());
        if (start > 0)
            out.appendShared (d, 0, start);
        std::vector<std::vector<float>> block (k);
        for (Frame pos = start; pos < end;)
        {
            const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, end - pos);
            const auto in = readAll (d, pos, n);
            for (size_t r = 0; r < k; ++r)
            {
                block[r].assign ((size_t) n, 0.0f);
                for (size_t c = 0; c < k && c < m[r].size(); ++c)
                {
                    const auto g = (float) m[r][c];
                    if (g != 0.0f)
                        for (int i = 0; i < n; ++i)
                            block[r][(size_t) i] += g * in[c][(size_t) i];
                }
                for (int i = 0; i < n; ++i)
                {
                    const auto at = pos + i;
                    const auto ramp = (float) std::min ({ 1.0, (double) (at - start + 1) / (double) edge, (double) (end - at) / (double) edge });
                    block[r][(size_t) i] = ramp * block[r][(size_t) i] + (1.0f - ramp) * in[r][(size_t) i];
                }
            }
            append (out, block, n);
            pos += n;
        }
        if (end < d.length())
            out.appendShared (d, end, d.length());
        return out.finish();
    }

    AudioDataPtr mix (const AudioData& d, const Matrix& m, std::atomic<float>* progress)
    {
        const auto outs = m.size();
        AudioData::Builder out ((int) outs, d.sampleRate());
        std::vector<std::vector<float>> block (outs);
        for (Frame pos = 0; pos < d.length();)
        {
            const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, d.length() - pos);
            const auto in = readAll (d, pos, n);
            for (size_t r = 0; r < outs; ++r)
            {
                block[r].assign ((size_t) n, 0.0f);
                for (size_t c = 0; c < in.size() && c < m[r].size(); ++c)
                {
                    const auto g = (float) m[r][c];
                    if (g != 0.0f)
                        for (int i = 0; i < n; ++i)
                            block[r][(size_t) i] += g * in[c][(size_t) i];
                }
            }
            append (out, block, n);
            pos += n;
            if (progress != nullptr)
                progress->store ((float) pos / (float) std::max<Frame> (1, d.length()));
        }
        return out.finish();
    }

    namespace
    {
        using EarFilters = std::array<std::vector<std::vector<float>>, 2>;   // [ear][channel]

        // The ambisonic channels' filters for each ear: a dense virtual layout heard through the HRTF.
        std::shared_ptr<const EarFilters> buildEarFilters (const std::string& hrtfPath, int order, double rate, std::string& error)
        {
            const auto hrtf = binaural::Hrtf::open (hrtfPath, rate, error);
            if (hrtf == nullptr)
                return {};

            // A dense virtual layout, each speaker heard through its HRTF, folded into one filter per channel and ear.
            const auto k = (size_t) channelsFor (order);
            const auto virtuals = fibonacci (std::max (50, (int) k * 4));
            const auto weights = maxReWeights (order);
            auto made = std::make_shared<EarFilters>();
            auto& filters = *made;
            filters[0].resize (k);
            filters[1].resize (k);
            std::vector<float> l, r, frontL, frontR;
            for (const auto& v : virtuals)
            {
                double az = 0.0, el = 0.0;
                toAngles (v, az, el);
                hrtf->filters (az, el, l, r);
                const auto y = harmonics (order, az, el);
                for (size_t c = 0; c < k; ++c)
                {
                    const auto g = (float) (y[c] * weights[(size_t) orderOf ((int) c)] * (2 * orderOf ((int) c) + 1) / (double) virtuals.size());
                    for (int ear = 0; ear < 2; ++ear)
                    {
                        const auto& h = ear == 0 ? l : r;
                        auto& f = filters[(size_t) ear][c];
                        if (f.size() < h.size())
                            f.resize (h.size(), 0.0f);
                        for (size_t t = 0; t < h.size(); ++t)
                            f[t] += g * h[t];
                    }
                }
            }

            // Level: a plane wave from the front matches the HRTF's own front response.
            hrtf->filters (0.0, 0.0, frontL, frontR);
            const auto front = harmonics (order, 0.0, 0.0);
            double want = 0.0, got = 0.0;
            for (auto x : frontL) want += (double) x * x;
            for (auto x : frontR) want += (double) x * x;
            for (int ear = 0; ear < 2; ++ear)
            {
                size_t longest = 0;
                for (const auto& f : filters[(size_t) ear])
                    longest = std::max (longest, f.size());
                for (size_t t = 0; t < longest; ++t)
                {
                    double s = 0.0;
                    for (size_t c = 0; c < k; ++c)
                        if (t < filters[(size_t) ear][c].size())
                            s += front[c] * filters[(size_t) ear][c][t];
                    got += s * s;
                }
            }
            const auto scale = got > 0.0 ? (float) std::sqrt (want / got) : 1.0f;
            for (auto& ear : filters)
                for (auto& f : ear)
                    for (auto& x : f)
                        x *= scale;
            return made;
        }

        // Built once per HRTF file, order and rate: a preview that is made again reuses them.
        std::shared_ptr<const EarFilters> earFilters (const std::string& hrtfPath, int order, double rate, std::string& error)
        {
            static std::mutex lock;
            static std::map<std::tuple<std::string, int, double>, std::shared_ptr<const EarFilters>> cache;
            const auto key = std::make_tuple (hrtfPath, order, rate);
            {
                std::lock_guard<std::mutex> guard (lock);
                if (const auto it = cache.find (key); it != cache.end())
                    return it->second;
            }
            auto made = buildEarFilters (hrtfPath, order, rate, error);
            if (made != nullptr)
            {
                std::lock_guard<std::mutex> guard (lock);
                if (cache.size() > 8)
                    cache.clear();
                cache[key] = made;
            }
            return made;
        }
    }

    AudioDataPtr decodeBinaural (const AudioData& d, const std::string& hrtfPath, std::string& error, std::atomic<float>* progress)
    {
        const auto order = orderFor (d.numChannels());
        if (order < 1)
        {
            error = "This is not an ambisonic file (it needs 4, 9, 16, 25 or 36 channels)";
            return {};
        }
        const auto cached = earFilters (hrtfPath, order, d.sampleRate(), error);
        if (cached == nullptr)
            return {};
        const auto k = (size_t) channelsFor (order);
        const auto& filters = *cached;

        constexpr int block = 4096;
        std::vector<std::unique_ptr<binaural::Convolver>> conv[2];
        for (int ear = 0; ear < 2; ++ear)
            for (size_t c = 0; c < k; ++c)
                conv[ear].push_back (std::make_unique<binaural::Convolver> (filters[(size_t) ear][c], block));

        AudioData::Builder out (2, d.sampleRate());
        std::vector<std::vector<float>> ears (2, std::vector<float> (block));
        std::vector<float> scratch (block), input (block);
        for (Frame pos = 0; pos < d.length(); pos += block)
        {
            const auto n = (int) std::min<Frame> (block, d.length() - pos);
            const auto in = readAll (d, pos, n);
            for (auto& e : ears)
                std::fill (e.begin(), e.end(), 0.0f);
            for (size_t c = 0; c < k; ++c)
            {
                std::fill (input.begin(), input.end(), 0.0f);
                std::copy (in[c].begin(), in[c].end(), input.begin());
                for (int ear = 0; ear < 2; ++ear)
                {
                    conv[ear][c]->process (input.data(), scratch.data());
                    for (int i = 0; i < block; ++i)
                        ears[(size_t) ear][(size_t) i] += scratch[(size_t) i];
                }
            }
            append (out, ears, n);
            if (progress != nullptr)
                progress->store ((float) (pos + n) / (float) std::max<Frame> (1, d.length()));
        }
        return out.finish();
    }

    void energyMap (const float* const* channels, int numChannels, int frames, int cols, int rows, std::vector<float>& out)
    {
        out.assign ((size_t) (cols * rows), 0.0f);
        const auto order = orderFor (numChannels);
        if (order < 1 || frames <= 0)
            return;
        const auto k = (size_t) numChannels;

        // The beams for this grid, made once.
        static std::mutex lock;
        static std::map<std::array<int, 3>, std::vector<std::vector<double>>> cache;
        std::vector<std::vector<double>>* beams = nullptr;
        {
            std::lock_guard<std::mutex> guard (lock);
            auto& b = cache[{ order, cols, rows }];
            if (b.empty())
            {
                const auto weights = maxReWeights (order);
                for (int row = 0; row < rows; ++row)
                    for (int col = 0; col < cols; ++col)
                    {
                        const auto az = -180.0 + (col + 0.5) * 360.0 / cols;
                        const auto el = 90.0 - (row + 0.5) * 180.0 / rows;
                        auto y = harmonics (order, az, el);
                        for (size_t c = 0; c < k; ++c)
                            y[c] *= weights[(size_t) orderOf ((int) c)] * (2 * orderOf ((int) c) + 1);
                        b.push_back (std::move (y));
                    }
            }
            beams = &b;
        }

        // The channels' covariance, then each beam's power from it.
        std::vector<double> cov (k * k, 0.0);
        for (size_t i = 0; i < k; ++i)
            for (size_t j = i; j < k; ++j)
            {
                double s = 0.0;
                for (int t = 0; t < frames; ++t)
                    s += (double) channels[i][t] * channels[j][t];
                cov[i * k + j] = cov[j * k + i] = s / frames;
            }
        for (size_t p = 0; p < beams->size(); ++p)
        {
            const auto& a = (*beams)[p];
            double power = 0.0;
            for (size_t i = 0; i < k; ++i)
            {
                double row = 0.0;
                for (size_t j = 0; j < k; ++j)
                    row += cov[i * k + j] * a[j];
                power += a[i] * row;
            }
            out[p] = (float) std::max (0.0, power);
        }
    }
}
