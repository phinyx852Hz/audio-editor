// SPDX-License-Identifier: AGPL-3.0-only
#include "Recorder.h"
#include "Cameras.h"
#include "../core/Dsp.h"
#include "../engine/AudioEngine.h"

extern "C"
{
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <QDateTime>
#include <QDir>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <deque>

namespace perf
{
namespace
{
    constexpr int outputRate = 48000;
    constexpr int tapChannels = AudioEngine::programChannels;
    using Clock = std::chrono::steady_clock;
}

QSize frameSize (int height)
{
    switch (height)
    {
        case 720:  return { 1280, 720 };
        case 1440: return { 2560, 1440 };
        case 2160: return { 3840, 2160 };
        default:   return { 1920, 1080 };
    }
}

int resolvedHeight (const Settings& s)
{
    return s.height > 0 ? s.height : recommendedHeight();
}

//==============================================================================
// One output (the file or the stream): a queue and the thread that encodes and writes it.
struct Recorder::Runner
{
    struct Item
    {
        size_t track = 0;
        bool video = false;
        QImage image;
        std::shared_ptr<const Picture> picture;
        int64_t frame = 0;
        std::vector<float> samples;
        int frames = 0;
    };

    MediaWriter writer;
    std::thread thread;
    std::mutex lock;
    std::condition_variable wake;
    std::deque<Item> items;
    int videoQueued = 0;
    bool closing = false;
    std::atomic<bool> failed { false }, done { false }, reported { false };
    std::string error;                // guarded by lock
    std::atomic<int64_t> bytes { 0 };

    int64_t firstFrame = 0;
    std::vector<size_t> audioTracks;  // the session's track for each of this output's audio tracks
    std::vector<size_t> cameraTracks; // the camera for each video track after the first
    Clock::time_point began = Clock::now();
    int height = 1080;

    static constexpr int maxVideoQueued = 6;   // 4K pictures are 33 MB each

    bool pushVideo (size_t track, QImage image, std::shared_ptr<const Picture> picture, int64_t frame)
    {
        {
            std::lock_guard l (lock);
            if (closing || videoQueued >= maxVideoQueued)
                return false;
            ++videoQueued;
            items.push_back ({ track, true, std::move (image), std::move (picture), frame, {}, 0 });
        }
        wake.notify_one();
        return true;
    }

    void pushAudio (size_t track, std::vector<float> samples, int frames)
    {
        {
            std::lock_guard l (lock);
            if (closing)
                return;
            items.push_back ({ track, false, {}, {}, 0, std::move (samples), frames });
        }
        wake.notify_one();
    }

    void run()
    {
        for (;;)
        {
            Item item;
            {
                std::unique_lock l (lock);
                wake.wait (l, [this] { return closing || ! items.empty(); });
                if (items.empty())
                    break;
                item = std::move (items.front());
                items.pop_front();
                if (item.video)
                    --videoQueued;
            }
            if (failed)
                continue;

            bool ok = true;
            if (! item.video)
                ok = writer.writeAudio (item.track, item.samples.data(), item.frames);
            else if (item.picture != nullptr)
                ok = writer.writeVideo (item.track, item.picture->pixels.data(), item.picture->width, item.picture->height,
                                        item.picture->width * 4, item.frame);
            else
                ok = writer.writeVideo (item.track, item.image.constBits(), item.image.width(), item.image.height(),
                                        (int) item.image.bytesPerLine(), item.frame);
            bytes = writer.bytesWritten();
            if (! ok)
            {
                std::lock_guard l (lock);
                error = writer.error();
                failed = true;
            }
        }
        if (! writer.finish() && ! failed)
        {
            std::lock_guard l (lock);
            error = writer.error();
            failed = true;
        }
        done = true;
    }
};

//==============================================================================
// PHI's sound, from the device's rate to 48 kHz, nudged to keep time with the clock.
struct Recorder::Resampler
{
    SwrContext* swr = nullptr;
    int inRate = 0;

    explicit Resampler (int rate) : inRate (rate)
    {
        AVChannelLayout layout {};
        av_channel_layout_default (&layout, tapChannels);
        swr_alloc_set_opts2 (&swr, &layout, AV_SAMPLE_FMT_FLT, outputRate, &layout, AV_SAMPLE_FMT_FLT, rate, 0, nullptr);
        av_channel_layout_uninit (&layout);
        if (swr != nullptr)
        {
            av_opt_set_int (swr, "used_channel_count", tapChannels, 0);
            if (swr_init (swr) < 0)
                swr_free (&swr);
        }
    }

    ~Resampler() { swr_free (&swr); }

    std::vector<float> process (const std::vector<float>& in, int frames)
    {
        std::vector<float> out;
        if (swr == nullptr || frames <= 0)
            return out;
        const auto room = swr_get_out_samples (swr, frames) + 64;
        out.resize ((size_t) room * tapChannels);
        const uint8_t* src[1] { (const uint8_t*) in.data() };
        uint8_t* dst[1] { (uint8_t*) out.data() };
        const auto made = swr_convert (swr, dst, room, src, frames);
        out.resize ((size_t) std::max (made, 0) * tapChannels);
        return out;
    }

    // Spreads `samples` more (or fewer, when negative) over the next second.
    void compensate (int samples)
    {
        if (swr != nullptr)
            swr_set_compensation (swr, samples, outputRate);
    }
};

//==============================================================================
Recorder::Recorder (AudioEngine& e) : engine (e) {}

Recorder::~Recorder()
{
    stopStreaming();
    stopRecording();
}

bool Recorder::isRecording() const
{
    std::lock_guard l (outputsLock);
    return recording != nullptr;
}

bool Recorder::isStreaming() const
{
    std::lock_guard l (outputsLock);
    return streaming != nullptr;
}

std::vector<std::shared_ptr<Recorder::Runner>> Recorder::runners() const
{
    std::lock_guard l (outputsLock);
    std::vector<std::shared_ptr<Runner>> all;
    for (const auto& r : { recording, streaming })
        if (r != nullptr)
            all.push_back (r);
    return all;
}

int64_t Recorder::sessionFrame() const
{
    const auto elapsed = std::chrono::duration<double> (Clock::now() - started).count();
    return (int64_t) std::floor (elapsed * rate);
}

QSize Recorder::canvasSize() const
{
    return frameSize (canvasHeight.load());
}

std::string Recorder::startSession (const Settings& s)
{
    if (sessionRunning)
        return {};

    session = s;
    rate = std::clamp (s.fps, 10, 120);
    started = Clock::now();
    lastProgram = started;
    lastFrameAsked = -1;
    windowFrame = -1;
    samplesOut = 0;
    baseline = 0;
    lastCheck = 0.0;
    backlog.clear();
    foldFor = 0;
    dropped = 0;
    quitting = false;
    audioQuit = false;

    // The tracks: the mix first (what players and the stream play), then PHI alone in
    // its own channels, then each input alone.
    programChannels = std::clamp (engine.masterChannels(), 2, tapChannels);
    tracks.clear();
    tracks.push_back ({ "Mix", 2 });
    programTrack = -1;
    if (s.programTrack && (programChannels > 2 || ! s.inputs.empty()))
    {
        programTrack = (int) tracks.size();
        tracks.push_back ({ programChannels > 2 ? APP_DISPLAY_NAME " (" + std::to_string (programChannels) + " channels)" : std::string (APP_DISPLAY_NAME),
                            programChannels });
    }

    inputs.clear();
    inputTrack.clear();
    for (const auto& choice : s.inputs)
    {
        auto capture = std::make_unique<AudioCapture>();
        const auto channels = std::clamp (choice.source.channels, 1, 2);
        if (choice.source.kind == CaptureSource::Kind::application)
        {
            // An application (a web browser...) is found by its name, and only has sound while it
            // plays: the audio thread connects to it when it appears and again whenever it comes back.
            // Until then its track is silent (stereo, AudioCapture's default).
        }
        else if (auto error = capture->start (choice.source, outputRate, channels); ! error.empty())
        {
            inputs.clear();
            return "Cannot record " + choice.source.name + ": " + error;
        }
        inputTrack.push_back (choice.ownTrack ? (int) tracks.size() : -1);
        if (choice.ownTrack)
            tracks.push_back ({ choice.source.name, capture->numChannels() });
        inputs.push_back (std::move (capture));
    }

    finder = nullptr;
    lastLookup = {};
    if (std::any_of (s.inputs.begin(), s.inputs.end(), [] (const InputChoice& c) { return c.source.kind == CaptureSource::Kind::application; }))
    {
        finder = std::make_unique<AudioCapture>();
        finder->connect();
    }

    cameras.clear();
    for (const auto& c : s.cameras)
    {
        auto camera = std::make_unique<Camera>();
        camera->start (c.path, c.width, c.height, rate);
        cameras.push_back (std::move (camera));
    }

    resampler = std::make_unique<Resampler> ((int) std::lround (engine.deviceSampleRate() > 0 ? engine.deviceSampleRate() : outputRate));
    engine.startProgramCapture();
    sessionRunning = true;
    compositor = std::thread ([this] { compositorLoop(); });
    audio = std::thread ([this] { audioLoop(); });
    return {};
}

void Recorder::stopSessionIfIdle()
{
    if (! sessionRunning || isRunning())
        return;
    {
        std::lock_guard l (pictureLock);
        quitting = true;
    }
    pictureReady.notify_all();
    audioQuit = true;
    if (compositor.joinable())
        compositor.join();
    if (audio.joinable())
        audio.join();
    engine.stopProgramCapture();
    inputs.clear();
    finder = nullptr;
    cameras.clear();
    canvases.clear();
    window = {};
    resampler = nullptr;
    sessionRunning = false;
}

std::shared_ptr<Recorder::Runner> Recorder::startRunner (const Output& out, std::vector<size_t> audioTracks, size_t, std::string& error)
{
    auto runner = std::make_shared<Runner>();
    if (! runner->writer.open (out))
    {
        error = runner->writer.error();
        return nullptr;
    }
    runner->audioTracks = std::move (audioTracks);
    runner->firstFrame = sessionFrame() + 1;
    runner->began = Clock::now();
    runner->height = out.video.empty() ? 1080 : out.video.front().height;
    runner->thread = std::thread ([r = runner.get()] { r->run(); });
    return runner;
}

void Recorder::finishRunner (std::shared_ptr<Runner>& runner, bool network)
{
    if (runner == nullptr)
        return;
    {
        std::lock_guard l (runner->lock);
        runner->closing = true;
    }
    runner->wake.notify_all();
    // A stream whose connection is stuck gives up after a few seconds rather than holding PHI.
    // A file always finishes, or it could not be played.
    for (int waited = 0; network && ! runner->done && waited < 500; ++waited)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    if (network && ! runner->done)
        runner->writer.abort();
    if (runner->thread.joinable())
        runner->thread.join();
}

std::string Recorder::startRecording (const Settings& s)
{
    if (isRecording())
        return {};

    const auto folder = QString::fromStdString (s.folder);
    if (! QDir().mkpath (folder))
        return "Cannot create the folder " + s.folder;
    const auto name = QStringLiteral (APP_DISPLAY_NAME " %1.mp4").arg (QDateTime::currentDateTime().toString (QStringLiteral ("yyyy-MM-dd HH-mm-ss")));
    const auto path = QDir (folder).filePath (name).toStdString();

    if (auto error = startSession (s); ! error.empty())
        return error;

    Output out;
    out.target = path;
    out.container = "mp4";
    out.fragmented = s.fragmented;
    out.sampleRate = outputRate;

    VideoTrack main;
    const auto size = frameSize (resolvedHeight (s));
    main.width = size.width();
    main.height = size.height();
    main.fps = rate;
    main.codec = Codec::hevc;
    main.encoder = s.encoder;
    main.bitrate = (int64_t) (defaultBitrate (main.width, main.height, rate, Codec::hevc) * std::clamp (s.quality, 0.3, 4.0));
    main.title = APP_DISPLAY_NAME;
    out.video.push_back (main);

    std::vector<size_t> cameraTracks;
    for (size_t i = 0; i < session.cameras.size(); ++i)
        if (session.cameras[i].ownTrack)
        {
            VideoTrack cam = main;
            cam.width = session.cameras[i].width & ~1;
            cam.height = session.cameras[i].height & ~1;
            cam.bitrate = defaultBitrate (cam.width, cam.height, rate, Codec::hevc);
            cam.title = session.cameras[i].name;
            out.video.push_back (cam);
            cameraTracks.push_back (i);
        }

    std::vector<size_t> audioTracks;
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        AudioTrack a;
        a.channels = tracks[i].channels;
        a.bitrate = (int64_t) s.audioKbps * 1000 * std::max (1, a.channels) / 2;
        a.bitrate = std::max<int64_t> (a.bitrate, 64000);
        a.title = tracks[i].title;
        out.audio.push_back (a);
        audioTracks.push_back (i);
    }

    std::string error;
    auto runner = startRunner (out, std::move (audioTracks), cameraTracks.size(), error);
    if (runner == nullptr)
    {
        stopSessionIfIdle();
        return error;
    }
    runner->cameraTracks = std::move (cameraTracks);
    {
        std::lock_guard l (outputsLock);
        recording = runner;
        recordPath = path;
        recordHeight = main.height;
    }
    canvasHeight = std::max (recordHeight, isStreaming() ? streamHeight : 0);
    return {};
}

std::string Recorder::stopRecording()
{
    std::shared_ptr<Runner> runner;
    std::string path;
    {
        std::lock_guard l (outputsLock);
        runner = std::move (recording);
        recording = nullptr;
        path = recordPath;
    }
    if (runner == nullptr)
        return {};
    finishRunner (runner, false);
    if (isStreaming())
        canvasHeight = streamHeight;
    stopSessionIfIdle();
    return runner->writer.videoFrames (0) > 0 ? path : std::string();
}

std::string Recorder::startStreaming (const Settings& s)
{
    if (isStreaming())
        return {};
    if (s.streamServer.empty())
        return "Choose a streaming service or enter a server address first";

    auto url = s.streamServer;
    if (! s.streamKey.empty())
        url += (url.back() == '/' ? "" : "/") + s.streamKey;
    const bool srt = url.rfind ("srt://", 0) == 0 || url.rfind ("udp://", 0) == 0;
    const auto scheme = url.substr (0, url.find ("://"));
    if (! hasProtocol (scheme))
        return "This FFmpeg cannot stream over " + scheme;
    if (! canStream (s.streamCodec) && ! srt)
        return "This FFmpeg is too old to stream H.265 over RTMP (it needs FFmpeg 6.1 or later); choose H.264";

    if (auto error = startSession (s); ! error.empty())
        return error;

    Output out;
    out.target = url;
    out.container = srt ? "mpegts" : "flv";
    out.sampleRate = outputRate;
    VideoTrack v;
    const auto size = frameSize (s.streamHeight);
    v.width = size.width();
    v.height = size.height();
    v.fps = rate;
    v.codec = s.streamCodec;
    v.bitrate = (int64_t) s.streamKbps * 1000;
    v.constantRate = true;
    out.video.push_back (v);
    AudioTrack a;
    a.channels = 2;
    a.bitrate = (int64_t) s.streamAudioKbps * 1000;
    out.audio.push_back (a);

    std::string error;
    auto runner = startRunner (out, { 0 }, 0, error);
    if (runner == nullptr)
    {
        stopSessionIfIdle();
        return error;
    }
    {
        std::lock_guard l (outputsLock);
        streaming = runner;
        streamHeight = v.height;
    }
    canvasHeight = std::max (streamHeight, isRecording() ? recordHeight : 0);
    return {};
}

void Recorder::stopStreaming()
{
    std::shared_ptr<Runner> runner;
    {
        std::lock_guard l (outputsLock);
        runner = std::move (streaming);
        streaming = nullptr;
    }
    if (runner == nullptr)
        return;
    finishRunner (runner, true);
    if (isRecording())
        canvasHeight = recordHeight;
    stopSessionIfIdle();
}

//==============================================================================
bool Recorder::wantsPicture()
{
    if (! sessionRunning)
        return false;
    const auto frame = sessionFrame();
    if (frame <= lastFrameAsked)
        return false;
    if (lastFrameAsked >= 0 && frame > lastFrameAsked + 1)
        dropped += frame - lastFrameAsked - 1;   // the window was busy (or the computer was)
    lastFrameAsked = frame;
    return true;
}

void Recorder::submitWindow (QImage image)
{
    {
        std::lock_guard l (pictureLock);
        if (windowFrame >= 0)
            ++dropped;                           // the compositor has not taken the last one yet
        window = std::move (image);
        windowFrame = lastFrameAsked;
    }
    pictureReady.notify_one();
}

void Recorder::compositorLoop()
{
    for (;;)
    {
        QImage picture;
        int64_t frame = 0;
        {
            std::unique_lock l (pictureLock);
            pictureReady.wait (l, [this] { return quitting || windowFrame >= 0; });
            if (quitting)
                return;
            picture = std::move (window);
            window = {};
            frame = windowFrame;
            windowFrame = -1;
        }

        const auto outputs = runners();
        if (outputs.empty() || picture.isNull())
            continue;

        // A canvas the outputs have finished with, else a new one. It is taken out of the
        // pool while painted, so painting does not copy it.
        const auto size = canvasSize();
        QImage canvas;
        size_t slot = canvases.size();
        for (size_t i = 0; i < canvases.size() && slot == canvases.size(); ++i)
            if (canvases[i].size() == size && canvases[i].isDetached())
            {
                canvas = std::move (canvases[i]);
                slot = i;
            }
        if (canvas.isNull())
        {
            canvases.erase (std::remove_if (canvases.begin(), canvases.end(), [&] (const QImage& c) { return ! c.isNull() && c.size() != size; }),
                            canvases.end());
            canvas = QImage (size, QImage::Format_RGB32);
            slot = canvases.size() < 12 ? canvases.size() : (size_t) -1;
            if (slot != (size_t) -1)
                canvases.emplace_back();
        }
        picture.setDevicePixelRatio (1.0);   // drawn by its pixels

        {
            QPainter p (&canvas);
            p.setRenderHint (QPainter::SmoothPixmapTransform);
            const auto fitted = picture.size().scaled (size, Qt::KeepAspectRatio);
            const QRect target (QPoint ((size.width() - fitted.width()) / 2, (size.height() - fitted.height()) / 2), fitted);
            if (target.size() != size)
                p.fillRect (canvas.rect(), Qt::black);
            if (fitted == picture.size())
                p.drawImage (target.topLeft(), picture);
            else
                p.drawImage (target, picture);

            // Cameras in their corners, stacked when several share one.
            const auto margin = size.height() / 40;
            int stacked[4] {};
            for (size_t i = 0; i < cameras.size() && i < session.cameras.size(); ++i)
            {
                const auto& choice = session.cameras[i];
                if (! choice.shown)
                    continue;
                const auto pic = cameras[i]->latest();
                if (pic == nullptr || pic->width <= 0)
                    continue;
                const QImage image (pic->pixels.data(), pic->width, pic->height, pic->width * 4, QImage::Format_RGB32);
                const auto w = size.width() * std::clamp (choice.sizePercent, 5, 100) / 100;
                const auto h = w * pic->height / pic->width;
                const auto corner = std::clamp (choice.corner, 0, 3);
                const bool right = corner == CameraChoice::topRight || corner == CameraChoice::bottomRight;
                const bool bottom = corner == CameraChoice::bottomLeft || corner == CameraChoice::bottomRight;
                const auto x = right ? size.width() - margin - w : margin;
                const auto y = bottom ? size.height() - margin - h - stacked[corner] : margin + stacked[corner];
                stacked[corner] += h + margin;
                const QRect box (x, y, w, h);
                p.drawImage (box, image);
                p.setPen (QPen (QColor (255, 255, 255, 160), std::max (1, size.height() / 540)));
                p.drawRect (box.adjusted (0, 0, -1, -1));
            }
        }
        if (slot < canvases.size())
            canvases[slot] = canvas;

        for (const auto& r : outputs)
        {
            if (frame < r->firstFrame)
                continue;
            if (! r->pushVideo (0, canvas, nullptr, frame - r->firstFrame))
                ++dropped;
            for (size_t k = 0; k < r->cameraTracks.size(); ++k)
                if (const auto i = r->cameraTracks[k]; i < cameras.size())
                    if (auto pic = cameras[i]->latest())
                        r->pushVideo (k + 1, {}, std::move (pic), frame - r->firstFrame);
        }
    }
}

//==============================================================================
void Recorder::audioLoop()
{
    while (! audioQuit)
    {
        pumpAudio();
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
}

void Recorder::pumpAudio()
{
    std::vector<float> raw;
    int heard = 2;
    const auto frames = engine.takeProgram (raw, heard);
    const auto now = Clock::now();
    if (frames > 0)
        lastProgram = now;

    if (const auto deviceRate = (int) std::lround (engine.deviceSampleRate()); deviceRate > 0 && deviceRate != resampler->inRate)
        resampler = std::make_unique<Resampler> (deviceRate);   // the output device changed
    auto program = resampler->process (raw, frames);
    auto made = (int) (program.size() / tapChannels);

    // Where the clock says the sound should be. Once settled (2 s in), the device's clock
    // is pulled towards it a little at a time, so the sound stays with the picture for hours.
    const auto elapsed = std::chrono::duration<double> (now - started).count();
    const auto expected = (int64_t) (elapsed * outputRate);
    const bool silentDevice = std::chrono::duration<double> (now - lastProgram).count() > 0.25;
    if (silentDevice)
    {
        // No output device, or it is changing: silence keeps time.
        const auto missing = std::max<int64_t> (0, expected - baseline - samplesOut - made);
        program.resize (program.size() + (size_t) missing * tapChannels, 0.0f);
        made += (int) missing;
    }
    else if (elapsed > 2.0 && baseline == 0)
        baseline = std::max<int64_t> (1, expected - samplesOut);
    else if (baseline != 0 && elapsed - lastCheck > 1.0)
    {
        lastCheck = elapsed;
        const auto drift = expected - baseline - samplesOut;
        if (std::abs (drift) > outputRate / 2)
        {
            if (drift > 0)
            {
                program.resize (program.size() + (size_t) drift * tapChannels, 0.0f);   // a gap: fill it
                made += (int) drift;
            }
        }
        else if (std::abs (drift) > outputRate / 200)
            resampler->compensate ((int) std::clamp<int64_t> (drift, -outputRate / 1000, outputRate / 1000));
    }
    if (made <= 0)
        return;

    // Applications come and go (a browser has a stream only while it plays): look for them again each second.
    if (finder != nullptr && now - lastLookup > std::chrono::seconds (1))
    {
        lastLookup = now;
        std::vector<CaptureSource> playing;
        for (size_t i = 0; i < inputs.size() && i < session.inputs.size(); ++i)
        {
            const auto& wanted = session.inputs[i].source;
            if (wanted.kind != CaptureSource::Kind::application || (inputs[i]->isRunning() && inputs[i]->streamError().empty()))
                continue;
            if (playing.empty())
                playing = finder->sources();
            const auto found = std::find_if (playing.begin(), playing.end(), [&] (const CaptureSource& c)
                                             { return c.kind == CaptureSource::Kind::application && c.name == wanted.name; });
            if (found != playing.end())
                inputs[i]->start (*found, outputRate, 2);
            else if (inputs[i]->isRunning())
                inputs[i]->stop();
        }
    }

    // Each input gives as many frames as PHI did, padded or trimmed to keep in step.
    std::vector<std::vector<float>> inputAudio (inputs.size());
    backlog.resize (inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        auto& capture = *inputs[i];
        const auto ch = capture.numChannels();
        auto& b = backlog[i];
        std::vector<float> buffer (4096 * (size_t) ch);
        for (int n; (n = capture.read (buffer.data(), 4096)) > 0;)
            b.insert (b.end(), buffer.begin(), buffer.begin() + (ptrdiff_t) n * ch);
        const auto available = (int) (b.size() / (size_t) ch);
        if (available > made + outputRate / 5)
            b.erase (b.begin(), b.begin() + (ptrdiff_t) (available - made - outputRate / 50) * ch);
        auto& out = inputAudio[i];
        out.assign ((size_t) made * (size_t) ch, 0.0f);
        const auto take = std::min ((size_t) made * (size_t) ch, b.size());
        std::copy (b.begin(), b.begin() + (ptrdiff_t) take, out.end() - (ptrdiff_t) take);   // late input: silence first
        b.erase (b.begin(), b.begin() + (ptrdiff_t) take);
    }

    // The mix: PHI folded to stereo, then the inputs.
    heard = std::clamp (heard, 2, tapChannels);
    if (foldFor != heard)
    {
        fold = dsp::stereoDownmix (heard);
        foldFor = heard;
    }
    std::vector<std::vector<float>> trackAudio (tracks.size());
    auto& mix = trackAudio[0];
    mix.assign ((size_t) made * 2, 0.0f);
    for (int f = 0; f < made; ++f)
        for (int side = 0; side < 2; ++side)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < heard && ch < (int) fold[(size_t) side].size(); ++ch)
                sum += fold[(size_t) side][(size_t) ch] * program[(size_t) f * tapChannels + (size_t) ch];
            mix[(size_t) f * 2 + (size_t) side] = sum;
        }
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        const auto ch = inputs[i]->numChannels();
        const auto gain = (float) std::pow (10.0, (i < session.inputs.size() ? session.inputs[i].gainDb : 0.0) / 20.0);
        const auto& in = inputAudio[i];
        for (int f = 0; f < made; ++f)
        {
            const auto l = in[(size_t) f * (size_t) ch];
            const auto r = ch > 1 ? in[(size_t) f * (size_t) ch + 1] : l;
            const auto scale = ch > 1 ? gain : gain * 0.70710678f;
            mix[(size_t) f * 2] += l * scale;
            mix[(size_t) f * 2 + 1] += r * scale;
        }
        if (inputTrack[i] >= 0)
            trackAudio[(size_t) inputTrack[i]] = in;
    }
    if (programTrack >= 0)
    {
        auto& own = trackAudio[(size_t) programTrack];
        own.resize ((size_t) made * (size_t) programChannels);
        for (int f = 0; f < made; ++f)
            for (int ch = 0; ch < programChannels; ++ch)
                own[(size_t) f * (size_t) programChannels + (size_t) ch] = program[(size_t) f * tapChannels + (size_t) ch];
    }
    samplesOut += made;

    for (const auto& r : runners())
        for (size_t k = 0; k < r->audioTracks.size(); ++k)
            if (const auto t = r->audioTracks[k]; t < trackAudio.size())
                r->pushAudio (k, trackAudio[t], made);
}

//==============================================================================
Status Recorder::status() const
{
    Status s;
    const auto now = Clock::now();
    std::lock_guard l (outputsLock);
    if (recording != nullptr)
    {
        s.recording = true;
        s.recordSeconds = std::chrono::duration<double> (now - recording->began).count();
        s.recordBytes = recording->bytes.load();
        s.recordEncoder = recording->writer.encoderName (0);
        s.recordPath = recordPath;
    }
    if (streaming != nullptr)
    {
        s.streaming = true;
        s.streamSeconds = std::chrono::duration<double> (now - streaming->began).count();
        s.streamEncoder = streaming->writer.encoderName (0);
    }
    s.droppedFrames = dropped.load();
    return s;
}

std::string Recorder::takeProblem()
{
    for (const auto& r : runners())
        if (r->failed && ! r->reported.exchange (true))
        {
            std::lock_guard l (r->lock);
            const bool stream = [&] { std::lock_guard o (outputsLock); return r == streaming; }();
            return (stream ? "The stream stopped: " : "The recording stopped: ") + r->error;
        }
    return {};
}
}
