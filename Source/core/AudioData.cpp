// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioData.h"

#include <algorithm>
#include <cmath>

AudioChunk::AudioChunk (std::vector<std::vector<float>> channelSamples)
    : samples (std::move (channelSamples))
{
    numFrames = samples.empty() ? 0 : (int) samples[0].size();
    const auto numPeaks = (size_t) ((numFrames + framesPerPeak - 1) / framesPerPeak);

    peakMin.assign (samples.size(), std::vector<float> (numPeaks));
    peakMax.assign (samples.size(), std::vector<float> (numPeaks));

    for (size_t ch = 0; ch < samples.size(); ++ch)
    {
        const auto& s = samples[ch];

        for (size_t p = 0; p < numPeaks; ++p)
        {
            const auto first = s.begin() + (long) (p * framesPerPeak);
            const auto last  = s.begin() + (long) std::min ((size_t) numFrames, (p + 1) * framesPerPeak);
            const auto [lo, hi] = std::minmax_element (first, last);
            peakMin[ch][p] = *lo;
            peakMax[ch][p] = *hi;
        }
    }
}

//==============================================================================
AudioData::AudioData (int numChannels, double sampleRate) : channels (numChannels), rate (sampleRate) {}

int AudioData::chunkIndexFor (Frame position) const
{
    const auto it = std::upper_bound (starts.begin(), starts.end(), position);
    return (int) (it - starts.begin()) - 1;
}

void AudioData::read (Frame start, int numFrames, float* const* dest) const
{
    for (int ch = 0; ch < channels; ++ch)
        std::fill (dest[ch], dest[ch] + numFrames, 0.0f);

    Frame pos = std::max<Frame> (start, 0);
    const Frame end = std::min<Frame> (start + numFrames, total);

    while (pos < end)
    {
        const auto index = chunkIndexFor (pos);
        const auto& chunk = *chunks[(size_t) index];
        const auto offset = (int) (pos - starts[(size_t) index]);
        const auto count = (int) std::min<Frame> (chunk.numFrames - offset, end - pos);

        for (int ch = 0; ch < channels; ++ch)
            std::copy_n (chunk.samples[(size_t) ch].data() + offset, count, dest[ch] + (pos - start));

        pos += count;
    }
}

void AudioData::minMax (int channel, Frame start, Frame end, float& lo, float& hi) const
{
    lo = hi = 0.0f;
    start = std::max<Frame> (start, 0);
    end = std::min (end, total);

    bool first = true;
    Frame pos = start;

    while (pos < end)
    {
        const auto index = chunkIndexFor (pos);
        const auto& chunk = *chunks[(size_t) index];
        const auto chunkStart = starts[(size_t) index];
        auto offset = (int) (pos - chunkStart);
        const auto stop = (int) std::min<Frame> (chunk.numFrames, end - chunkStart);
        const auto& s = chunk.samples[(size_t) channel];

        auto take = [&] (float a, float b)
        {
            if (first) { lo = a; hi = b; first = false; }
            else       { lo = std::min (lo, a); hi = std::max (hi, b); }
        };

        while (offset < stop)
        {
            if (offset % AudioChunk::framesPerPeak == 0 && offset + AudioChunk::framesPerPeak <= stop)
            {
                const auto p = (size_t) (offset / AudioChunk::framesPerPeak);
                take (chunk.peakMin[(size_t) channel][p], chunk.peakMax[(size_t) channel][p]);
                offset += AudioChunk::framesPerPeak;
            }
            else
            {
                take (s[(size_t) offset], s[(size_t) offset]);
                ++offset;
            }
        }

        pos = chunkStart + stop;
    }
}

float AudioData::peakAbs (Frame start, Frame end) const
{
    float peak = 0.0f;

    for (int ch = 0; ch < channels; ++ch)
    {
        float lo, hi;
        minMax (ch, start, end, lo, hi);
        peak = std::max ({ peak, std::abs (lo), std::abs (hi) });
    }

    return peak;
}

AudioDataPtr AudioData::slice (Frame start, Frame end) const
{
    Builder b (channels, rate);
    b.appendShared (*this, start, end);
    return b.finish();
}

AudioDataPtr AudioData::replaced (Frame start, Frame end, const AudioData* insert) const
{
    Builder b (channels, rate);
    b.appendShared (*this, 0, start);

    if (insert != nullptr && insert->length() > 0)
    {
        if (insert->numChannels() == channels)
        {
            b.appendShared (*insert, 0, insert->length());
        }
        else
        {
            // Mono fills every channel; into mono, left and right are averaged;
            // otherwise channels match by position (L to L, R to R) and the rest are silent.
            const auto inChannels = insert->numChannels();
            std::vector<std::vector<float>> src ((size_t) inChannels, std::vector<float> (AudioChunk::maxFrames));
            std::vector<float> silent (AudioChunk::maxFrames, 0.0f), mixed (AudioChunk::maxFrames);
            std::vector<float*> srcPtrs, dstPtrs;
            for (auto& v : src)
                srcPtrs.push_back (v.data());
            for (int ch = 0; ch < channels; ++ch)
            {
                if (inChannels == 1)
                    dstPtrs.push_back (src[0].data());
                else if (channels == 1)
                    dstPtrs.push_back (mixed.data());
                else
                    dstPtrs.push_back (ch < inChannels ? src[(size_t) ch].data() : silent.data());
            }

            for (Frame pos = 0; pos < insert->length(); pos += AudioChunk::maxFrames)
            {
                const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, insert->length() - pos);
                insert->read (pos, n, srcPtrs.data());
                if (channels == 1)
                    for (int i = 0; i < n; ++i)
                        mixed[(size_t) i] = 0.5f * (src[0][(size_t) i] + src[1][(size_t) i]);
                b.append (dstPtrs.data(), n);
            }
        }
    }

    b.appendShared (*this, end, total);
    return b.finish();
}

AudioDataPtr AudioData::processed (Frame start, Frame end, const Processor& process) const
{
    start = std::clamp<Frame> (start, 0, total);
    end = std::clamp<Frame> (end, start, total);

    Builder b (channels, rate);
    b.appendShared (*this, 0, start);

    std::vector<std::vector<float>> block ((size_t) channels);
    std::vector<float*> ptrs ((size_t) channels);

    for (Frame pos = start; pos < end; pos += AudioChunk::maxFrames)
    {
        const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, end - pos);

        for (int ch = 0; ch < channels; ++ch)
        {
            block[(size_t) ch].resize ((size_t) n);
            ptrs[(size_t) ch] = block[(size_t) ch].data();
        }

        read (pos, n, ptrs.data());
        process (block, pos);

        for (int ch = 0; ch < channels; ++ch)
            ptrs[(size_t) ch] = block[(size_t) ch].data();

        b.append (ptrs.data(), n);
    }

    b.appendShared (*this, end, total);
    return b.finish();
}

//==============================================================================
AudioData::Builder::Builder (int numChannels, double sampleRate)
    : data (std::make_shared<AudioData> (numChannels, sampleRate)), pending ((size_t) numChannels)
{
}

void AudioData::Builder::append (const float* const* channelData, int numFrames)
{
    int done = 0;

    while (done < numFrames)
    {
        const auto space = AudioChunk::maxFrames - (int) pending[0].size();
        const auto n = std::min (space, numFrames - done);

        for (size_t ch = 0; ch < pending.size(); ++ch)
            pending[ch].insert (pending[ch].end(), channelData[ch] + done, channelData[ch] + done + n);

        done += n;

        if ((int) pending[0].size() == AudioChunk::maxFrames)
            flush();
    }
}

void AudioData::Builder::appendShared (const AudioData& source, Frame start, Frame end)
{
    start = std::clamp<Frame> (start, 0, source.total);
    end = std::clamp<Frame> (end, start, source.total);

    Frame pos = start;

    while (pos < end)
    {
        const auto index = source.chunkIndexFor (pos);
        const auto& chunk = source.chunks[(size_t) index];
        const auto chunkStart = source.starts[(size_t) index];
        const auto offset = (int) (pos - chunkStart);
        const auto count = (int) std::min<Frame> (chunk->numFrames - offset, end - pos);

        if (offset == 0 && count == chunk->numFrames)
        {
            // Whole chunk: share it instead of copying.
            flush();
            data->starts.push_back (data->total);
            data->chunks.push_back (chunk);
            data->total += count;
        }
        else
        {
            std::vector<const float*> ptrs;
            for (auto& s : chunk->samples)
                ptrs.push_back (s.data() + offset);

            append (ptrs.data(), count);
        }

        pos += count;
    }
}

void AudioData::Builder::flush()
{
    if (pending.empty() || pending[0].empty())
        return;

    const auto n = (Frame) pending[0].size();
    data->starts.push_back (data->total);
    data->chunks.push_back (std::make_shared<const AudioChunk> (std::move (pending)));
    data->total += n;
    pending.assign ((size_t) data->channels, {});
}

AudioDataPtr AudioData::Builder::finish()
{
    flush();
    return data;
}
