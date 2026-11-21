/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later

    Host CLAP: carica i file .clap, crea le istanze e le espone come
    juce::AudioPluginInstance. Riferimento: gli header ufficiali in
    external/clap/include/clap (licenza MIT).
*/

#include "ClapPluginFormat.h"

#include <clap/clap.h>

#include <atomic>
#include <map>
#include <mutex>

#if JUCE_LINUX || JUCE_BSD
 #include <poll.h>
#endif

namespace sonora
{
namespace
{

//==============================================================================
// Thread: CLAP chiede all'host di dire se il thread corrente e' "main" o "audio".
thread_local bool isAudioThreadFlag = false;

struct ScopedAudioThread
{
    ScopedAudioThread()  : previous (isAudioThreadFlag) { isAudioThreadFlag = true; }
    ~ScopedAudioThread() { isAudioThreadFlag = previous; }
    bool previous;
};

bool isMainThread()
{
    return juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread();
}

template <typename Ext>
const Ext* getPluginExtension (const clap_plugin* plugin, const char* id)
{
    if (plugin == nullptr || plugin->get_extension == nullptr)
        return nullptr;

    return static_cast<const Ext*> (plugin->get_extension (plugin, id));
}

//==============================================================================
/** Un file .clap caricato in memoria. Condiviso tra tutte le istanze dello
    stesso file, perche' clap_entry.init/deinit vanno chiamati una sola volta.
*/
class ClapModule
{
public:
    ~ClapModule()
    {
        if (entry != nullptr)
            entry->deinit();

        library.close();
    }

    static std::shared_ptr<ClapModule> get (const juce::String& path, juce::String& error)
    {
        static std::mutex cacheLock;
        static std::map<juce::String, std::weak_ptr<ClapModule>> cache;

        const std::scoped_lock sl (cacheLock);

        if (auto existing = cache[path].lock())
            return existing;

        auto module = std::shared_ptr<ClapModule> (new ClapModule());

        if (! module->library.open (path))
        {
            error = "Impossibile caricare la libreria: " + path;
            return {};
        }

        module->entry = static_cast<const clap_plugin_entry*> (module->library.getFunction ("clap_entry"));

        if (module->entry == nullptr)
        {
            error = "Il file non esporta clap_entry: " + path;
            return {};
        }

        if (! clap_version_is_compatible (module->entry->clap_version))
        {
            error = "Versione CLAP non compatibile: " + path;
            module->entry = nullptr;
            return {};
        }

        if (! module->entry->init (path.toRawUTF8()))
        {
            error = "clap_entry.init() ha restituito false: " + path;
            module->entry = nullptr;
            return {};
        }

        module->factory = static_cast<const clap_plugin_factory*> (module->entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

        if (module->factory == nullptr)
        {
            error = "Il file non contiene una plugin factory CLAP: " + path;
            return {};
        }

        cache[path] = module;
        return module;
    }

    const clap_plugin_descriptor* findDescriptor (int uniqueId) const
    {
        for (uint32_t i = 0; i < factory->get_plugin_count (factory); ++i)
            if (auto d = factory->get_plugin_descriptor (factory, i))
                if (ClapPluginFormat::uniqueIdForClapId (d->id) == uniqueId)
                    return d;

        return nullptr;
    }

    juce::DynamicLibrary library;
    const clap_plugin_entry* entry = nullptr;
    const clap_plugin_factory* factory = nullptr;

private:
    ClapModule() = default;
};

class ClapPluginInstance;

//==============================================================================
/** Il lato "host" visto dal plugin: la struttura clap_host e i servizi che
    offriamo (timer, file descriptor, log...). Esiste prima dell'istanza JUCE,
    perche' il plugin puo' chiamare l'host gia' durante create_plugin/init.
*/
class HostContext
{
public:
    HostContext()
    {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "Ondaria";
        host.vendor = "Ondaria";
        host.url = "https://github.com/jankie98";
        host.version = SONORA_VERSION;
        host.get_extension = getExtension;
        host.request_restart = requestRestart;
        host.request_process = requestProcess;
        host.request_callback = requestCallback;
    }

    ~HostContext()
    {
        *alive = false;
        timers.clear();

       #if JUCE_LINUX || JUCE_BSD
        for (auto& fd : fds)
            juce::LinuxEventLoop::unregisterFdCallback (fd.first);
       #endif
    }

    static HostContext* from (const clap_host* h)    { return h != nullptr ? static_cast<HostContext*> (h->host_data) : nullptr; }

    /** Esegue f sul message thread, solo se questo contesto esiste ancora. */
    void callOnMainThread (std::function<void (HostContext&)> f)
    {
        if (isMainThread())
        {
            f (*this);
            return;
        }

        juce::MessageManager::callAsync ([this, a = alive, f = std::move (f)]
        {
            if (*a)
                f (*this);
        });
    }

    clap_host host {};
    const clap_plugin* plugin = nullptr;
    ClapPluginInstance* owner = nullptr;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    //==============================================================================
    struct PluginTimer final : public juce::Timer
    {
        PluginTimer (HostContext& c, clap_id i) : context (c), id (i) {}

        void timerCallback() override
        {
            if (auto ext = getPluginExtension<clap_plugin_timer_support> (context.plugin, CLAP_EXT_TIMER_SUPPORT))
                ext->on_timer (context.plugin, id);
        }

        HostContext& context;
        const clap_id id;
    };

    std::map<clap_id, std::unique_ptr<PluginTimer>> timers;
    clap_id nextTimerId = 1;
    std::map<int, clap_posix_fd_flags_t> fds;

private:
    static const void* getExtension (const clap_host*, const char* id);
    static void requestRestart (const clap_host*);
    static void requestProcess (const clap_host*) {}
    static void requestCallback (const clap_host*);
};

//==============================================================================
/** Lista di eventi in ingresso, preallocata: niente allocazioni nel thread audio. */
union ClapEvent
{
    clap_event_header_t header;
    clap_event_note_t note;
    clap_event_midi_t midi;
    clap_event_param_value_t param;
};

class InputEventList
{
public:
    InputEventList()
    {
        list.ctx = this;
        list.size = [] (const clap_input_events* l) -> uint32_t { return (uint32_t) static_cast<InputEventList*> (l->ctx)->count; };
        list.get = [] (const clap_input_events* l, uint32_t i) -> const clap_event_header_t*
        {
            auto& self = *static_cast<InputEventList*> (l->ctx);
            return i < self.count ? &self.events[i].header : nullptr;
        };
    }

    void reserve (size_t n)     { events.resize (n); }
    void clear()                { count = 0; }

    ClapEvent* add()
    {
        if (count >= events.size())
            return nullptr;

        return &events[count++];
    }

    clap_input_events_t list {};

private:
    std::vector<ClapEvent> events;
    size_t count = 0;
};

//==============================================================================
/** Un parametro CLAP esposto come parametro JUCE (valore normalizzato 0..1). */
class ClapParameter final : public juce::AudioPluginInstance::HostedParameter
{
public:
    ClapParameter (ClapPluginInstance& o, const clap_param_info& info)
        : owner (o), id (info.id), cookie (info.cookie), flags (info.flags),
          name (juce::CharPointer_UTF8 (info.name)),
          minValue (info.min_value), maxValue (info.max_value), defaultValue (info.default_value),
          plainValue (info.default_value)
    {
    }

    float toNormalised (double plain) const
    {
        if (maxValue <= minValue)
            return 0.0f;

        return (float) juce::jlimit (0.0, 1.0, (plain - minValue) / (maxValue - minValue));
    }

    double toPlain (float normalised) const
    {
        auto v = minValue + (double) normalised * (maxValue - minValue);
        return isStepped() ? std::round (v) : v;
    }

    bool isStepped() const  { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }

    float getValue() const override             { return toNormalised (plainValue.load()); }
    void setValue (float newValue) override;
    float getDefaultValue() const override      { return toNormalised (defaultValue); }
    juce::String getName (int maxLen) const override  { return name.substring (0, maxLen); }
    juce::String getLabel() const override      { return {}; }
    juce::String getParameterID() const override { return juce::String ((juce::uint32) id); }

    int getNumSteps() const override
    {
        if (isStepped() && maxValue - minValue < 1.0e6)
            return juce::jmax (2, (int) (maxValue - minValue) + 1);

        return AudioProcessorParameter::getNumSteps();
    }

    bool isDiscrete() const override            { return isStepped(); }
    bool isBoolean() const override             { return isStepped() && juce::approximatelyEqual (maxValue - minValue, 1.0); }
    bool isAutomatable() const override         { return (flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }

    juce::String getText (float normalised, int maxLen) const override;
    float getValueForText (const juce::String& text) const override;

    ClapPluginInstance& owner;
    const clap_id id;
    void* const cookie;
    const clap_param_info_flags flags;
    const juce::String name;
    const double minValue, maxValue, defaultValue;

    /** Valore attuale in unita' del plugin. */
    std::atomic<double> plainValue;
    /** Il valore e' cambiato dal lato host e va inviato al plugin. */
    std::atomic<bool> pendingToPlugin { false };
    /** Il valore e' cambiato dal lato plugin e va notificato all'host. */
    std::atomic<bool> pendingToHost { false };
};

//==============================================================================
class ClapPluginInstance final : public juce::AudioPluginInstance,
                                 private juce::Timer
{
public:
    static std::unique_ptr<ClapPluginInstance> create (const juce::PluginDescription& desc, juce::String& error)
    {
        jassert (isMainThread());

        auto module = ClapModule::get (desc.fileOrIdentifier, error);

        if (module == nullptr)
            return {};

        auto descriptor = module->findDescriptor (desc.uniqueId);

        if (descriptor == nullptr)
        {
            error = "Plugin non trovato nel file: " + desc.name;
            return {};
        }

        auto context = std::make_unique<HostContext>();
        auto plugin = module->factory->create_plugin (module->factory, &context->host, descriptor->id);

        if (plugin == nullptr)
        {
            error = "create_plugin() non ha creato il plugin: " + desc.name;
            return {};
        }

        context->plugin = plugin;

        if (! plugin->init (plugin))
        {
            plugin->destroy (plugin);
            error = "init() del plugin non riuscito: " + desc.name;
            return {};
        }

        auto ports = queryMainPorts (plugin);

        return std::unique_ptr<ClapPluginInstance> (new ClapPluginInstance (std::move (module), std::move (context),
                                                                            descriptor, ports));
    }

    struct MainPorts
    {
        int numInputs = 0, numOutputs = 0;
        uint32_t inputIndex = 0, outputIndex = 0;
        std::vector<uint32_t> inputChannels, outputChannels;  // canali di ogni porta
    };

    static MainPorts queryMainPorts (const clap_plugin* plugin)
    {
        MainPorts p;

        if (auto ext = getPluginExtension<clap_plugin_audio_ports> (plugin, CLAP_EXT_AUDIO_PORTS))
        {
            for (int dir = 0; dir < 2; ++dir)
            {
                const bool isInput = dir == 0;
                auto& channels = isInput ? p.inputChannels : p.outputChannels;
                auto& mainIndex = isInput ? p.inputIndex : p.outputIndex;
                auto& mainChannels = isInput ? p.numInputs : p.numOutputs;
                bool foundMain = false;

                for (uint32_t i = 0; i < ext->count (plugin, isInput); ++i)
                {
                    clap_audio_port_info info {};

                    if (! ext->get (plugin, i, isInput, &info))
                        continue;

                    channels.push_back (info.channel_count);

                    if (! foundMain && (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0)
                    {
                        foundMain = true;
                        mainIndex = (uint32_t) channels.size() - 1;
                        mainChannels = (int) info.channel_count;
                    }
                }

                if (! foundMain && ! channels.empty())
                {
                    mainIndex = 0;
                    mainChannels = (int) channels.front();
                }
            }
        }

        return p;
    }

    ~ClapPluginInstance() override
    {
        stopTimer();
        releaseResources();

        context->owner = nullptr;

        if (plugin != nullptr)
            plugin->destroy (plugin);

        context->plugin = nullptr;
        context.reset();
        module.reset();
    }

    //==============================================================================
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d = description;
    }

    const juce::String getName() const override         { return description.name; }
    bool acceptsMidi() const override                   { return noteInputDialects != 0; }
    bool producesMidi() const override                  { return hasNoteOutput; }
    double getTailLengthSeconds() const override
    {
        if (auto ext = getPluginExtension<clap_plugin_tail> (plugin, CLAP_EXT_TAIL); ext != nullptr && activeSampleRate > 0)
        {
            auto tail = ext->get (plugin);
            return tail == UINT32_MAX ? std::numeric_limits<double>::infinity() : tail / activeSampleRate;
        }

        return 0.0;
    }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannels() == ports.numInputs
            && layouts.getMainOutputChannels() == ports.numOutputs;
    }

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumBlockSize) override
    {
        jassert (isMainThread());
        releaseResources();

        maxBlockSize = juce::jmax (1, maximumBlockSize);

        auto allocate = [this] (const std::vector<uint32_t>& channels,
                                std::vector<juce::AudioBuffer<float>>& buffers,
                                std::vector<std::vector<float*>>& pointers)
        {
            buffers.clear();
            pointers.clear();

            for (auto numChannels : channels)
            {
                buffers.emplace_back ((int) numChannels, maxBlockSize);
                buffers.back().clear();
                pointers.emplace_back (numChannels, nullptr);
            }
        };

        allocate (ports.inputChannels, inputScratch, inputPointers);
        allocate (ports.outputChannels, outputScratch, outputPointers);
        inputBuffers.assign (ports.inputChannels.size(), clap_audio_buffer {});
        outputBuffers.assign (ports.outputChannels.size(), clap_audio_buffer {});

        inputEvents.reserve (4096 + (size_t) parameters.size());

        if (plugin->activate (plugin, sampleRate, 1, (uint32_t) maxBlockSize))
        {
            active = true;
            activeSampleRate = sampleRate;
            updateLatency();
        }
    }

    void releaseResources() override
    {
        if (! active)
            return;

        if (processing)
        {
            ScopedAudioThread audioThread;
            plugin->stop_processing (plugin);
            processing = false;
        }

        plugin->deactivate (plugin);
        active = false;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        ScopedAudioThread audioThread;
        juce::ScopedNoDenormals noDenormals;

        const auto numSamples = buffer.getNumSamples();

        if (! active || numSamples > maxBlockSize)
        {
            buffer.clear();
            midi.clear();
            return;
        }

        if (! processing)
        {
            processing = plugin->start_processing (plugin);

            if (! processing)
            {
                buffer.clear();
                midi.clear();
                return;
            }
        }

        // Eventi in ingresso: prima i parametri cambiati dall'host, poi il MIDI.
        inputEvents.clear();
        addPendingParameterEvents();
        addMidiEvents (midi);

        midi.clear();
        midiOut = &midi;

        // Audio: la porta principale usa il buffer JUCE, le altre buffer di appoggio.
        for (size_t port = 0; port < inputBuffers.size(); ++port)
        {
            auto& scratch = inputScratch[port];

            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
            {
                if (port == ports.inputIndex && ch < buffer.getNumChannels())
                    scratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
                else
                    scratch.clear (ch, 0, numSamples);

                inputPointers[port][(size_t) ch] = scratch.getWritePointer (ch);
            }

            inputBuffers[port] = { inputPointers[port].data(), nullptr, (uint32_t) scratch.getNumChannels(), 0, 0 };
        }

        for (size_t port = 0; port < outputBuffers.size(); ++port)
        {
            auto& scratch = outputScratch[port];

            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
            {
                if (port == ports.outputIndex && ch < buffer.getNumChannels())
                    outputPointers[port][(size_t) ch] = buffer.getWritePointer (ch);
                else
                    outputPointers[port][(size_t) ch] = scratch.getWritePointer (ch);
            }

            outputBuffers[port] = { outputPointers[port].data(), nullptr, (uint32_t) scratch.getNumChannels(), 0, 0 };
        }

        clap_event_transport_t transport {};
        const bool hasTransport = fillTransport (transport);

        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = (uint32_t) numSamples;
        process.transport = hasTransport ? &transport : nullptr;
        process.audio_inputs = inputBuffers.data();
        process.audio_outputs = outputBuffers.data();
        process.audio_inputs_count = (uint32_t) inputBuffers.size();
        process.audio_outputs_count = (uint32_t) outputBuffers.size();
        process.in_events = &inputEvents.list;
        process.out_events = &outputEvents;

        const auto status = plugin->process (plugin, &process);
        steadyTime += numSamples;
        midiOut = nullptr;

        if (status == CLAP_PROCESS_ERROR || outputBuffers.empty())
            buffer.clear();

        // Canali del buffer JUCE che il plugin non scrive.
        for (int ch = ports.numOutputs; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);
    }

    using AudioPluginInstance::processBlock;

    //==============================================================================
    bool hasEditor() const override
    {
        auto gui = getPluginExtension<clap_plugin_gui> (plugin, CLAP_EXT_GUI);
        return gui != nullptr && gui->is_api_supported (plugin, getWindowApi(), false);
    }

    juce::AudioProcessorEditor* createEditor() override;

    static const char* getWindowApi()
    {
       #if JUCE_WINDOWS
        return CLAP_WINDOW_API_WIN32;
       #elif JUCE_MAC
        return CLAP_WINDOW_API_COCOA;
       #else
        return CLAP_WINDOW_API_X11;
       #endif
    }

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override
    {
        auto state = getPluginExtension<clap_plugin_state> (plugin, CLAP_EXT_STATE);

        if (state == nullptr)
            return;

        juce::MemoryOutputStream out (destData, false);

        clap_ostream_t stream {};
        stream.ctx = &out;
        stream.write = [] (const clap_ostream* s, const void* buffer, uint64_t size) -> int64_t
        {
            auto& o = *static_cast<juce::MemoryOutputStream*> (s->ctx);
            return o.write (buffer, (size_t) size) ? (int64_t) size : -1;
        };

        if (! state->save (plugin, &stream))
            destData.reset();
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        auto state = getPluginExtension<clap_plugin_state> (plugin, CLAP_EXT_STATE);

        if (state == nullptr || data == nullptr || sizeInBytes <= 0)
            return;

        juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);

        clap_istream_t stream {};
        stream.ctx = &in;
        stream.read = [] (const clap_istream* s, void* buffer, uint64_t size) -> int64_t
        {
            auto& i = *static_cast<juce::MemoryInputStream*> (s->ctx);
            return (int64_t) i.read (buffer, (int) juce::jmin<uint64_t> (size, (uint64_t) std::numeric_limits<int>::max()));
        };

        if (state->load (plugin, &stream))
            refreshParameterValuesFromPlugin();
    }

    //==============================================================================
    /** Invia subito un valore al plugin quando non sta elaborando audio. */
    void parameterChangedByHost (ClapParameter& p)
    {
        p.pendingToPlugin = true;
        anyParameterPending = true;

        if (! processing && isMainThread())
            flushParametersOnMainThread();
    }

    void flushParametersOnMainThread()
    {
        auto params = getPluginExtension<clap_plugin_params> (plugin, CLAP_EXT_PARAMS);

        if (params == nullptr || params->flush == nullptr || processing)
            return;

        inputEvents.reserve (juce::jmax<size_t> (64, (size_t) parameters.size()));
        inputEvents.clear();
        addPendingParameterEvents();
        params->flush (plugin, &inputEvents.list, &outputEvents);
        inputEvents.clear();
    }

    void refreshParameterValuesFromPlugin()
    {
        auto params = getPluginExtension<clap_plugin_params> (plugin, CLAP_EXT_PARAMS);

        if (params == nullptr)
            return;

        for (auto* p : parameters)
        {
            double value = 0.0;

            if (params->get_value (plugin, p->id, &value))
            {
                p->plainValue = value;
                p->pendingToHost = true;
            }
        }

        notifyHostOfParameterChanges();
    }

    void updateLatency()
    {
        if (auto ext = getPluginExtension<clap_plugin_latency> (plugin, CLAP_EXT_LATENCY))
            setLatencySamples ((int) ext->get (plugin));
    }

    void restart()
    {
        if (! active)
            return;

        auto rate = activeSampleRate;
        auto block = maxBlockSize;
        releaseResources();
        prepareToPlay (rate, block);
    }

    ClapParameter* findParameter (clap_id id) const
    {
        auto it = parameterById.find (id);
        return it != parameterById.end() ? it->second : nullptr;
    }

    HostContext& getContext()                   { return *context; }
    const clap_plugin* getClapPlugin() const    { return plugin; }
    juce::Component::SafePointer<juce::AudioProcessorEditor> currentEditor;

private:
    ClapPluginInstance (std::shared_ptr<ClapModule> m, std::unique_ptr<HostContext> c,
                        const clap_plugin_descriptor* descriptor, const MainPorts& p)
        : AudioPluginInstance (makeBuses (p)),
          module (std::move (m)), context (std::move (c)), plugin (context->plugin), ports (p)
    {
        context->owner = this;

        description = describe (*descriptor, {});
        description.numInputChannels = ports.numInputs;
        description.numOutputChannels = ports.numOutputs;

        queryNotePorts();
        createParameters();

        outputEvents.ctx = this;
        outputEvents.try_push = [] (const clap_output_events* list, const clap_event_header_t* event) -> bool
        {
            static_cast<ClapPluginInstance*> (list->ctx)->handleOutputEvent (*event);
            return true;
        };

        startTimerHz (30);
    }

public:
    static BusesProperties makeBuses (const MainPorts& p)
    {
        BusesProperties buses;

        if (p.numInputs > 0)
            buses = buses.withInput ("Input", juce::AudioChannelSet::canonicalChannelSet (p.numInputs), true);

        if (p.numOutputs > 0)
            buses = buses.withOutput ("Output", juce::AudioChannelSet::canonicalChannelSet (p.numOutputs), true);

        return buses;
    }

    static juce::PluginDescription describe (const clap_plugin_descriptor& d, const juce::File& file)
    {
        juce::PluginDescription desc;
        desc.name = juce::CharPointer_UTF8 (d.name);
        desc.descriptiveName = d.description != nullptr ? juce::String (juce::CharPointer_UTF8 (d.description)) : desc.name;
        desc.pluginFormatName = ClapPluginFormat::formatName;
        desc.manufacturerName = d.vendor != nullptr ? juce::String (juce::CharPointer_UTF8 (d.vendor)) : juce::String();
        desc.version = d.version != nullptr ? juce::String (juce::CharPointer_UTF8 (d.version)) : juce::String();
        desc.uniqueId = desc.deprecatedUid = ClapPluginFormat::uniqueIdForClapId (juce::CharPointer_UTF8 (d.id));

        if (file != juce::File())
        {
            desc.fileOrIdentifier = file.getFullPathName();
            desc.lastFileModTime = file.getLastModificationTime();
        }

        juce::StringArray features;

        for (auto f = d.features; f != nullptr && *f != nullptr; ++f)
            features.add (juce::CharPointer_UTF8 (*f));

        desc.isInstrument = features.contains (CLAP_PLUGIN_FEATURE_INSTRUMENT);

        juce::String category = desc.isInstrument ? "Instrument"
                              : features.contains (CLAP_PLUGIN_FEATURE_NOTE_EFFECT) ? "MIDI"
                              : features.contains (CLAP_PLUGIN_FEATURE_ANALYZER) ? "Analyzer"
                              : "Effect";

        for (auto& f : features)
        {
            if (f != CLAP_PLUGIN_FEATURE_INSTRUMENT && f != CLAP_PLUGIN_FEATURE_AUDIO_EFFECT
                 && f != CLAP_PLUGIN_FEATURE_NOTE_EFFECT && f != CLAP_PLUGIN_FEATURE_ANALYZER
                 && f != "stereo" && f != "mono" && f != "surround" && f != "ambisonic")
            {
                category << "|" << f.substring (0, 1).toUpperCase() + f.substring (1);
                break;
            }
        }

        desc.category = category;
        return desc;
    }

private:
    void queryNotePorts()
    {
        if (auto ext = getPluginExtension<clap_plugin_note_ports> (plugin, CLAP_EXT_NOTE_PORTS))
        {
            if (ext->count (plugin, true) > 0)
            {
                clap_note_port_info info {};

                if (ext->get (plugin, 0, true, &info))
                    noteInputDialects = info.supported_dialects;
            }

            hasNoteOutput = ext->count (plugin, false) > 0;
        }
    }

    void createParameters()
    {
        auto params = getPluginExtension<clap_plugin_params> (plugin, CLAP_EXT_PARAMS);

        if (params == nullptr)
            return;

        for (uint32_t i = 0; i < params->count (plugin); ++i)
        {
            clap_param_info info {};

            if (! params->get_info (plugin, i, &info))
                continue;

            if ((info.flags & CLAP_PARAM_IS_HIDDEN) != 0)
                continue;

            auto p = std::make_unique<ClapParameter> (*this, info);

            double value = info.default_value;

            if (params->get_value (plugin, info.id, &value))
                p->plainValue = value;

            parameters.add (p.get());
            parameterById[info.id] = p.get();
            addHostedParameter (std::move (p));
        }
    }

    void addPendingParameterEvents()
    {
        if (! anyParameterPending.exchange (false))
            return;

        for (auto* p : parameters)
        {
            if (! p->pendingToPlugin.exchange (false))
                continue;

            if (auto e = inputEvents.add())
            {
                auto& pe = e->param;
                pe.header = { sizeof (clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                pe.param_id = p->id;
                pe.cookie = p->cookie;
                pe.note_id = -1;
                pe.port_index = -1;
                pe.channel = -1;
                pe.key = -1;
                pe.value = p->plainValue.load();
            }
        }
    }

    void addMidiEvents (const juce::MidiBuffer& midi)
    {
        if (noteInputDialects == 0)
            return;

        const bool useClapNotes = (noteInputDialects & CLAP_NOTE_DIALECT_CLAP) != 0;
        const bool useMidi = (noteInputDialects & CLAP_NOTE_DIALECT_MIDI) != 0;

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            const auto time = (uint32_t) juce::jmax (0, metadata.samplePosition);

            if (useClapNotes && (msg.isNoteOn() || msg.isNoteOff()))
            {
                if (auto e = inputEvents.add())
                {
                    auto& n = e->note;
                    const bool on = msg.isNoteOn();
                    n.header = { sizeof (clap_event_note_t), time, CLAP_CORE_EVENT_SPACE_ID,
                                 (uint16_t) (on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0 };
                    n.note_id = -1;
                    n.port_index = 0;
                    n.channel = (int16_t) (msg.getChannel() - 1);
                    n.key = (int16_t) msg.getNoteNumber();
                    n.velocity = msg.getFloatVelocity();
                }
            }
            else if (useMidi && msg.getRawDataSize() <= 3 && ! msg.isSysEx())
            {
                if (auto e = inputEvents.add())
                {
                    auto& m = e->midi;
                    m.header = { sizeof (clap_event_midi_t), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 };
                    m.port_index = 0;
                    m.data[0] = m.data[1] = m.data[2] = 0;

                    for (int i = 0; i < msg.getRawDataSize(); ++i)
                        m.data[i] = msg.getRawData()[i];
                }
            }
        }
    }

    bool fillTransport (clap_event_transport_t& t)
    {
        auto* ph = getPlayHead();

        if (ph == nullptr)
            return false;

        auto pos = ph->getPosition();

        if (! pos.hasValue())
            return false;

        t.header = { sizeof (clap_event_transport_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };

        if (auto bpm = pos->getBpm())
        {
            t.flags |= CLAP_TRANSPORT_HAS_TEMPO;
            t.tempo = *bpm;
        }

        if (auto ppq = pos->getPpqPosition())
        {
            t.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
            t.song_pos_beats = (clap_beattime) std::llround (*ppq * (double) CLAP_BEATTIME_FACTOR);

            if (auto barStart = pos->getPpqPositionOfLastBarStart())
                t.bar_start = (clap_beattime) std::llround (*barStart * (double) CLAP_BEATTIME_FACTOR);

            if (auto bars = pos->getBarCount())
                t.bar_number = (int32_t) *bars;
        }

        if (auto seconds = pos->getTimeInSeconds())
        {
            t.flags |= CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
            t.song_pos_seconds = (clap_sectime) std::llround (*seconds * (double) CLAP_SECTIME_FACTOR);
        }

        if (auto sig = pos->getTimeSignature())
        {
            t.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
            t.tsig_num = (uint16_t) sig->numerator;
            t.tsig_denom = (uint16_t) sig->denominator;
        }

        if (pos->getIsPlaying())     t.flags |= CLAP_TRANSPORT_IS_PLAYING;
        if (pos->getIsRecording())   t.flags |= CLAP_TRANSPORT_IS_RECORDING;

        if (pos->getIsLooping())
        {
            t.flags |= CLAP_TRANSPORT_IS_LOOP_ACTIVE;

            if (auto loop = pos->getLoopPoints())
            {
                t.loop_start_beats = (clap_beattime) std::llround (loop->ppqStart * (double) CLAP_BEATTIME_FACTOR);
                t.loop_end_beats = (clap_beattime) std::llround (loop->ppqEnd * (double) CLAP_BEATTIME_FACTOR);
            }
        }

        return true;
    }

    void handleOutputEvent (const clap_event_header_t& event)
    {
        if (event.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;

        switch (event.type)
        {
            case CLAP_EVENT_PARAM_VALUE:
            {
                auto& pe = reinterpret_cast<const clap_event_param_value_t&> (event);

                if (auto p = findParameter (pe.param_id))
                {
                    p->plainValue = pe.value;
                    p->pendingToHost = true;
                    anyParameterToHost = true;
                }
                break;
            }

            case CLAP_EVENT_PARAM_GESTURE_BEGIN:
            case CLAP_EVENT_PARAM_GESTURE_END:
            {
                auto& ge = reinterpret_cast<const clap_event_param_gesture_t&> (event);

                if (auto p = findParameter (ge.param_id))
                {
                    int start1, size1, start2, size2;
                    gestureFifo.prepareToWrite (1, start1, size1, start2, size2);

                    if (size1 > 0)
                    {
                        gestures[(size_t) start1] = { p->getParameterIndex(), event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN };
                        gestureFifo.finishedWrite (1);
                    }
                }
                break;
            }

            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF:
            {
                if (midiOut == nullptr)
                    break;

                auto& n = reinterpret_cast<const clap_event_note_t&> (event);
                const int channel = juce::jlimit (1, 16, n.channel + 1);
                const int key = juce::jlimit (0, 127, (int) n.key);

                midiOut->addEvent (event.type == CLAP_EVENT_NOTE_ON
                                       ? juce::MidiMessage::noteOn (channel, key, (float) n.velocity)
                                       : juce::MidiMessage::noteOff (channel, key, (float) n.velocity),
                                   (int) event.time);
                break;
            }

            case CLAP_EVENT_MIDI:
            {
                if (midiOut == nullptr)
                    break;

                auto& m = reinterpret_cast<const clap_event_midi_t&> (event);
                const auto size = juce::MidiMessage::getMessageLengthFromFirstByte (m.data[0]);
                midiOut->addEvent (m.data, size, (int) event.time);
                break;
            }

            default:
                break;
        }
    }

    /** Sul message thread: riporta all'host i cambi decisi dal plugin. */
    void timerCallback() override
    {
        notifyHostOfParameterChanges();
    }

    void notifyHostOfParameterChanges()
    {
        int start1, size1, start2, size2;
        const auto ready = gestureFifo.getNumReady();
        gestureFifo.prepareToRead (ready, start1, size1, start2, size2);

        auto sendGesture = [this] (const Gesture& g)
        {
            if (auto* p = getHostedParameter (g.parameterIndex))
            {
                if (g.begin)
                    p->beginChangeGesture();
                else
                    p->endChangeGesture();
            }
        };

        for (int i = 0; i < size1; ++i)  sendGesture (gestures[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i)  sendGesture (gestures[(size_t) (start2 + i)]);
        gestureFifo.finishedRead (size1 + size2);

        if (! anyParameterToHost.exchange (false))
            return;

        for (auto* p : parameters)
            if (p->pendingToHost.exchange (false))
                p->sendValueChangedMessageToListeners (p->getValue());
    }

    struct Gesture { int parameterIndex = 0; bool begin = false; };

    std::shared_ptr<ClapModule> module;
    std::unique_ptr<HostContext> context;
    const clap_plugin* plugin = nullptr;
    const MainPorts ports;
    juce::PluginDescription description;

    juce::Array<ClapParameter*> parameters;
    std::map<clap_id, ClapParameter*> parameterById;
    std::atomic<bool> anyParameterPending { false }, anyParameterToHost { false };

    juce::AbstractFifo gestureFifo { 256 };
    std::array<Gesture, 256> gestures {};

    uint32_t noteInputDialects = 0;
    bool hasNoteOutput = false;

    bool active = false;
    std::atomic<bool> processing { false };
    double activeSampleRate = 0.0;
    int maxBlockSize = 0;
    int64_t steadyTime = 0;

    InputEventList inputEvents;
    clap_output_events_t outputEvents {};
    juce::MidiBuffer* midiOut = nullptr;

    std::vector<juce::AudioBuffer<float>> inputScratch, outputScratch;
    std::vector<std::vector<float*>> inputPointers, outputPointers;
    std::vector<clap_audio_buffer> inputBuffers, outputBuffers;

public:
    /** Impostato dal formato, che conosce il percorso del file. */
    void setDescriptionFile (const juce::File& f)
    {
        description.fileOrIdentifier = f.getFullPathName();
        description.lastFileModTime = f.getLastModificationTime();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapPluginInstance)
};

//==============================================================================
void ClapParameter::setValue (float newValue)
{
    plainValue = toPlain (newValue);
    owner.parameterChangedByHost (*this);
}

juce::String ClapParameter::getText (float normalised, int maxLen) const
{
    const auto plain = toPlain (normalised);

    if (isMainThread())
    {
        if (auto params = getPluginExtension<clap_plugin_params> (owner.getClapPlugin(), CLAP_EXT_PARAMS);
            params != nullptr && params->value_to_text != nullptr)
        {
            char text[256] = {};

            if (params->value_to_text (owner.getClapPlugin(), id, plain, text, sizeof (text)))
                return juce::String (juce::CharPointer_UTF8 (text)).substring (0, maxLen);
        }
    }

    return juce::String (plain, isStepped() ? 0 : 3).substring (0, maxLen);
}

float ClapParameter::getValueForText (const juce::String& text) const
{
    if (isMainThread())
    {
        if (auto params = getPluginExtension<clap_plugin_params> (owner.getClapPlugin(), CLAP_EXT_PARAMS);
            params != nullptr && params->text_to_value != nullptr)
        {
            double plain = 0.0;

            if (params->text_to_value (owner.getClapPlugin(), id, text.toRawUTF8(), &plain))
                return toNormalised (plain);
        }
    }

    return toNormalised (text.getDoubleValue());
}

//==============================================================================
/** Editor grafico del plugin: la finestra nativa del plugin viene inserita
    come figlia della finestra che contiene questo componente.
*/
class ClapEditor final : public juce::AudioProcessorEditor
{
public:
    ClapEditor (ClapPluginInstance& p)
        : AudioProcessorEditor (p), instance (p), plugin (p.getClapPlugin()),
          gui (getPluginExtension<clap_plugin_gui> (plugin, CLAP_EXT_GUI))
    {
        setOpaque (true);

        created = gui != nullptr && gui->create (plugin, ClapPluginInstance::getWindowApi(), false);

        uint32_t w = 400, h = 300;

        if (created)
            gui->get_size (plugin, &w, &h);

        setSize ((int) juce::jmax (1u, w), (int) juce::jmax (1u, h));
        instance.currentEditor = this;
    }

    ~ClapEditor() override
    {
        instance.currentEditor = nullptr;

        if (created)
            gui->destroy (plugin);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);

        if (! created)
        {
            g.setColour (juce::Colours::white);
            g.drawFittedText ("Il plugin non ha un'interfaccia per questa piattaforma",
                              getLocalBounds().reduced (10), juce::Justification::centred, 3);
        }
    }

    void parentHierarchyChanged() override  { attachIfPossible(); }
    void visibilityChanged() override       { attachIfPossible(); }

    void resized() override
    {
        if (attached && ! resizingFromPlugin && gui->can_resize != nullptr && gui->can_resize (plugin))
        {
            uint32_t w = (uint32_t) getWidth(), h = (uint32_t) getHeight();

            if (gui->adjust_size (plugin, &w, &h))
                gui->set_size (plugin, w, h);
        }
    }

    bool requestResize (uint32_t w, uint32_t h)
    {
        const juce::ScopedValueSetter<bool> svs (resizingFromPlugin, true);
        setSize ((int) w, (int) h);

        if (attached)
            gui->set_size (plugin, w, h);

        return true;
    }

private:
    void attachIfPossible()
    {
        if (! created || attached || ! isShowing())
            return;

        auto* peer = getPeer();

        if (peer == nullptr)
            return;

        clap_window_t window {};
        window.api = ClapPluginInstance::getWindowApi();

       #if JUCE_WINDOWS
        window.win32 = peer->getNativeHandle();
       #elif JUCE_MAC
        window.cocoa = peer->getNativeHandle();
       #else
        window.x11 = (clap_xwnd) (juce::pointer_sized_uint) peer->getNativeHandle();
       #endif

        if (gui->set_parent (plugin, &window))
        {
            attached = true;
            gui->show (plugin);
        }
    }

    ClapPluginInstance& instance;
    const clap_plugin* plugin;
    const clap_plugin_gui* gui;
    bool created = false, attached = false, resizingFromPlugin = false;
};

juce::AudioProcessorEditor* ClapPluginInstance::createEditor()
{
    return hasEditor() ? new ClapEditor (*this) : nullptr;
}

//==============================================================================
// Estensioni che l'host offre al plugin.

const clap_host_log hostLog
{
    [] (const clap_host*, clap_log_severity severity, const char* msg)
    {
        if (isAudioThreadFlag || msg == nullptr)
            return;

        juce::ignoreUnused (severity);
        DBG ("[CLAP] " << msg);
    }
};

const clap_host_thread_check hostThreadCheck
{
    [] (const clap_host*) -> bool { return isMainThread(); },
    [] (const clap_host*) -> bool { return isAudioThreadFlag; }
};

const clap_host_params hostParams
{
    // rescan
    [] (const clap_host* h, clap_param_rescan_flags flags)
    {
        if (auto c = HostContext::from (h); c != nullptr && c->owner != nullptr
             && (flags & (CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_ALL)) != 0)
        {
            c->owner->refreshParameterValuesFromPlugin();
            c->owner->updateHostDisplay (juce::AudioProcessor::ChangeDetails().withParameterInfoChanged (true));
        }
    },
    // clear
    [] (const clap_host*, clap_id, clap_param_clear_flags) {},
    // request_flush
    [] (const clap_host* h)
    {
        if (auto c = HostContext::from (h))
            c->callOnMainThread ([] (HostContext& ctx)
            {
                if (ctx.owner != nullptr)
                    ctx.owner->flushParametersOnMainThread();
            });
    }
};

const clap_host_state hostState
{
    [] (const clap_host* h)
    {
        if (auto c = HostContext::from (h))
            c->callOnMainThread ([] (HostContext& ctx)
            {
                if (ctx.owner != nullptr)
                    ctx.owner->updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
            });
    }
};

const clap_host_latency hostLatency
{
    [] (const clap_host* h)
    {
        if (auto c = HostContext::from (h))
            c->callOnMainThread ([] (HostContext& ctx)
            {
                if (ctx.owner != nullptr)
                    ctx.owner->updateLatency();
            });
    }
};

const clap_host_audio_ports hostAudioPorts
{
    [] (const clap_host*, uint32_t) -> bool { return false; },
    [] (const clap_host*, uint32_t) {}
};

const clap_host_note_ports hostNotePorts
{
    [] (const clap_host*) -> uint32_t { return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI; },
    [] (const clap_host*, uint32_t) {}
};

const clap_host_tail hostTail
{
    [] (const clap_host*) {}
};

const clap_host_gui hostGui
{
    // resize_hints_changed
    [] (const clap_host*) {},
    // request_resize
    [] (const clap_host* h, uint32_t width, uint32_t height) -> bool
    {
        auto c = HostContext::from (h);

        if (c == nullptr)
            return false;

        c->callOnMainThread ([width, height] (HostContext& ctx)
        {
            if (ctx.owner != nullptr)
                if (auto* editor = dynamic_cast<ClapEditor*> (ctx.owner->currentEditor.getComponent()))
                    editor->requestResize (width, height);
        });

        return true;
    },
    // request_show
    [] (const clap_host*) -> bool { return false; },
    // request_hide
    [] (const clap_host*) -> bool { return false; },
    // closed
    [] (const clap_host*, bool) {}
};

const clap_host_timer_support hostTimerSupport
{
    // register_timer
    [] (const clap_host* h, uint32_t periodMs, clap_id* timerId) -> bool
    {
        auto c = HostContext::from (h);

        if (c == nullptr || timerId == nullptr || ! isMainThread())
            return false;

        const auto id = c->nextTimerId++;
        auto timer = std::make_unique<HostContext::PluginTimer> (*c, id);
        timer->startTimer ((int) juce::jmax (1u, periodMs));
        c->timers[id] = std::move (timer);
        *timerId = id;
        return true;
    },
    // unregister_timer
    [] (const clap_host* h, clap_id timerId) -> bool
    {
        auto c = HostContext::from (h);
        return c != nullptr && c->timers.erase (timerId) > 0;
    }
};

#if JUCE_LINUX || JUCE_BSD
short toPollEvents (clap_posix_fd_flags_t flags)
{
    short events = 0;
    if ((flags & CLAP_POSIX_FD_READ) != 0)   events |= POLLIN;
    if ((flags & CLAP_POSIX_FD_WRITE) != 0)  events |= POLLOUT;
    if ((flags & CLAP_POSIX_FD_ERROR) != 0)  events |= POLLERR;
    return events;
}

bool registerFd (HostContext& c, int fd, clap_posix_fd_flags_t flags)
{
    c.fds[fd] = flags;

    juce::LinuxEventLoop::registerFdCallback (fd, [&c, a = c.alive, flags] (int readyFd)
    {
        if (! *a)
            return;

        if (auto ext = getPluginExtension<clap_plugin_posix_fd_support> (c.plugin, CLAP_EXT_POSIX_FD_SUPPORT))
            ext->on_fd (c.plugin, readyFd, flags);
    }, toPollEvents (flags));

    return true;
}

const clap_host_posix_fd_support hostPosixFdSupport
{
    // register_fd
    [] (const clap_host* h, int fd, clap_posix_fd_flags_t flags) -> bool
    {
        auto c = HostContext::from (h);
        return c != nullptr && isMainThread() && registerFd (*c, fd, flags);
    },
    // modify_fd
    [] (const clap_host* h, int fd, clap_posix_fd_flags_t flags) -> bool
    {
        auto c = HostContext::from (h);

        if (c == nullptr || ! isMainThread() || c->fds.count (fd) == 0)
            return false;

        juce::LinuxEventLoop::unregisterFdCallback (fd);
        return registerFd (*c, fd, flags);
    },
    // unregister_fd
    [] (const clap_host* h, int fd) -> bool
    {
        auto c = HostContext::from (h);

        if (c == nullptr || c->fds.erase (fd) == 0)
            return false;

        juce::LinuxEventLoop::unregisterFdCallback (fd);
        return true;
    }
};
#endif

const void* HostContext::getExtension (const clap_host*, const char* id)
{
    const juce::String ext (id);

    if (ext == CLAP_EXT_LOG)            return &hostLog;
    if (ext == CLAP_EXT_THREAD_CHECK)   return &hostThreadCheck;
    if (ext == CLAP_EXT_PARAMS)         return &hostParams;
    if (ext == CLAP_EXT_STATE)          return &hostState;
    if (ext == CLAP_EXT_LATENCY)        return &hostLatency;
    if (ext == CLAP_EXT_AUDIO_PORTS)    return &hostAudioPorts;
    if (ext == CLAP_EXT_NOTE_PORTS)     return &hostNotePorts;
    if (ext == CLAP_EXT_TAIL)           return &hostTail;
    if (ext == CLAP_EXT_GUI)            return &hostGui;
    if (ext == CLAP_EXT_TIMER_SUPPORT)  return &hostTimerSupport;
   #if JUCE_LINUX || JUCE_BSD
    if (ext == CLAP_EXT_POSIX_FD_SUPPORT) return &hostPosixFdSupport;
   #endif

    return nullptr;
}

void HostContext::requestRestart (const clap_host* h)
{
    if (auto c = from (h))
        c->callOnMainThread ([] (HostContext& ctx)
        {
            if (ctx.owner != nullptr)
                ctx.owner->restart();
        });
}

void HostContext::requestCallback (const clap_host* h)
{
    if (auto c = from (h))
        c->callOnMainThread ([] (HostContext& ctx)
        {
            if (ctx.plugin != nullptr)
                ctx.plugin->on_main_thread (ctx.plugin);
        });
}

} // namespace

//==============================================================================
int ClapPluginFormat::uniqueIdForClapId (const juce::String& clapId)
{
    return clapId.hashCode();
}

void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                                            const juce::String& fileOrIdentifier)
{
    if (! fileMightContainThisPluginType (fileOrIdentifier))
        return;

    juce::String error;
    auto module = ClapModule::get (fileOrIdentifier, error);

    if (module == nullptr)
        return;

    const juce::File file (fileOrIdentifier);
    const auto count = module->factory->get_plugin_count (module->factory);

    for (uint32_t i = 0; i < count; ++i)
    {
        auto d = module->factory->get_plugin_descriptor (module->factory, i);

        if (d == nullptr || d->id == nullptr || d->name == nullptr)
            continue;

        auto desc = std::make_unique<juce::PluginDescription> (ClapPluginInstance::describe (*d, file));
        desc->hasSharedContainer = count > 1;

        // Per conoscere i canali bisogna creare il plugin: per questo la
        // scansione gira in un processo separato.
        HostContext context;

        if (auto plugin = module->factory->create_plugin (module->factory, &context.host, d->id))
        {
            context.plugin = plugin;

            if (plugin->init (plugin))
            {
                auto ports = ClapPluginInstance::queryMainPorts (plugin);
                desc->numInputChannels = ports.numInputs;
                desc->numOutputChannels = ports.numOutputs;
            }

            plugin->destroy (plugin);
            context.plugin = nullptr;
        }

        results.add (desc.release());
    }
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    const juce::File f (fileOrIdentifier);
    return f.hasFileExtension (".clap") && (f.existsAsFile() || f.isDirectory());
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File (fileOrIdentifier).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& desc)
{
    return juce::File (desc.fileOrIdentifier).getLastModificationTime() != desc.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& desc)
{
    return juce::File (desc.fileOrIdentifier).exists();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& directories,
                                                           bool recursive, bool)
{
    juce::StringArray results;

    for (int i = 0; i < directories.getNumPaths(); ++i)
    {
        const auto dir = directories.getRawString (i);

        for (const auto& entry : juce::RangedDirectoryIterator (juce::File (dir), recursive, "*.clap",
                                                                juce::File::findFilesAndDirectories))
        {
            // Su macOS un .clap e' un bundle (cartella): non va esplorato dentro.
            if (fileMightContainThisPluginType (entry.getFile().getFullPathName()))
                results.addIfNotAlreadyThere (entry.getFile().getFullPathName());
        }
    }

    return results;
}

juce::FileSearchPath ClapPluginFormat::getDefaultSearchPath()
{
    juce::FileSearchPath paths;

    // Percorsi standard dalla specifica CLAP (entry.h).
   #if JUCE_WINDOWS
    paths.add (juce::File (juce::SystemStats::getEnvironmentVariable ("COMMONPROGRAMFILES", "C:\\Program Files\\Common Files")).getChildFile ("CLAP"));
    paths.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs\\Common\\CLAP"));
    const juce::String separator = ";";
   #elif JUCE_MAC
    paths.add (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
    paths.add (juce::File ("~/Library/Audio/Plug-Ins/CLAP"));
    const juce::String separator = ":";
   #else
    paths.add (juce::File ("~/.clap"));
    paths.add (juce::File ("/usr/lib/clap"));
    paths.add (juce::File ("/usr/local/lib/clap"));
    const juce::String separator = ":";
   #endif

    for (auto& p : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("CLAP_PATH", {}), separator, {}))
        if (p.isNotEmpty())
            paths.add (juce::File (p));

    paths.removeNonExistentPaths();
    return paths;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    return getDefaultSearchPath();
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& desc, double initialSampleRate,
                                             int initialBufferSize, PluginCreationCallback callback)
{
    juce::String error;
    auto instance = ClapPluginInstance::create (desc, error);

    if (instance == nullptr)
    {
        callback (nullptr, error.isNotEmpty() ? error : juce::String ("Plugin CLAP non caricato"));
        return;
    }

    instance->setDescriptionFile (juce::File (desc.fileOrIdentifier));
    juce::ignoreUnused (initialSampleRate, initialBufferSize);
    callback (std::move (instance), {});
}

} // namespace sonora
