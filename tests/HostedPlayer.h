/*
    Sonora - motore su un dispositivo audio "ospitato" per i test.

    I blocchi li elaboriamo noi, con il MIDI e l'audio in ingresso che vogliamo,
    quindi registrazione e cattura avvengono davvero anche senza scheda audio.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace sonora::test
{

namespace te = tracktion;

/** Fa girare il motore a blocchi, con note MIDI in ingresso a tempi dati. */
class HostedPlayer
{
public:
    HostedPlayer (te::Engine& e)
        : engine (e), hosted (e.getDeviceManager().getHostedAudioDeviceInterface())
    {
        params.sampleRate = 44100.0;
        params.blockSize = 512;
        params.inputChannels = 2;
        params.outputChannels = 2;
        params.useMidiDevices = false;

        engine.getDeviceManager().setCpuLimitBeforeMuting (1000.0);
        hosted.initialise (params);
        hosted.prepareToPlay (params.sampleRate, params.blockSize);
        engine.getDeviceManager().dispatchPendingUpdates();
    }

    ~HostedPlayer()
    {
        engine.getDeviceManager().deviceManager.closeAudioDevice();
        engine.getDeviceManager().removeHostedAudioDeviceInterface();
    }

    /** Elabora seconds secondi. Le note (tempo in secondi da adesso, nota, on/off)
        entrano dall'ingresso MIDI; in ingresso audio c'e' un seno se sine e' true. */
    void process (double seconds, std::vector<std::tuple<double, int, bool>> notes = {}, bool sine = false)
    {
        const auto total = (int) std::round (seconds * params.sampleRate);

        for (int done = 0; done < total;)
        {
            const int num = std::min (params.blockSize, total - done);
            juce::AudioBuffer<float> buffer (2, num);
            buffer.clear();

            if (sine)
                for (int i = 0; i < num; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                                                   * (float) (samplePos + i) / (float) params.sampleRate));

            juce::MidiBuffer midi;

            for (auto [time, note, on] : notes)
            {
                const auto pos = (int) std::round (time * params.sampleRate);

                if (pos >= done && pos < done + num)
                    midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100)
                                      : juce::MidiMessage::noteOff (1, note),
                                   pos - done);
            }

            hosted.processBlock (buffer, midi);
            done += num;
            samplePos += num;

            if (afterBlock)
                afterBlock();

            // I timer del motore (fine delle registrazioni, aggiornamenti) girano sul thread dei messaggi.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    }

    void pump (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    std::function<void()> afterBlock;
    te::Engine& engine;
    te::HostedAudioDeviceInterface& hosted;
    te::HostedAudioDeviceInterface::Parameters params;
    int64_t samplePos = 0;
};

} // namespace sonora::test
