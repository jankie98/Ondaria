/*
    Sonora - plugin CLAP di prova per i test automatici dell'host.
    SPDX-License-Identifier: AGPL-3.0-or-later

    "Test Gain": stereo in/out, un parametro Gain (0..2), una porta note che
    rimanda in uscita le note ricevute, stato salvabile, latenza di 16 campioni.
*/

#include <clap/clap.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
constexpr clap_id gainParamId = 7;
constexpr uint32_t latencySamples = 16;

struct TestGain
{
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double gain = 1.0;
};

TestGain* self (const clap_plugin_t* p) { return static_cast<TestGain*> (p->plugin_data); }

void applyEvent (TestGain& t, const clap_event_header_t* e, const clap_output_events_t* out)
{
    if (e->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;

    if (e->type == CLAP_EVENT_PARAM_VALUE)
    {
        auto pv = reinterpret_cast<const clap_event_param_value_t*> (e);

        if (pv->param_id == gainParamId)
            t.gain = pv->value;
    }
    else if ((e->type == CLAP_EVENT_NOTE_ON || e->type == CLAP_EVENT_NOTE_OFF) && out != nullptr)
    {
        clap_event_note_t copy = *reinterpret_cast<const clap_event_note_t*> (e);
        out->try_push (out, &copy.header);
    }
}

//==============================================================================
const clap_plugin_audio_ports_t audioPorts
{
    [] (const clap_plugin_t*, bool) -> uint32_t { return 1; },
    [] (const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) -> bool
    {
        if (index != 0)
            return false;

        info->id = isInput ? 0 : 1;
        std::snprintf (info->name, sizeof (info->name), "%s", isInput ? "In" : "Out");
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }
};

const clap_plugin_note_ports_t notePorts
{
    [] (const clap_plugin_t*, bool) -> uint32_t { return 1; },
    [] (const clap_plugin_t*, uint32_t index, bool, clap_note_port_info_t* info) -> bool
    {
        if (index != 0)
            return false;

        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf (info->name, sizeof (info->name), "Note");
        return true;
    }
};

const clap_plugin_params_t params
{
    [] (const clap_plugin_t*) -> uint32_t { return 1; },
    [] (const clap_plugin_t*, uint32_t index, clap_param_info_t* info) -> bool
    {
        if (index != 0)
            return false;

        std::memset (info, 0, sizeof (*info));
        info->id = gainParamId;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::snprintf (info->name, sizeof (info->name), "Gain");
        info->min_value = 0.0;
        info->max_value = 2.0;
        info->default_value = 1.0;
        return true;
    },
    [] (const clap_plugin_t* p, clap_id id, double* value) -> bool
    {
        if (id != gainParamId)
            return false;

        *value = self (p)->gain;
        return true;
    },
    [] (const clap_plugin_t*, clap_id id, double value, char* text, uint32_t size) -> bool
    {
        if (id != gainParamId)
            return false;

        std::snprintf (text, size, "x%.2f", value);
        return true;
    },
    [] (const clap_plugin_t*, clap_id id, const char* text, double* value) -> bool
    {
        if (id != gainParamId)
            return false;

        *value = std::atof (text[0] == 'x' ? text + 1 : text);
        return true;
    },
    [] (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out)
    {
        for (uint32_t i = 0; i < in->size (in); ++i)
            applyEvent (*self (p), in->get (in, i), out);
    }
};

const clap_plugin_state_t state
{
    [] (const clap_plugin_t* p, const clap_ostream_t* stream) -> bool
    {
        const std::string text = "gain=" + std::to_string (self (p)->gain);
        return stream->write (stream, text.data(), text.size()) == (int64_t) text.size();
    },
    [] (const clap_plugin_t* p, const clap_istream_t* stream) -> bool
    {
        char buffer[128] = {};
        int64_t total = 0;

        for (;;)
        {
            auto n = stream->read (stream, buffer + total, sizeof (buffer) - 1 - (size_t) total);

            if (n <= 0)
                break;

            total += n;
        }

        if (std::strncmp (buffer, "gain=", 5) != 0)
            return false;

        self (p)->gain = std::atof (buffer + 5);
        return true;
    }
};

const clap_plugin_latency_t latency
{
    [] (const clap_plugin_t*) -> uint32_t { return latencySamples; }
};

//==============================================================================
const char* const features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor_t descriptor
{
    CLAP_VERSION_INIT,
    "org.sonora.test-gain",
    "Test Gain",
    "Sonora",
    "https://github.com/jankie98",
    "", "",
    "1.0.0",
    "Plugin di prova per i test dell'host",
    features
};

const clap_plugin_t* createPlugin (const clap_host_t* host)
{
    auto t = new TestGain();
    t->host = host;

    auto& p = t->plugin;
    p.desc = &descriptor;
    p.plugin_data = t;
    p.init = [] (const clap_plugin_t*) { return true; };
    p.destroy = [] (const clap_plugin_t* pl) { delete self (pl); };
    p.activate = [] (const clap_plugin_t*, double, uint32_t, uint32_t) { return true; };
    p.deactivate = [] (const clap_plugin_t*) {};
    p.start_processing = [] (const clap_plugin_t*) { return true; };
    p.stop_processing = [] (const clap_plugin_t*) {};
    p.reset = [] (const clap_plugin_t*) {};
    p.process = [] (const clap_plugin_t* pl, const clap_process_t* proc) -> clap_process_status
    {
        auto& t = *self (pl);

        for (uint32_t i = 0; i < proc->in_events->size (proc->in_events); ++i)
            applyEvent (t, proc->in_events->get (proc->in_events, i), proc->out_events);

        const auto& in = proc->audio_inputs[0];
        auto& out = proc->audio_outputs[0];

        for (uint32_t ch = 0; ch < 2; ++ch)
            for (uint32_t s = 0; s < proc->frames_count; ++s)
                out.data32[ch][s] = (float) (in.data32[ch][s] * t.gain);

        return CLAP_PROCESS_CONTINUE;
    };
    p.get_extension = [] (const clap_plugin_t*, const char* id) -> const void*
    {
        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
        if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0)  return &notePorts;
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)      return &params;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0)       return &state;
        if (std::strcmp (id, CLAP_EXT_LATENCY) == 0)     return &latency;
        return nullptr;
    };
    p.on_main_thread = [] (const clap_plugin_t*) {};
    return &p;
}

const clap_plugin_factory_t factory
{
    [] (const clap_plugin_factory_t*) -> uint32_t { return 1; },
    [] (const clap_plugin_factory_t*, uint32_t index) -> const clap_plugin_descriptor_t*
    {
        return index == 0 ? &descriptor : nullptr;
    },
    [] (const clap_plugin_factory_t*, const clap_host_t* host, const char* id) -> const clap_plugin_t*
    {
        if (! clap_version_is_compatible (host->clap_version) || std::strcmp (id, descriptor.id) != 0)
            return nullptr;

        return createPlugin (host);
    }
};
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry
{
    CLAP_VERSION_INIT,
    [] (const char*) { return true; },
    [] () {},
    [] (const char* id) -> const void*
    {
        return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
    }
};
