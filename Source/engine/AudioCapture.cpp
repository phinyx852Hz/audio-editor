// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioCapture.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#if PHINYX_PIPEWIRE

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <unistd.h>

#include <map>

namespace
{
    std::string prop (const spa_dict* props, const char* key)
    {
        const char* v = props != nullptr ? spa_dict_lookup (props, key) : nullptr;
        return v != nullptr ? std::string (v) : std::string();
    }

    void setPositions (spa_audio_info_raw& info)
    {
        static const uint32_t surround[8] { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                            SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR, SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR };
        const auto n = info.channels;

        if (n == 1)
            info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        else if (n == 4)
        {
            const uint32_t quad[4] { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR };
            std::copy (quad, quad + 4, info.position);
        }
        else if (n == 2 || n == 6 || n == 8)
            std::copy (surround, surround + n, info.position);
        else
            for (uint32_t i = 0; i < n; ++i)
                info.position[i] = SPA_AUDIO_CHANNEL_AUX0 + i;
    }
}

struct AudioCapture::Impl
{
    struct Node
    {
        std::string mediaClass, name, description, serial, app, media;
        int channels = 0;
    };

    AudioCapture& owner;
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook registryListener {};
    pw_stream* stream = nullptr;
    spa_hook streamListener {};

    mutable std::mutex lock;          // guards nodes and error
    std::map<uint32_t, Node> nodes;
    std::string error;

    // Single-producer (PipeWire's thread), single-consumer (read()) ring of interleaved samples.
    std::vector<float> ring;
    std::atomic<uint64_t> written { 0 }, taken { 0 };
    std::atomic<bool> running { false };

    explicit Impl (AudioCapture& o) : owner (o) {}

    ~Impl()
    {
        if (loop == nullptr)
            return;

        pw_thread_loop_lock (loop);
        destroyStream();
        if (registry != nullptr)
            pw_proxy_destroy ((pw_proxy*) registry);
        if (core != nullptr)
            pw_core_disconnect (core);
        pw_thread_loop_unlock (loop);
        pw_thread_loop_stop (loop);
        if (context != nullptr)
            pw_context_destroy (context);
        pw_thread_loop_destroy (loop);
    }

    void destroyStream()
    {
        if (stream == nullptr)
            return;

        spa_hook_remove (&streamListener);
        pw_stream_destroy (stream);
        stream = nullptr;
    }

    static void onGlobal (void* data, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props)
    {
        auto& self = *static_cast<Impl*> (data);
        if (std::strcmp (type, PW_TYPE_INTERFACE_Node) != 0)
            return;

        Node n;
        n.mediaClass = prop (props, PW_KEY_MEDIA_CLASS);
        const bool wanted = n.mediaClass == "Audio/Sink" || n.mediaClass == "Audio/Source"
                         || n.mediaClass == "Audio/Source/Virtual" || n.mediaClass == "Audio/Duplex"
                         || n.mediaClass == "Stream/Output/Audio";
        if (! wanted)
            return;

        // Our own playback is not a source to record.
        if (prop (props, PW_KEY_APP_PROCESS_ID) == std::to_string (getpid()))
            return;

        n.name = prop (props, PW_KEY_NODE_NAME);
        n.description = prop (props, PW_KEY_NODE_DESCRIPTION);
        n.serial = prop (props, PW_KEY_OBJECT_SERIAL);
        n.app = prop (props, PW_KEY_APP_NAME);
        n.media = prop (props, PW_KEY_MEDIA_NAME);
        n.channels = std::atoi (prop (props, PW_KEY_AUDIO_CHANNELS).c_str());

        {
            const std::lock_guard<std::mutex> l (self.lock);
            self.nodes[id] = std::move (n);
        }
        ++self.owner.version;
    }

    static void onGlobalRemove (void* data, uint32_t id)
    {
        auto& self = *static_cast<Impl*> (data);
        bool removed;
        {
            const std::lock_guard<std::mutex> l (self.lock);
            removed = self.nodes.erase (id) > 0;
        }
        if (removed)
            ++self.owner.version;
    }

    static void onStateChanged (void* data, pw_stream_state, pw_stream_state state, const char* message)
    {
        auto& self = *static_cast<Impl*> (data);
        const std::lock_guard<std::mutex> l (self.lock);

        if (state == PW_STREAM_STATE_ERROR)
            self.error = message != nullptr ? message : "PipeWire stopped the recording";
        else if (state == PW_STREAM_STATE_UNCONNECTED && self.running.load())
            self.error = "The source went away";
    }

    static void onProcess (void* data)
    {
        auto& self = *static_cast<Impl*> (data);
        auto* b = pw_stream_dequeue_buffer (self.stream);
        if (b == nullptr)
            return;

        const auto& d = b->buffer->datas[0];
        if (d.data != nullptr && d.chunk != nullptr && self.running.load (std::memory_order_relaxed))
        {
            const auto channels = (uint64_t) self.owner.channels;
            const auto offset = std::min<uint32_t> (d.chunk->offset, d.maxsize);
            const auto bytes = std::min<uint32_t> (d.chunk->size, d.maxsize - offset);
            const auto* src = reinterpret_cast<const float*> (static_cast<const char*> (d.data) + offset);
            const uint64_t frames = bytes / (sizeof (float) * channels);

            const auto w = self.written.load (std::memory_order_relaxed);
            const auto r = self.taken.load (std::memory_order_acquire);
            const uint64_t space = (self.ring.size() - (w - r)) / channels;
            const auto n = std::min (frames, space);
            const auto size = (uint64_t) self.ring.size();

            for (uint64_t i = 0; i < n * channels; ++i)
                self.ring[(size_t) ((w + i) % size)] = src[i];

            self.written.store (w + n * channels, std::memory_order_release);
            if (n < frames)
                self.owner.dropped += frames - n;
        }

        pw_stream_queue_buffer (self.stream, b);
    }
};

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture()
{
    stop();
}

bool AudioCapture::isAvailable()
{
    return true;
}

bool AudioCapture::isConnected() const
{
    return impl != nullptr && impl->core != nullptr;
}

std::string AudioCapture::connect()
{
    if (isConnected())
        return {};

    static std::once_flag initialised;
    std::call_once (initialised, [] { pw_init (nullptr, nullptr); });

    auto i = std::make_unique<Impl> (*this);
    i->loop = pw_thread_loop_new ("phinyx-capture", nullptr);
    if (i->loop == nullptr)
        return "Could not start PipeWire's thread";

    i->context = pw_context_new (pw_thread_loop_get_loop (i->loop), nullptr, 0);
    if (i->context == nullptr || pw_thread_loop_start (i->loop) != 0)
        return "Could not start PipeWire";

    pw_thread_loop_lock (i->loop);
    i->core = pw_context_connect (i->context, nullptr, 0);
    if (i->core == nullptr)
    {
        pw_thread_loop_unlock (i->loop);
        return "PipeWire is not running, so there is nothing to record from";
    }

    static pw_registry_events events = []
    {
        pw_registry_events e {};
        e.version = PW_VERSION_REGISTRY_EVENTS;
        e.global = Impl::onGlobal;
        e.global_remove = Impl::onGlobalRemove;
        return e;
    }();

    i->registry = pw_core_get_registry (i->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener (i->registry, &i->registryListener, &events, i.get());
    pw_thread_loop_unlock (i->loop);

    impl = std::move (i);
    return {};
}

std::vector<CaptureSource> AudioCapture::sources() const
{
    std::vector<CaptureSource> result;
    result.push_back ({ CaptureSource::Kind::desktop, {}, "Desktop audio (default output)", "Everything you hear", 0 });

    if (impl == nullptr)
        return result;

    std::vector<CaptureSource> outputs, inputs, apps;
    {
        const std::lock_guard<std::mutex> l (impl->lock);
        for (const auto& [id, n] : impl->nodes)
        {
            const auto label = n.description.empty() ? n.name : n.description;
            if (n.mediaClass == "Audio/Sink")
                outputs.push_back ({ CaptureSource::Kind::desktop, n.name, label, "Everything played on this output", n.channels });
            else if (n.mediaClass == "Stream/Output/Audio")
                apps.push_back ({ CaptureSource::Kind::application, n.serial, n.app.empty() ? label : n.app, n.media, n.channels });
            else
                inputs.push_back ({ CaptureSource::Kind::input, n.name, label, {}, n.channels });
        }
    }

    const auto byName = [] (const CaptureSource& a, const CaptureSource& b) { return a.name < b.name; };
    for (auto* list : { &outputs, &inputs, &apps })
    {
        std::stable_sort (list->begin(), list->end(), byName);
        result.insert (result.end(), list->begin(), list->end());
    }
    return result;
}

std::string AudioCapture::start (const CaptureSource& source, int sampleRate, int numChannels)
{
    stop();
    if (auto e = connect(); ! e.empty())
        return e;

    rate = std::clamp (sampleRate, 8000, 384000);
    channels = std::clamp (numChannels, 1, 32);
    dropped = 0;

    impl->ring.assign ((size_t) rate * (size_t) channels * 30, 0.0f);
    impl->written = 0;
    impl->taken = 0;
    {
        const std::lock_guard<std::mutex> l (impl->lock);
        impl->error.clear();
    }

    auto* props = pw_properties_new (PW_KEY_MEDIA_TYPE, "Audio",
                                     PW_KEY_MEDIA_CATEGORY, "Capture",
                                     PW_KEY_MEDIA_ROLE, "Production",
                                     PW_KEY_NODE_NAME, APP_ID "-recorder",
                                     PW_KEY_NODE_DESCRIPTION, APP_DISPLAY_NAME " recorder",
                                     PW_KEY_APP_NAME, APP_DISPLAY_NAME,
                                     nullptr);
    if (source.kind == CaptureSource::Kind::desktop)
        pw_properties_set (props, PW_KEY_STREAM_CAPTURE_SINK, "true");
    if (! source.target.empty())
    {
        pw_properties_set (props, PW_KEY_TARGET_OBJECT, source.target.c_str());
        // A chosen device or app that goes away must not silently become the microphone.
        pw_properties_set (props, PW_KEY_NODE_DONT_RECONNECT, "true");
    }

    static pw_stream_events events = []
    {
        pw_stream_events e {};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.state_changed = Impl::onStateChanged;
        e.process = Impl::onProcess;
        return e;
    }();

    spa_audio_info_raw info {};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = (uint32_t) rate;
    info.channels = (uint32_t) channels;
    setPositions (info);

    uint8_t buffer[1024];
    spa_pod_builder b {};
    spa_pod_builder_init (&b, buffer, sizeof (buffer));
    const spa_pod* params[1] { spa_format_audio_raw_build (&b, SPA_PARAM_EnumFormat, &info) };

    pw_thread_loop_lock (impl->loop);
    impl->stream = pw_stream_new (impl->core, APP_DISPLAY_NAME " recorder", props);
    int result = -1;
    if (impl->stream != nullptr)
    {
        pw_stream_add_listener (impl->stream, &impl->streamListener, &events, impl.get());
        impl->running = true;
        result = pw_stream_connect (impl->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                    (pw_stream_flags) (PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                                    params, 1);
        if (result < 0)
        {
            impl->running = false;
            impl->destroyStream();
        }
    }
    pw_thread_loop_unlock (impl->loop);

    return result < 0 ? std::string ("PipeWire could not open that source") : std::string();
}

void AudioCapture::stop()
{
    if (impl == nullptr || impl->stream == nullptr)
        return;

    impl->running = false;
    pw_thread_loop_lock (impl->loop);
    impl->destroyStream();
    pw_thread_loop_unlock (impl->loop);
}

bool AudioCapture::isRunning() const
{
    return impl != nullptr && impl->running.load();
}

int AudioCapture::read (float* interleaved, int maxFrames)
{
    if (impl == nullptr || impl->ring.empty())
        return 0;

    const auto ch = (uint64_t) channels;
    const auto r = impl->taken.load (std::memory_order_relaxed);
    const auto w = impl->written.load (std::memory_order_acquire);
    const auto n = std::min<uint64_t> ((w - r) / ch, (uint64_t) std::max (0, maxFrames));
    const auto size = (uint64_t) impl->ring.size();

    for (uint64_t i = 0; i < n * ch; ++i)
        interleaved[i] = impl->ring[(size_t) ((r + i) % size)];

    impl->taken.store (r + n * ch, std::memory_order_release);
    return (int) n;
}

std::string AudioCapture::streamError() const
{
    if (impl == nullptr)
        return {};

    const std::lock_guard<std::mutex> l (impl->lock);
    return impl->error;
}

#else  // No PipeWire at build time: recording is unavailable.

struct AudioCapture::Impl {};

AudioCapture::AudioCapture() = default;
AudioCapture::~AudioCapture() = default;
bool AudioCapture::isAvailable() { return false; }
std::string AudioCapture::connect() { return "This build of " APP_DISPLAY_NAME " was made without PipeWire, so it cannot record"; }
bool AudioCapture::isConnected() const { return false; }
std::vector<CaptureSource> AudioCapture::sources() const { return {}; }
std::string AudioCapture::start (const CaptureSource&, int, int) { return connect(); }
void AudioCapture::stop() {}
bool AudioCapture::isRunning() const { return false; }
int AudioCapture::read (float*, int) { return 0; }
std::string AudioCapture::streamError() const { return {}; }

#endif
