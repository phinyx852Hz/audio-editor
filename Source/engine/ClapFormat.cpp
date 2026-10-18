// SPDX-License-Identifier: AGPL-3.0-only
#include "ClapFormat.h"
#include "X11Windows.h"

#include <clap/clap.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cstring>
#include <map>
#include <poll.h>
#include <unordered_map>

namespace
{
    bool onMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm != nullptr && mm->isThisTheMessageThread();
    }

    // Runs `fn` on the message thread and waits for it. CLAP wants activation
    // and state calls there; renders prepare the chain from a worker thread.
    template <typename Fn>
    void onMainThread (Fn&& fn)
    {
        if (onMessageThread())
            fn();
        else
            juce::MessageManager::callSync (fn);
    }

    //==========================================================================
    // One loaded .clap file. Several instances, and the scanner, share it.
    class ClapModule final : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<ClapModule>;

        static Ptr open (const juce::File& file, juce::String& error)
        {
            for (auto* m : loaded())
                if (m->file == file)
                    return m;

            Ptr m (new ClapModule (file));
            if (! m->library.open (file.getFullPathName()))
            {
                error = "could not load " + file.getFileName();
                return nullptr;
            }

            auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (m->library.getFunction ("clap_entry"));
            if (entry == nullptr || ! clap_version_is_compatible (entry->clap_version))
            {
                error = file.getFileName() + " is not a CLAP plugin this version understands";
                return nullptr;
            }

            if (! entry->init (file.getFullPathName().toRawUTF8()))
            {
                error = file.getFileName() + " failed to start";
                return nullptr;
            }

            m->entry = entry;
            keepLoaded().add (m);
            m->factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
            if (m->factory == nullptr)
            {
                error = file.getFileName() + " holds no plugins";
                return nullptr;
            }
            return m;
        }

        ~ClapModule() override
        {
            loaded().removeFirstMatchingValue (this);
            if (entry != nullptr)
                entry->deinit();
        }

        int count() const { return (int) factory->get_plugin_count (factory); }
        const clap_plugin_descriptor_t* descriptor (int i) const { return factory->get_plugin_descriptor (factory, (uint32_t) i); }

        const juce::File file;
        const clap_plugin_factory_t* factory = nullptr;

    private:
        explicit ClapModule (const juce::File& f) : file (f) { loaded().add (this); }

        // Plugin libraries stay loaded until PHINYX quits: unloading one is unsafe,
        // since it can leave behind an X error handler, exit hooks or threads
        // that point into code that is gone. Never freed, so nothing unloads at
        // exit either.
        static juce::ReferenceCountedArray<ClapModule>& keepLoaded()
        {
            static auto* modules = new juce::ReferenceCountedArray<ClapModule>();
            return *modules;
        }

        static juce::Array<ClapModule*>& loaded()
        {
            static juce::Array<ClapModule*> modules;
            return modules;
        }

        juce::DynamicLibrary library;
        const clap_plugin_entry_t* entry = nullptr;
    };

    int uniqueIdFor (const char* clapId) { return juce::String (clapId).hashCode(); }

    juce::String categoryFor (const clap_plugin_descriptor_t& d)
    {
        static const juce::StringArray general { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MONO,
                                                 CLAP_PLUGIN_FEATURE_STEREO, CLAP_PLUGIN_FEATURE_SURROUND,
                                                 CLAP_PLUGIN_FEATURE_AMBISONIC };
        for (auto* f = d.features; f != nullptr && *f != nullptr; ++f)
            if (! general.contains (*f))
            {
                const juce::String s (*f);
                return s.substring (0, 1).toUpperCase() + s.substring (1).replaceCharacter ('-', ' ');
            }
        return "Effect";
    }

    bool hasFeature (const clap_plugin_descriptor_t& d, const char* feature)
    {
        for (auto* f = d.features; f != nullptr && *f != nullptr; ++f)
            if (std::strcmp (*f, feature) == 0)
                return true;
        return false;
    }

    class ClapInstance;

    //==========================================================================
    // What the plugin sees of PHINYX. Exists before the instance (the plugin is
    // created, and its ports read, before the JUCE processor can be built).
    struct Host
    {
        Host();

        clap_host_t host {};
        const clap_plugin_t* plugin = nullptr;
        ClapInstance* owner = nullptr;
        std::weak_ptr<Host> self;

        // Timers and file descriptors the plugin (mostly its window) asked for.
        struct Timer final : juce::Timer
        {
            Timer (Host& h, clap_id i) : host (h), id (i) {}
            void timerCallback() override;
            Host& host;
            const clap_id id;
        };
        std::map<clap_id, std::unique_ptr<Timer>> timers;
        clap_id nextTimer = 1;
        std::map<int, clap_posix_fd_flags_t> fds;

        void callAsync (std::function<void (Host&)> fn)
        {
            juce::MessageManager::callAsync ([weak = self, fn = std::move (fn)]
            {
                if (auto h = weak.lock())
                    fn (*h);
            });
        }

        void stopTimersAndFds()
        {
            timers.clear();
            for (const auto& [fd, flags] : fds)
                juce::LinuxEventLoop::unregisterFdCallback (fd);
            fds.clear();
        }

        static Host& of (const clap_host_t* h) { return *static_cast<Host*> (h->host_data); }
    };

    //==========================================================================
    class ClapParameter final : public juce::HostedAudioProcessorParameter
    {
    public:
        ClapParameter (ClapInstance& o, const clap_param_info_t& i) : owner (o), info (i) {}

        float getValue() const override { return normalise (plain.load()); }
        void setValue (float v) override;
        float getDefaultValue() const override { return normalise (info.default_value); }
        juce::String getName (int maximumLength) const override { return juce::String (info.name).substring (0, maximumLength); }
        juce::String getLabel() const override { return {}; }
        juce::String getText (float value, int maximumLength) const override;
        float getValueForText (const juce::String&) const override;
        int getNumSteps() const override
        {
            return stepped() ? std::max (2, (int) std::lround (info.max_value - info.min_value) + 1)
                             : juce::AudioProcessor::getDefaultNumParameterSteps();
        }
        bool isDiscrete() const override { return stepped(); }
        bool isBoolean() const override { return stepped() && std::lround (info.max_value - info.min_value) == 1; }
        bool isAutomatable() const override { return (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
        juce::String getParameterID() const override { return juce::String (info.id); }

        double toPlain (float normalised) const
        {
            auto v = info.min_value + (double) std::clamp (normalised, 0.0f, 1.0f) * (info.max_value - info.min_value);
            return stepped() ? std::round (v) : v;
        }

        float normalise (double v) const
        {
            const auto range = info.max_value - info.min_value;
            return range > 0.0 ? (float) std::clamp ((v - info.min_value) / range, 0.0, 1.0) : 0.0f;
        }

        ClapInstance& owner;
        const clap_param_info_t info;
        std::atomic<double> plain { 0.0 };

    private:
        bool stepped() const { return (info.flags & CLAP_PARAM_IS_STEPPED) != 0; }
    };

    //==========================================================================
    struct PortInfo
    {
        int channels = 0;
        bool main = false;
    };

    std::vector<PortInfo> portsOf (const clap_plugin_t* plugin, bool input)
    {
        std::vector<PortInfo> result;
        auto* ports = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
        if (ports == nullptr)
        {
            result.push_back ({ 2, true });   // no ports extension: assume one stereo pair
            return result;
        }

        for (uint32_t i = 0, n = ports->count (plugin, input); i < n; ++i)
        {
            clap_audio_port_info_t info {};
            if (ports->get (plugin, i, input, &info))
                result.push_back ({ (int) info.channel_count, (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0 });
        }
        return result;
    }

    int mainPort (const std::vector<PortInfo>& ports)
    {
        for (size_t i = 0; i < ports.size(); ++i)
            if (ports[i].main)
                return (int) i;
        return ports.empty() ? -1 : 0;
    }

    juce::AudioChannelSet setFor (int channels)
    {
        return channels == 1 ? juce::AudioChannelSet::mono()
             : channels == 2 ? juce::AudioChannelSet::stereo()
                             : juce::AudioChannelSet::discreteChannels (channels);
    }

    //==========================================================================
    class ClapEditor;

    class ClapInstance final : public juce::AudioPluginInstance
    {
    public:
        ClapInstance (ClapModule::Ptr m, std::shared_ptr<Host> h, const clap_plugin_descriptor_t& d,
                      std::vector<PortInfo> inPorts, std::vector<PortInfo> outPorts)
            : AudioPluginInstance (busesFor (inPorts, outPorts)),
              module (std::move (m)), host (std::move (h)), plugin (host->plugin), descriptor (d),
              inputs (std::move (inPorts)), outputs (std::move (outPorts)),
              mainIn (mainPort (inputs)), mainOut (mainPort (outputs))
        {
            host->owner = this;
            params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
            state = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
            latency = static_cast<const clap_plugin_latency_t*> (plugin->get_extension (plugin, CLAP_EXT_LATENCY));
            gui = static_cast<const clap_plugin_gui_t*> (plugin->get_extension (plugin, CLAP_EXT_GUI));
            canShowGui = gui != nullptr && gui->is_api_supported (plugin, CLAP_WINDOW_API_X11, false);

            if (params != nullptr)
            {
                for (uint32_t i = 0, n = params->count (plugin); i < n; ++i)
                {
                    clap_param_info_t info {};
                    if (! params->get_info (plugin, i, &info) || (info.flags & CLAP_PARAM_IS_HIDDEN) != 0)
                        continue;

                    auto p = std::make_unique<ClapParameter> (*this, info);
                    byId[info.id] = p.get();
                    addHostedParameter (std::move (p));
                }
                refreshValues();
            }

            inEvents.ctx = this;
            inEvents.size = [] (const clap_input_events_t* l) { return (uint32_t) static_cast<ClapInstance*> (l->ctx)->pendingCount; };
            inEvents.get = [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t*
            {
                auto* self = static_cast<ClapInstance*> (l->ctx);
                return i < self->pendingCount ? &self->pending[i].header : nullptr;
            };
            outEvents.ctx = this;
            outEvents.try_push = [] (const clap_output_events_t* l, const clap_event_header_t* e)
            {
                static_cast<ClapInstance*> (l->ctx)->handleOutputEvent (*e);
                return true;
            };
        }

        ~ClapInstance() override
        {
            deactivate();
            host->stopTimersAndFds();
            plugin->destroy (plugin);
            host->owner = nullptr;
            host->plugin = nullptr;
        }

        static BusesProperties busesFor (const std::vector<PortInfo>& in, const std::vector<PortInfo>& out)
        {
            BusesProperties b;
            if (const auto i = mainPort (in); i >= 0 && in[(size_t) i].channels > 0)
                b = b.withInput ("Input", setFor (in[(size_t) i].channels), true);
            if (const auto o = mainPort (out); o >= 0 && out[(size_t) o].channels > 0)
                b = b.withOutput ("Output", setFor (out[(size_t) o].channels), true);
            return b;
        }

        //======================================================================
        void fillInPluginDescription (juce::PluginDescription& d) const override
        {
            d.name = descriptor.name;
            d.descriptiveName = descriptor.description != nullptr && *descriptor.description != 0 ? juce::String (descriptor.description) : d.name;
            d.pluginFormatName = ClapPluginFormat::formatName;
            d.category = categoryFor (descriptor);
            d.manufacturerName = descriptor.vendor;
            d.version = descriptor.version;
            d.fileOrIdentifier = module->file.getFullPathName();
            d.lastFileModTime = module->file.getLastModificationTime();
            d.lastInfoUpdateTime = juce::Time::getCurrentTime();
            d.uniqueId = d.deprecatedUid = uniqueIdFor (descriptor.id);
            d.isInstrument = hasFeature (descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT);
            d.numInputChannels = getTotalNumInputChannels();
            d.numOutputChannels = getTotalNumOutputChannels();
            d.hasSharedContainer = module->count() > 1;
        }

        const juce::String getName() const override { return descriptor.name; }
        bool isBusesLayoutSupported (const BusesLayout& layout) const override
        {
            // CLAP ports have a fixed width; the main buses may only be on or off.
            const auto inOk = mainIn < 0 || layout.inputBuses.isEmpty() || layout.getMainInputChannels() == 0
                              || layout.getMainInputChannels() == inputs[(size_t) mainIn].channels;
            const auto outOk = mainOut < 0 || layout.outputBuses.isEmpty()
                               || layout.getMainOutputChannels() == outputs[(size_t) mainOut].channels;
            return inOk && outOk;
        }

        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        //======================================================================
        void prepareToPlay (double sampleRate, int maximumBlock) override
        {
            // Activation fixes the largest block. Leave room for the 4096-frame
            // render blocks so switching between listening and rendering only
            // re-activates when the sample rate changes.
            const auto frames = (uint32_t) std::max (maximumBlock, 8192);
            onMainThread ([this, sampleRate, frames] { activate (sampleRate, frames); });
        }

        void releaseResources() override
        {
            onMainThread ([this] { deactivate(); });
        }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            const juce::ScopedTryLock sl (callbackLock);
            if (! sl.isLocked() || ! active)
                return;   // being re-activated: let the audio through untouched

            if (! processing)
            {
                processing = plugin->start_processing (plugin);
                if (! processing)
                    return;
            }

            const auto total = buffer.getNumSamples();
            for (int done = 0; done < total;)
            {
                const auto n = std::min (total - done, (int) maxFrames);
                runOnce (buffer, done, n);
                done += n;
            }
        }

        using AudioPluginInstance::processBlock;

        //======================================================================
        void getStateInformation (juce::MemoryBlock& data) override
        {
            if (state == nullptr)
                return;

            juce::MemoryOutputStream out (data, false);
            clap_ostream_t stream { &out, [] (const clap_ostream_t* s, const void* buffer, uint64_t count) -> int64_t
            {
                return static_cast<juce::MemoryOutputStream*> (s->ctx)->write (buffer, (size_t) count) ? (int64_t) count : -1;
            } };
            onMainThread ([&] { state->save (plugin, &stream); });
        }

        void setStateInformation (const void* data, int size) override
        {
            if (state == nullptr || size <= 0)
                return;

            juce::MemoryInputStream in (data, (size_t) size, false);
            clap_istream_t stream { &in, [] (const clap_istream_t* s, void* buffer, uint64_t wanted) -> int64_t
            {
                return static_cast<juce::MemoryInputStream*> (s->ctx)->read (buffer, (int) std::min<uint64_t> (wanted, 1 << 30));
            } };
            onMainThread ([&]
            {
                state->load (plugin, &stream);
                refreshValues();
            });
        }

        bool hasEditor() const override { return canShowGui; }
        juce::AudioProcessorEditor* createEditor() override;

        //======================================================================
        // Parameter changes made here are handed to the plugin with its next
        // block of audio, or straight away while it is not running.
        void queueChange (const ClapParameter& p, double value)
        {
            {
                const juce::SpinLock::ScopedLockType sl (queueLock);
                const auto scope = changes.write (1);
                if (scope.blockSize1 > 0)
                    queued[(size_t) scope.startIndex1] = { p.info.id, p.info.cookie, value };
            }

            if (! active && onMessageThread())
                flush();
        }

        void flush()   // message thread, while not active
        {
            if (params == nullptr || active)
                return;
            takeQueuedChanges();
            params->flush (plugin, &inEvents, &outEvents);
            pendingCount = 0;
        }

        void refreshValues()
        {
            if (params == nullptr)
                return;
            for (auto& [id, p] : byId)
            {
                double v = 0.0;
                if (params->get_value (plugin, id, &v))
                    p->plain = v;
            }
        }

        juce::String valueText (const ClapParameter& p, double value) const
        {
            char text[256] {};
            if (params != nullptr && params->value_to_text != nullptr && onMessageThread()
                && params->value_to_text (plugin, p.info.id, value, text, sizeof (text)))
                return juce::String::fromUTF8 (text);
            return juce::String (value, 2);
        }

        bool textValue (const ClapParameter& p, const juce::String& text, double& value) const
        {
            return params != nullptr && params->text_to_value != nullptr && onMessageThread()
                && params->text_to_value (plugin, p.info.id, text.toRawUTF8(), &value);
        }

        void restart()   // the plugin asked to be re-activated
        {
            if (! active)
                return;
            const auto rate = activeRate;
            activeRate = 0.0;
            activate (rate, maxFrames);
        }

        void updateLatency()
        {
            if (latency != nullptr && active)
                setLatencySamples ((int) latency->get (plugin));
        }

        void editorGone (ClapEditor* e)
        {
            if (editor == e)
                editor = nullptr;
        }

        ClapModule::Ptr module;
        std::shared_ptr<Host> host;
        const clap_plugin_t* plugin;
        const clap_plugin_gui_t* gui = nullptr;
        ClapEditor* editor = nullptr;
        bool active = false;

    private:
        void activate (double sampleRate, uint32_t frames)
        {
            if (active && std::abs (sampleRate - activeRate) < 1.0e-6 && frames <= maxFrames)
                return;

            {
                const juce::ScopedLock sl (callbackLock);
                stopAndDeactivate();
                maxFrames = frames;
                activeRate = sampleRate;
                active = plugin->activate (plugin, sampleRate, 1, frames);

                // Buffers for every port: the main pair carries the audio,
                // the others (side chains...) get silence.
                auto allocate = [frames] (const std::vector<PortInfo>& ports, std::vector<std::vector<float>>& storage,
                                          std::vector<std::vector<float*>>& pointers, std::vector<clap_audio_buffer_t>& buffers)
                {
                    storage.clear();
                    pointers.assign (ports.size(), {});
                    buffers.assign (ports.size(), clap_audio_buffer_t {});
                    for (size_t p = 0; p < ports.size(); ++p)
                        for (int ch = 0; ch < ports[p].channels; ++ch)
                            storage.emplace_back (frames, 0.0f);

                    size_t next = 0;
                    for (size_t p = 0; p < ports.size(); ++p)
                    {
                        for (int ch = 0; ch < ports[p].channels; ++ch)
                            pointers[p].push_back (storage[next++].data());
                        buffers[p].channel_count = (uint32_t) ports[p].channels;
                        buffers[p].data32 = pointers[p].data();
                    }
                };
                allocate (inputs, inStorage, inPointers, inBuffers);
                allocate (outputs, outStorage, outPointers, outBuffers);
            }
            updateLatency();
        }

        void deactivate()
        {
            const juce::ScopedLock sl (callbackLock);
            stopAndDeactivate();
        }

        void stopAndDeactivate()   // callbackLock held
        {
            if (! active)
                return;
            if (processing)
                plugin->stop_processing (plugin);
            processing = false;
            plugin->deactivate (plugin);
            active = false;
        }

        void runOnce (juce::AudioBuffer<float>& buffer, int offset, int n)
        {
            const auto channels = buffer.getNumChannels();

            if (mainIn >= 0)
                for (int ch = 0; ch < inputs[(size_t) mainIn].channels; ++ch)
                {
                    auto* dest = inPointers[(size_t) mainIn][(size_t) ch];
                    if (ch < channels)
                        std::copy_n (buffer.getReadPointer (ch, offset), n, dest);
                    else
                        std::fill_n (dest, n, 0.0f);
                }

            // The main output writes straight into the buffer; channels it does not have go to scratch.
            if (mainOut >= 0)
            {
                auto& ptrs = outPointers[(size_t) mainOut];
                for (int ch = 0; ch < outputs[(size_t) mainOut].channels; ++ch)
                    ptrs[(size_t) ch] = ch < channels ? buffer.getWritePointer (ch, offset)
                                                      : outStorage[(size_t) ch].data();
            }

            takeQueuedChanges();

            clap_process_t process {};
            process.steady_time = steadyTime;
            process.frames_count = (uint32_t) n;
            process.audio_inputs = inBuffers.data();
            process.audio_inputs_count = (uint32_t) inBuffers.size();
            process.audio_outputs = outBuffers.data();
            process.audio_outputs_count = (uint32_t) outBuffers.size();
            process.in_events = &inEvents;
            process.out_events = &outEvents;
            plugin->process (plugin, &process);
            steadyTime += n;
            pendingCount = 0;
        }

        void takeQueuedChanges()
        {
            pendingCount = 0;
            const auto scope = changes.read (changes.getNumReady());
            auto add = [this] (int start, int count)
            {
                for (int i = start; i < start + count && pendingCount < pending.size(); ++i)
                {
                    const auto& c = queued[(size_t) i];
                    auto& e = pending[pendingCount++];
                    e = {};
                    e.header.size = sizeof (e);
                    e.header.type = CLAP_EVENT_PARAM_VALUE;
                    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    e.param_id = c.id;
                    e.cookie = c.cookie;
                    e.note_id = e.port_index = e.channel = e.key = -1;
                    e.value = c.value;
                }
            };
            add (scope.startIndex1, scope.blockSize1);
            add (scope.startIndex2, scope.blockSize2);
        }

        void handleOutputEvent (const clap_event_header_t& e)
        {
            // The plugin's own window moved a control: show the new value here.
            if (e.space_id == CLAP_CORE_EVENT_SPACE_ID && e.type == CLAP_EVENT_PARAM_VALUE)
            {
                const auto& v = reinterpret_cast<const clap_event_param_value_t&> (e);
                if (const auto it = byId.find (v.param_id); it != byId.end())
                    it->second->plain = v.value;
            }
        }

        const clap_plugin_descriptor_t& descriptor;
        const std::vector<PortInfo> inputs, outputs;
        const int mainIn, mainOut;
        const clap_plugin_params_t* params = nullptr;
        const clap_plugin_state_t* state = nullptr;
        const clap_plugin_latency_t* latency = nullptr;
        bool canShowGui = false;
        std::unordered_map<clap_id, ClapParameter*> byId;

        juce::CriticalSection callbackLock;   // processing, against (de)activation
        bool processing = false;
        double activeRate = 0.0;
        uint32_t maxFrames = 8192;
        int64_t steadyTime = 0;
        std::vector<std::vector<float>> inStorage, outStorage;
        std::vector<std::vector<float*>> inPointers, outPointers;
        std::vector<clap_audio_buffer_t> inBuffers, outBuffers;

        struct Change { clap_id id; void* cookie; double value; };
        static constexpr int queueSize = 1024;
        juce::AbstractFifo changes { queueSize };
        std::array<Change, queueSize> queued {};
        juce::SpinLock queueLock;
        std::array<clap_event_param_value_t, queueSize> pending {};
        size_t pendingCount = 0;
        clap_input_events_t inEvents {};
        clap_output_events_t outEvents {};
    };

    void ClapParameter::setValue (float v)
    {
        const auto value = toPlain (v);
        plain = value;
        owner.queueChange (*this, value);
    }

    juce::String ClapParameter::getText (float v, int maximumLength) const
    {
        return owner.valueText (*this, toPlain (v)).substring (0, maximumLength);
    }

    float ClapParameter::getValueForText (const juce::String& text) const
    {
        double v = 0.0;
        if (owner.textValue (*this, text, v))
            return normalise (v);
        return normalise (text.retainCharacters ("+-.0123456789eE").getDoubleValue());
    }

    //==========================================================================
    // The plugin's window, embedded in a JUCE component with XEmbed.
    class ClapEditor final : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit ClapEditor (ClapInstance& p) : AudioProcessorEditor (p), owner (p)
        {
            created = owner.gui->create (owner.plugin, CLAP_WINDOW_API_X11, false);
            x11::useHostErrorHandler();
            uint32_t w = 400, h = 300;
            if (created)
                owner.gui->get_size (owner.plugin, &w, &h);
            resizable = created && owner.gui->can_resize (owner.plugin);
            setResizable (resizable, false);
            addAndMakeVisible (embedded);
            setSize ((int) std::max (w, 10u), (int) std::max (h, 10u));
            owner.editor = this;
            startTimer (20);   // attach once the window is on screen
        }

        ~ClapEditor() override
        {
            owner.editorBeingDeleted (this);   // or JUCE thinks the window is still open and never makes another
            owner.editorGone (this);
            if (created)
            {
                if (attached)
                    owner.gui->hide (owner.plugin);
                // The CLAP spec wants the plugin's window destroyed before the
                // window it sits in, so this comes before removeClient().
                owner.gui->destroy (owner.plugin);
            }
            x11::useHostErrorHandler();
            embedded.removeClient();
        }

        bool isCreated() const { return created; }

        void pluginWantsSize (uint32_t w, uint32_t h)
        {
            const juce::ScopedValueSetter<bool> s (resizingForPlugin, true);
            setSize ((int) w, (int) h);
        }

        void resized() override
        {
            embedded.setBounds (getLocalBounds());
            if (attached && resizable && ! resizingForPlugin)
            {
                auto w = (uint32_t) getWidth(), h = (uint32_t) getHeight();
                owner.gui->adjust_size (owner.plugin, &w, &h);
                owner.gui->set_size (owner.plugin, w, h);
            }
        }

    private:
        void timerCallback() override
        {
            if (! isShowing() || getPeer() == nullptr)
                return;

            stopTimer();
            const auto window = embedded.getHostWindowID();
            if (window == 0)
                return;

            clap_window_t w {};
            w.api = CLAP_WINDOW_API_X11;
            w.x11 = window;
            attached = owner.gui->set_parent (owner.plugin, &w);
            if (attached)
                owner.gui->show (owner.plugin);
            x11::useHostErrorHandler();
        }

        ClapInstance& owner;
        juce::XEmbedComponent embedded { juce::XEmbedComponentOptions{}.withWantsKeyboardFocus (true)
                                                                       .withAllowForeignWidgetToResizeComponent (false)
                                                                       .withIgnoreXembedMapped() };
        bool created = false, attached = false, resizable = false, resizingForPlugin = false;
    };

    juce::AudioProcessorEditor* ClapInstance::createEditor()
    {
        if (! canShowGui || editor != nullptr)
            return nullptr;

        auto e = std::make_unique<ClapEditor> (*this);
        return e->isCreated() ? e.release() : nullptr;
    }

    //==========================================================================
    void Host::Timer::timerCallback()
    {
        if (host.plugin == nullptr)
            return;
        if (auto* t = static_cast<const clap_plugin_timer_support_t*> (host.plugin->get_extension (host.plugin, CLAP_EXT_TIMER_SUPPORT)))
            t->on_timer (host.plugin, id);
    }

    const clap_host_log_t hostLog {
        [] (const clap_host_t*, clap_log_severity severity, const char* message)
        {
            if (severity >= CLAP_LOG_ERROR)
                juce::Logger::writeToLog (juce::String ("CLAP: ") + message);
        }
    };

    const clap_host_thread_check_t hostThreadCheck {
        [] (const clap_host_t*) { return onMessageThread(); },
        [] (const clap_host_t*) { return ! onMessageThread(); }
    };

    const clap_host_params_t hostParams {
        [] (const clap_host_t* h, clap_param_rescan_flags)
        {
            if (auto* o = Host::of (h).owner)
                o->refreshValues();
        },
        [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
        [] (const clap_host_t* h)
        {
            Host::of (h).callAsync ([] (Host& host)
            {
                if (host.owner != nullptr)
                    host.owner->flush();   // while active, the next block of audio does it
            });
        }
    };

    const clap_host_state_t hostState { [] (const clap_host_t*) {} };

    const clap_host_latency_t hostLatency {
        [] (const clap_host_t* h)
        {
            Host::of (h).callAsync ([] (Host& host)
            {
                if (host.owner != nullptr)
                    host.owner->updateLatency();
            });
        }
    };

    const clap_host_audio_ports_t hostAudioPorts {
        [] (const clap_host_t*, uint32_t) { return false; },
        [] (const clap_host_t*, uint32_t) {}
    };

    const clap_host_gui_t hostGui {
        [] (const clap_host_t*) {},
        [] (const clap_host_t* h, uint32_t width, uint32_t height)
        {
            Host::of (h).callAsync ([width, height] (Host& host)
            {
                if (host.owner != nullptr && host.owner->editor != nullptr)
                    host.owner->editor->pluginWantsSize (width, height);
            });
            return true;
        },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t*, bool) {}
    };

    const clap_host_timer_support_t hostTimers {
        [] (const clap_host_t* h, uint32_t periodMs, clap_id* id)
        {
            auto& host = Host::of (h);
            *id = host.nextTimer++;
            auto t = std::make_unique<Host::Timer> (host, *id);
            t->startTimer ((int) std::max (periodMs, 1u));
            host.timers[*id] = std::move (t);
            return true;
        },
        [] (const clap_host_t* h, clap_id id)
        {
            return Host::of (h).timers.erase (id) > 0;
        }
    };

    short pollFlags (clap_posix_fd_flags_t flags)
    {
        return (short) (((flags & CLAP_POSIX_FD_READ) != 0 ? POLLIN : 0)
                      | ((flags & CLAP_POSIX_FD_WRITE) != 0 ? POLLOUT : 0)
                      | ((flags & CLAP_POSIX_FD_ERROR) != 0 ? POLLERR : 0));
    }

    bool registerFd (Host& host, int fd, clap_posix_fd_flags_t flags)
    {
        juce::LinuxEventLoop::unregisterFdCallback (fd);
        host.fds[fd] = flags;
        juce::LinuxEventLoop::registerFdCallback (fd, [weak = host.self, flags] (int f)
        {
            if (auto h = weak.lock(); h != nullptr && h->plugin != nullptr)
                if (auto* s = static_cast<const clap_plugin_posix_fd_support_t*> (h->plugin->get_extension (h->plugin, CLAP_EXT_POSIX_FD_SUPPORT)))
                    s->on_fd (h->plugin, f, flags);
        }, pollFlags (flags));
        return true;
    }

    const clap_host_posix_fd_support_t hostFds {
        [] (const clap_host_t* h, int fd, clap_posix_fd_flags_t flags) { return registerFd (Host::of (h), fd, flags); },
        [] (const clap_host_t* h, int fd, clap_posix_fd_flags_t flags) { return registerFd (Host::of (h), fd, flags); },
        [] (const clap_host_t* h, int fd)
        {
            juce::LinuxEventLoop::unregisterFdCallback (fd);
            return Host::of (h).fds.erase (fd) > 0;
        }
    };

    Host::Host()
    {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = APP_DISPLAY_NAME;
        host.vendor = APP_DISPLAY_NAME;
        host.url = "https://github.com/phinyx852Hz/audio-editor";
        host.version = APP_VERSION;
        host.get_extension = [] (const clap_host_t*, const char* id) -> const void*
        {
            const std::string e (id);
            if (e == CLAP_EXT_LOG)              return &hostLog;
            if (e == CLAP_EXT_THREAD_CHECK)     return &hostThreadCheck;
            if (e == CLAP_EXT_PARAMS)           return &hostParams;
            if (e == CLAP_EXT_STATE)            return &hostState;
            if (e == CLAP_EXT_LATENCY)          return &hostLatency;
            if (e == CLAP_EXT_AUDIO_PORTS)      return &hostAudioPorts;
            if (e == CLAP_EXT_GUI)              return &hostGui;
            if (e == CLAP_EXT_TIMER_SUPPORT)    return &hostTimers;
            if (e == CLAP_EXT_POSIX_FD_SUPPORT) return &hostFds;
            return nullptr;
        };
        host.request_restart = [] (const clap_host_t* h)
        {
            of (h).callAsync ([] (Host& target)
            {
                if (target.owner != nullptr)
                    target.owner->restart();
            });
        };
        host.request_process = [] (const clap_host_t*) {};   // audio runs all the time the device is open
        host.request_callback = [] (const clap_host_t* h)
        {
            of (h).callAsync ([] (Host& target)
            {
                if (target.plugin != nullptr)
                    target.plugin->on_main_thread (target.plugin);
            });
        };
    }

    //==========================================================================
    std::unique_ptr<ClapInstance> instantiate (ClapModule::Ptr module, const clap_plugin_descriptor_t& d, juce::String& error)
    {
        auto host = std::make_shared<Host>();
        host->self = host;
        host->plugin = module->factory->create_plugin (module->factory, &host->host, d.id);
        if (host->plugin == nullptr)
        {
            error = juce::String (d.name) + " could not be created";
            return nullptr;
        }

        if (! host->plugin->init (host->plugin))
        {
            host->plugin->destroy (host->plugin);
            host->stopTimersAndFds();
            error = juce::String (d.name) + " failed to start";
            return nullptr;
        }

        auto in = portsOf (host->plugin, true);
        auto out = portsOf (host->plugin, false);
        return std::make_unique<ClapInstance> (module, host, d, std::move (in), std::move (out));
    }
}

//==============================================================================
void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& file)
{
    if (! fileMightContainThisPluginType (file))
        return;

    juce::String error;
    auto module = ClapModule::open (file, error);
    if (module == nullptr)
        return;

    for (int i = 0; i < module->count(); ++i)
    {
        const auto* d = module->descriptor (i);
        if (d == nullptr || d->id == nullptr || d->name == nullptr)
            continue;

        if (auto instance = instantiate (module, *d, error))
        {
            auto desc = std::make_unique<juce::PluginDescription>();
            instance->fillInPluginDescription (*desc);
            results.add (desc.release());
        }
    }
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& desc, double rate, int blockSize,
                                             PluginCreationCallback callback)
{
    juce::String error;
    std::unique_ptr<ClapInstance> result;

    if (auto module = ClapModule::open (desc.fileOrIdentifier, error))
    {
        for (int i = 0; i < module->count() && result == nullptr; ++i)
            if (const auto* d = module->descriptor (i); d != nullptr && d->id != nullptr && uniqueIdFor (d->id) == desc.uniqueId)
                result = instantiate (module, *d, error);

        if (result == nullptr && error.isEmpty())
            error = desc.name + " is no longer in " + module->file.getFileName();
    }

    if (result != nullptr)
        result->setRateAndBufferSizeDetails (rate, blockSize);

    callback (std::move (result), error);
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    const auto f = juce::File::createFileWithoutCheckingPath (fileOrIdentifier);
    return f.hasFileExtension (".clap") && f.existsAsFile();
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File::createFileWithoutCheckingPath (fileOrIdentifier).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
{
    return juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier).existsAsFile();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& folders, bool recursive, bool)
{
    juce::StringArray results;
    for (int i = 0; i < folders.getNumPaths(); ++i)
    {
        const auto folder = folders[i];
        if (! folder.isDirectory())
            continue;

        // followSymlinks: yabridge links its copies into ~/.clap/yabridge.
        for (const auto& entry : juce::RangedDirectoryIterator (folder, recursive, "*.clap", juce::File::findFiles,
                                                                juce::File::FollowSymlinks::noCycles))
            results.addIfNotAlreadyThere (entry.getFile().getFullPathName());
    }
    return results;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath result (juce::SystemStats::getEnvironmentVariable ("CLAP_PATH", {}).replace (":", ";"));
    result.add (juce::File ("~/.clap"));
    result.add (juce::File ("/usr/lib/clap"));
    result.add (juce::File ("/usr/local/lib/clap"));
    return result;
}
