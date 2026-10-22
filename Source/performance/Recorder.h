// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioCapture.h"
#include "MediaWriter.h"

#include <QImage>
#include <QSize>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AudioEngine;

// The Performance menu's video recorder and live stream, OBS-style but for PHINYX only:
// the picture is PHINYX's own window (rendered by PHINYX, so no other window or screen
// is ever in it, and no screen-sharing prompt on Wayland), with cameras on top, and the
// sound is what PHINYX plays plus any microphones. One picture and one sound feed both
// the recording (MP4, H.265) and the stream; either can start and stop on its own.
//
// Threads: the UI thread renders the window (submitWindow); a compositor thread puts it
// and the cameras on the canvas; an audio thread mixes the tracks; each output encodes
// and writes on a thread of its own, so a slow network never holds up the recording.
namespace perf
{
    struct InputChoice
    {
        CaptureSource source;          // an input (microphone), or an application such as a web browser (found by its name)
        double gainDb = 0.0;
        bool ownTrack = true;          // also on a track of its own in the recording
    };

    struct CameraChoice
    {
        std::string path, name;
        int width = 1280, height = 720;
        enum Corner { topLeft, topRight, bottomLeft, bottomRight };
        int corner = bottomRight;
        int sizePercent = 25;          // of the picture's width
        bool shown = true;             // on the picture
        bool ownTrack = false;         // also a video track of its own in the recording (for multicam editing)
    };

    struct Settings
    {
        // Recording
        int height = 0;                // 1080, 1440 or 2160; 0: recommendedHeight
        int fps = 30;
        std::string encoder;           // empty: the best HEVC encoder
        double quality = 1.0;          // times defaultBitrate
        int audioKbps = 256;           // AAC, per stereo pair
        bool programTrack = true;      // PHINYX's sound in its own channels (5.1, 7.1...) on a track of its own
        bool fragmented = false;
        std::string folder;
        bool followWindow = true;      // the PHINYX window in front (Mixer, Tracker...), else always the main window
        // Shared
        std::vector<InputChoice> inputs;
        std::vector<CameraChoice> cameras;
        // Streaming
        std::string streamServer, streamKey;
        int streamHeight = 1080;
        int streamKbps = 6000;
        Codec streamCodec = Codec::h264;
        int streamAudioKbps = 160;
    };

    // The 16:9 size for 1080, 1440 or 2160 lines (720 for streams too).
    QSize frameSize (int height);
    int resolvedHeight (const Settings&);

    struct Status
    {
        bool recording = false, streaming = false;
        double recordSeconds = 0.0, streamSeconds = 0.0;
        int64_t recordBytes = 0;
        int64_t droppedFrames = 0;     // pictures that came too late or found the encoders busy
        std::string recordEncoder, streamEncoder;
        std::string recordPath;
    };

    class Recorder
    {
    public:
        explicit Recorder (AudioEngine&);
        ~Recorder();

        // Starts writing a new MP4 in settings.folder. Returns an error, or empty.
        std::string startRecording (const Settings&);
        // Stops and closes the file; returns its path (empty if nothing was written).
        std::string stopRecording();
        std::string startStreaming (const Settings&);
        void stopStreaming();
        bool isRecording() const;
        bool isStreaming() const;
        bool isRunning() const { return isRecording() || isStreaming(); }

        // UI thread. True when it is time for the next picture: render the window at
        // canvasSize() (fitted, keeping its shape) and hand it over with submitWindow.
        bool wantsPicture();
        QSize canvasSize() const;
        int fps() const { return rate; }
        void submitWindow (QImage);

        Status status() const;
        // An output that stopped by itself (a stream that lost its connection, a full disk),
        // once each; empty when none did.
        std::string takeProblem();

    private:
        struct Runner;
        struct Track { std::string title; int channels = 2; };

        std::string startSession (const Settings&);
        void stopSessionIfIdle();
        std::shared_ptr<Runner> startRunner (const Output&, std::vector<size_t> audioTracks, size_t cameraTracks, std::string& error);
        void finishRunner (std::shared_ptr<Runner>&, bool network);
        void compositorLoop();
        void audioLoop();
        void pumpAudio();
        int64_t sessionFrame() const;
        std::vector<std::shared_ptr<Runner>> runners() const;

        AudioEngine& engine;
        Settings session;
        int rate = 30;
        bool sessionRunning = false;
        std::chrono::steady_clock::time_point started;
        int64_t lastFrameAsked = -1;

        mutable std::mutex outputsLock;
        std::shared_ptr<Runner> recording, streaming;
        std::string recordPath;
        int recordHeight = 1080, streamHeight = 1080;
        std::atomic<int> canvasHeight { 1080 };

        // Compositor
        std::thread compositor;
        std::mutex pictureLock;
        std::condition_variable pictureReady;
        QImage window;
        int64_t windowFrame = -1;
        bool quitting = false;
        std::vector<QImage> canvases;   // reused once the outputs let go of them
        std::atomic<int64_t> dropped { 0 };

        // Sound
        std::thread audio;
        std::atomic<bool> audioQuit { false };
        std::vector<Track> tracks;      // 0: the mix; then PHINYX's own channels; then each input
        std::vector<std::unique_ptr<AudioCapture>> inputs;
        std::vector<int> inputTrack;    // track of each input, or -1
        std::unique_ptr<AudioCapture> finder;   // follows PipeWire's applications, when one is recorded
        std::chrono::steady_clock::time_point lastLookup;
        int programChannels = 2;
        int programTrack = -1;
        struct Resampler;
        std::unique_ptr<Resampler> resampler;
        int64_t samplesOut = 0;         // at 48 kHz since the session started
        std::chrono::steady_clock::time_point lastProgram;
        int64_t baseline = 0;           // how far the sound runs behind the clock once settled (the device's buffering)
        double lastCheck = 0.0;
        std::vector<std::vector<float>> backlog;   // each input's frames not used yet
        std::array<std::vector<float>, 2> fold;    // PHINYX's channels to the stereo mix
        int foldFor = 0;

        // Cameras
        std::vector<std::unique_ptr<class Camera>> cameras;

        mutable std::mutex problemLock;
        std::string problem;
    };
}
