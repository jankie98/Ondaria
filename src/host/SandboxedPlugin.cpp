/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "SandboxedPlugin.h"
#include "PluginFormats.h"
#include "SandboxProtocol.h"
#include "ScanProtocol.h"
#include "SharedMemory.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace sonora::sandbox
{

namespace proto = protocol;

namespace
{
    std::atomic<Policy> currentPolicy { Policy::chosen };
    juce::CriticalSection settingsLock;
    juce::StringArray chosenPlugins;
    std::function<void (const juce::String&)> crashCallback;

    juce::MemoryBlock toBlock (const juce::ValueTree& v)
    {
        juce::MemoryOutputStream out;
        v.writeToStream (out);
        return out.getMemoryBlock();
    }

    juce::ValueTree fromBlock (const juce::MemoryBlock& mb)
    {
        return juce::ValueTree::readFromData (mb.getData(), mb.getSize());
    }

    juce::MemoryBlock binaryProperty (const juce::ValueTree& v, const juce::Identifier& id)
    {
        if (auto* mb = v[id].getBinaryData())
            return *mb;

        return {};
    }

    void copyText (char* dest, const juce::String& text)
    {
        text.copyToUTF8 (dest, (size_t) proto::maxTextLength);
    }

    const juce::Identifier reqID ("req"), errorID ("error"), dataID ("data"), latencyID ("latency"),
                           sampleRateID ("sampleRate"), blockSizeID ("blockSize"), descID ("desc"),
                           memoryID ("memory"), requestID ("request"), replyID ("reply"),
                           insID ("ins"), outsID ("outs"), midiInID ("midiIn"), midiOutID ("midiOut"),
                           editorID ("editor"), tailID ("tail"), nameID ("name"), labelID ("label"),
                           defaultID ("default"), stepsID ("steps"), discreteID ("discrete"),
                           booleanID ("boolean"), paramIdID ("id"), valueID ("value"), textID ("text");
}

//==============================================================================
Policy getPolicy()                 { return currentPolicy.load(); }
void setPolicy (Policy p)          { currentPolicy = p; }

juce::String policyName (Policy p)
{
    switch (p)
    {
        case Policy::none:      return "Nessuno";
        case Policy::chosen:    return "Solo quelli scelti";
        case Policy::all:       return "Tutti";
    }

    return {};
}

juce::StringArray getChosenPlugins()
{
    const juce::ScopedLock sl (settingsLock);
    return chosenPlugins;
}

void setChosenPlugins (const juce::StringArray& list)
{
    const juce::ScopedLock sl (settingsLock);
    chosenPlugins = list;
}

bool isChosen (const juce::PluginDescription& d)
{
    const juce::ScopedLock sl (settingsLock);
    return chosenPlugins.contains (d.createIdentifierString());
}

void setChosen (const juce::PluginDescription& d, bool b)
{
    const juce::ScopedLock sl (settingsLock);

    if (b)
        chosenPlugins.addIfNotAlreadyThere (d.createIdentifierString());
    else
        chosenPlugins.removeString (d.createIdentifierString());
}

bool shouldIsolate (const juce::PluginDescription& d)
{
    switch (getPolicy())
    {
        case Policy::none:      return false;
        case Policy::all:       return true;
        case Policy::chosen:    return isChosen (d);
    }

    return false;
}

void setCrashCallback (std::function<void (const juce::String&)> f)
{
    const juce::ScopedLock sl (settingsLock);
    crashCallback = std::move (f);
}

juce::File findHostExecutable()
{
    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

   #if JUCE_WINDOWS
    return dir.getChildFile (juce::String (scan::executableName) + ".exe");
   #else
    return dir.getChildFile (scan::executableName);
   #endif
}

//==============================================================================
//  Lato DAW
//==============================================================================
namespace
{

/** Il canale di controllo verso il processo del plugin. */
class Connection final : public juce::ChildProcessCoordinator
{
public:
    ~Connection() override
    {
        closing = true;
        killWorkerProcess();
    }

    bool launch (const juce::File& exe)
    {
        return launchWorkerProcess (exe, proto::commandLineID, 10000, 0);
    }

    /** Manda una richiesta e aspetta la risposta (invalida se scade o il processo muore). */
    juce::ValueTree request (juce::ValueTree message, int timeoutMs)
    {
        const juce::ScopedLock sl (requestLock);

        if (dead)
            return {};

        const int id = ++nextRequest;
        message.setProperty (reqID, id, nullptr);

        {
            const juce::ScopedLock rl (replyLock);
            pendingRequest = id;
            reply = {};
        }

        replyEvent.reset();

        if (! sendMessageToWorker (toBlock (message)))
            return {};

        replyEvent.wait (timeoutMs);

        const juce::ScopedLock rl (replyLock);
        pendingRequest = 0;
        return reply;
    }

    void send (const juce::ValueTree& message)
    {
        if (! dead)
            sendMessageToWorker (toBlock (message));
    }

    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        auto v = fromBlock (mb);

        if (const int id = v[reqID]; id != 0)
        {
            const juce::ScopedLock rl (replyLock);

            if (id == pendingRequest)
            {
                reply = v;
                replyEvent.signal();
            }

            return;
        }

        if (onNotification != nullptr)
            onNotification (v);
    }

    void handleConnectionLost() override
    {
        dead = true;
        replyEvent.signal();

        if (! closing && onLost != nullptr)
            onLost();
    }

    std::atomic<bool> dead { false }, closing { false };
    std::function<void()> onLost;
    std::function<void (const juce::ValueTree&)> onNotification;

private:
    juce::CriticalSection requestLock, replyLock;
    juce::WaitableEvent replyEvent;
    juce::ValueTree reply;
    int nextRequest = 0, pendingRequest = 0;
};

class IsolatedInstance;

/** Un parametro del plugin visto dalla DAW: i cambi vanno al processo col blocco audio. */
class ProxyParameter final : public juce::HostedAudioProcessorParameter
{
public:
    ProxyParameter (IsolatedInstance& o, int i, const juce::ValueTree& info);

    float getValue() const override                         { return value.load(); }
    void setValue (float v) override;
    float getDefaultValue() const override                  { return defaultValue; }
    juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
    juce::String getLabel() const override                  { return label; }
    int getNumSteps() const override                        { return steps; }
    bool isDiscrete() const override                        { return discrete; }
    bool isBoolean() const override                         { return boolean; }
    juce::String getParameterID() const override            { return parameterID; }

    juce::String getText (float v, int maximumLength) const override
    {
        {
            const juce::SpinLock::ScopedLockType sl (textLock);

            if (std::abs (v - textValue) < 1.0e-5f && text.isNotEmpty())
                return text.substring (0, maximumLength);
        }

        return juce::String (v, 3).substring (0, maximumLength);
    }

    float getValueForText (const juce::String& t) const override   { return juce::jlimit (0.0f, 1.0f, t.getFloatValue()); }

    /** Il testo di un valore, calcolato dal plugin sul suo thread dei messaggi. */
    void setTextFromPlugin (float v, const juce::String& newText)
    {
        const juce::SpinLock::ScopedLockType sl (textLock);
        textValue = v;
        text = newText;
    }

    /** Il plugin ha cambiato valore (dalla sua finestra o rispondendo a noi). */
    void updateFromPlugin (float v, const char* newText)
    {
        {
            const juce::SpinLock::ScopedLockType sl (textLock);
            textValue = v;
            text = juce::String::fromUTF8 (newText);
        }

        if (std::abs (value.exchange (v) - v) > 1.0e-6f)
            sendValueChangedMessageToListeners (v);
    }

private:
    IsolatedInstance& owner;
    const int index;
    std::atomic<float> value;
    float defaultValue;
    juce::String name, label, parameterID;
    int steps;
    bool discrete, boolean;
    mutable juce::SpinLock textLock;
    juce::String text;
    float textValue;
};

//==============================================================================
class IsolatedInstance final : public juce::AudioPluginInstance
{
public:
    IsolatedInstance (const juce::PluginDescription& d, std::shared_ptr<Connection> c,
                      std::unique_ptr<SharedRegion> r, std::unique_ptr<NamedSemaphore> rq, std::unique_ptr<NamedSemaphore> rp,
                      const juce::ValueTree& info)
        : juce::AudioPluginInstance (busesFor (info)),
          desc (d), connection (std::move (c)), region (std::move (r)), requestSemaphore (std::move (rq)), replySemaphore (std::move (rp)),
          block (static_cast<proto::SharedBlock*> (region->getData())),
          acceptsMidiInput ((bool) info[midiInID]), producesMidiOutput ((bool) info[midiOutID]),
          editorAvailable ((bool) info[editorID]), tailSeconds ((double) info[tailID])
    {
        lastState = binaryProperty (info, dataID);
        setLatencySamples ((int) info[latencyID]);

        int i = 0;

        for (auto p : info)
            if (p.hasType ("PARAM"))
                addHostedParameter (std::make_unique<ProxyParameter> (*this, i++, p));

        juce::Array<juce::AudioProcessorParameter*> list (getParameters());

        for (auto* p : list)
            proxies.add (static_cast<ProxyParameter*> (p));

        pendingIndices.resize ((size_t) proto::maxParamChanges);
        pendingValues.resize ((size_t) proto::maxParamChanges);

        connection->onLost = [this] { markCrashed(); };
        connection->onNotification = [this] (const juce::ValueTree& v)
        {
            if (v.hasType (proto::latency))
                setLatencySamples ((int) v[latencyID]);

            if (v.hasType (proto::texts))
                for (auto t : v)
                    if (const int i = t[paramIdID]; juce::isPositiveAndBelow (i, proxies.size()))
                        proxies[i]->setTextFromPlugin ((float) t[valueID], t[textID].toString());
        };
    }

    ~IsolatedInstance() override
    {
        connection->onLost = nullptr;
        connection->onNotification = nullptr;
        connection.reset();
    }

    static BusesProperties busesFor (const juce::ValueTree& info)
    {
        BusesProperties b;
        const int ins = juce::jlimit (0, proto::maxChannels, (int) info[insID]);
        const int outs = juce::jlimit (0, proto::maxChannels, (int) info[outsID]);

        if (ins > 0)   b.addBus (true, "Input", juce::AudioChannelSet::canonicalChannelSet (ins), true);
        if (outs > 0)  b.addBus (false, "Output", juce::AudioChannelSet::canonicalChannelSet (outs), true);

        return b;
    }

    //==============================================================================
    void fillInPluginDescription (juce::PluginDescription& d) const override    { d = desc; }
    const juce::String getName() const override                                 { return desc.name; }

    bool acceptsMidi() const override           { return acceptsMidiInput; }
    bool producesMidi() const override          { return producesMidiOutput; }
    bool isMidiEffect() const override          { return desc.isInstrument == false && getTotalNumInputChannels() == 0
                                                         && getTotalNumOutputChannels() == 0; }
    double getTailLengthSeconds() const override { return tailSeconds; }

    bool hasEditor() const override             { return editorAvailable; }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        // Solo la disposizione che ha il plugin nel suo processo
        return layout.getMainInputChannels() == getMainBusNumInputChannels()
            && layout.getMainOutputChannels() == getMainBusNumOutputChannels();
    }

    //==============================================================================
    void prepareToPlay (double rate, int maximumBlockSize) override
    {
        sampleRate = rate;
        midiOut.ensureSize (proto::maxMidiEvents * 8);
        answeredOnce = false;

        if (crashed)
            return;

        juce::ValueTree msg (proto::prepare);
        msg.setProperty (sampleRateID, rate, nullptr);
        msg.setProperty (blockSizeID, juce::jmin (maximumBlockSize, proto::maxBlockSize), nullptr);

        if (auto reply = connection->request (msg, 15000); reply.isValid())
            setLatencySamples ((int) reply[latencyID]);
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        midiOut.clear();

        if (crashed)
        {
            buffer.clear();
            midi.clear();
            return;
        }

        const int total = buffer.getNumSamples();

        for (int start = 0; start < total; start += proto::maxBlockSize)
            processChunk (buffer, midi, start, juce::jmin (proto::maxBlockSize, total - start));

        midi.swapWith (midiOut);
    }

    using juce::AudioPluginInstance::processBlock;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (! crashed)
            if (auto reply = connection->request (juce::ValueTree (proto::getState), 15000); reply.hasType (proto::state))
            {
                const juce::ScopedLock sl (stateLock);
                lastState = binaryProperty (reply, dataID);
            }

        // Se il plugin e' morto restituiamo l'ultimo stato buono: il progetto non lo perde
        const juce::ScopedLock sl (stateLock);
        dest = lastState;
    }

    void setStateInformation (const void* data, int size) override
    {
        {
            const juce::ScopedLock sl (stateLock);
            lastState = juce::MemoryBlock (data, (size_t) size);
        }

        if (crashed)
            return;

        juce::ValueTree msg (proto::setState);
        msg.setProperty (dataID, juce::var (juce::MemoryBlock (data, (size_t) size)), nullptr);
        const auto reply = connection->request (msg, 15000);

        // I valori dei parametri dopo lo stato nuovo, subito (senza aspettare un blocco audio)
        for (int i = 0; i < juce::jmin (reply.getNumChildren(), proxies.size()); ++i)
        {
            const auto p = reply.getChild (i);
            proxies[i]->updateFromPlugin ((float) p[valueID], p[textID].toString().toRawUTF8());
        }
    }

    //==============================================================================
    void queueParameterChange (int index, float v)
    {
        const juce::SpinLock::ScopedLockType sl (pendingLock);

        // Lo stesso parametro piu' volte nello stesso blocco: conta l'ultimo valore
        for (int i = 0; i < numPending; ++i)
            if (pendingIndices[(size_t) i] == index)
            {
                pendingValues[(size_t) i] = v;
                return;
            }

        if (numPending < proto::maxParamChanges)
        {
            pendingIndices[(size_t) numPending] = index;
            pendingValues[(size_t) numPending] = v;
            ++numPending;
        }
    }

    void showRemoteEditor()     { connection->send (juce::ValueTree (proto::showEditor)); }
    void hideRemoteEditor()     { connection->send (juce::ValueTree (proto::hideEditor)); }
    bool hasCrashed() const     { return crashed; }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int start, int num)
    {
        auto& b = *block;
        const int ins = juce::jmin (getTotalNumInputChannels(), proto::maxChannels);
        const int outs = juce::jmin (getTotalNumOutputChannels(), proto::maxChannels);
        const int available = buffer.getNumChannels();

        b.numSamples = num;
        b.numInputChannels = ins;
        b.numOutputChannels = outs;

        for (int ch = 0; ch < juce::jmax (ins, outs); ++ch)
        {
            if (ch < ins && ch < available)
                juce::FloatVectorOperations::copy (b.audio[ch], buffer.getReadPointer (ch, start), num);
            else
                juce::FloatVectorOperations::clear (b.audio[ch], num);
        }

        int count = 0;

        for (const auto m : midi)
        {
            if (m.samplePosition < start || m.samplePosition >= start + num)
                continue;

            if (m.numBytes > proto::maxMidiBytes || count >= proto::maxMidiEvents)
                continue;   // sysex lunghi: non passano

            auto& e = b.midiIn[count++];
            e.sampleOffset = m.samplePosition - start;
            e.size = m.numBytes;
            std::memcpy (e.data, m.data, (size_t) m.numBytes);
        }

        b.numMidiIn = count;

        {
            const juce::SpinLock::ScopedLockType sl (pendingLock);

            for (int i = 0; i < numPending; ++i)
            {
                b.paramChanges[i].index = pendingIndices[(size_t) i];
                b.paramChanges[i].value = pendingValues[(size_t) i];
            }

            b.numParamChanges = numPending;
            numPending = 0;
        }

        fillTransport (b, start);

        b.requestSequence = ++sequence;
        requestSemaphore->signal();

        if (! waitForReply (num))
        {
            // Niente risposta in tempo: silenzio per questo pezzo. Se il processo e' morto,
            // o non risponde da piu' blocchi di fila (bloccato), il plugin e' perso.
            for (int ch = 0; ch < available; ++ch)
                buffer.clear (ch, start, num);

            if (connection->dead || ++missedBlocks >= maxMissedBlocks)
                markCrashed();

            return;
        }

        missedBlocks = 0;

        for (int ch = 0; ch < available; ++ch)
        {
            if (ch < outs)
                juce::FloatVectorOperations::copy (buffer.getWritePointer (ch, start), b.audio[ch], num);
            else
                buffer.clear (ch, start, num);
        }

        for (int i = 0; i < juce::jmin (b.numMidiOut, proto::maxMidiEvents); ++i)
        {
            const auto& e = b.midiOut[i];
            midiOut.addEvent (e.data, juce::jlimit (1, proto::maxMidiBytes, (int) e.size), start + juce::jlimit (0, num - 1, (int) e.sampleOffset));
        }

        for (int i = 0; i < juce::jmin (b.numParamUpdates, proto::maxParamChanges); ++i)
        {
            const auto& u = b.paramUpdates[i];

            if (juce::isPositiveAndBelow (u.index, proxies.size()))
            {
                char text[proto::maxTextLength];
                std::memcpy (text, u.text, sizeof (text));
                text[proto::maxTextLength - 1] = 0;
                proxies[u.index]->updateFromPlugin (u.value, text);
            }
        }
    }

    bool waitForReply (int numSamples)
    {
        // All'inizio (caricamento, prime allocazioni) il plugin puo' metterci di piu'; poi
        // diamo almeno 250 ms, o venti volte la durata del blocco (render offline lenti).
        const int timeoutMs = ! answeredOnce ? 3000
                                             : juce::jmax (250, juce::roundToInt (20000.0 * numSamples / sampleRate));

        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (replySemaphore->wait (20))
            {
                if (block->replySequence == sequence)
                {
                    answeredOnce = true;
                    return true;
                }

                continue;   // risposta vecchia, arrivata dopo un timeout
            }

            if (connection->dead)
                return false;
        }

        return false;
    }

    void fillTransport (proto::SharedBlock& b, int offset)
    {
        b.hasPosition = 0;

        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                b.hasPosition = 1;
                b.bpm = pos->getBpm().orFallback (120.0);
                b.ppqPosition = pos->getPpqPosition().orFallback (0.0)
                                 + (offset / sampleRate) * b.bpm / 60.0;
                b.barStartPpq = pos->getPpqPositionOfLastBarStart().orFallback (0.0);
                b.timeInSamples = pos->getTimeInSamples().orFallback (0) + offset;
                auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature());
                b.timeSigNumerator = sig.numerator;
                b.timeSigDenominator = sig.denominator;
                b.isPlaying = pos->getIsPlaying() ? 1 : 0;
                b.isRecording = pos->getIsRecording() ? 1 : 0;
                b.isLooping = pos->getIsLooping() ? 1 : 0;
                auto loop = pos->getLoopPoints().orFallback (juce::AudioPlayHead::LoopPoints());
                b.ppqLoopStart = loop.ppqStart;
                b.ppqLoopEnd = loop.ppqEnd;
            }
    }

    void markCrashed()
    {
        if (crashed.exchange (true))
            return;

        juce::MessageManager::callAsync ([name = desc.name, c = connection]
        {
            // Se e' solo bloccato, il processo va chiuso: non lo usiamo piu'
            c->closing = true;
            c->killWorkerProcess();

            std::function<void (const juce::String&)> f;

            {
                const juce::ScopedLock sl (settingsLock);
                f = crashCallback;
            }

            if (f != nullptr)
                f (name);
        });
    }

    juce::PluginDescription desc;
    std::shared_ptr<Connection> connection;
    std::unique_ptr<SharedRegion> region;
    std::unique_ptr<NamedSemaphore> requestSemaphore, replySemaphore;
    proto::SharedBlock* block;
    const bool acceptsMidiInput, producesMidiOutput, editorAvailable;
    const double tailSeconds;

    juce::Array<ProxyParameter*> proxies;
    juce::SpinLock pendingLock;
    std::vector<int> pendingIndices;
    std::vector<float> pendingValues;
    int numPending = 0;

    juce::CriticalSection stateLock;
    juce::MemoryBlock lastState;
    juce::MidiBuffer midiOut;
    double sampleRate = 44100.0;
    uint32_t sequence = 0;
    bool answeredOnce = false;
    int missedBlocks = 0;
    static constexpr int maxMissedBlocks = 4;
    std::atomic<bool> crashed { false };
};

ProxyParameter::ProxyParameter (IsolatedInstance& o, int i, const juce::ValueTree& info)
    : owner (o), index (i), value ((float) info[valueID]), defaultValue ((float) info[defaultID]),
      name (info[nameID].toString()), label (info[labelID].toString()), parameterID (info[paramIdID].toString()),
      steps ((int) info[stepsID]), discrete ((bool) info[discreteID]), boolean ((bool) info[booleanID]),
      text (info[textID].toString()), textValue ((float) info[valueID])
{
    if (parameterID.isEmpty())
        parameterID = juce::String (index);
}

void ProxyParameter::setValue (float v)
{
    value = v;
    owner.queueParameterChange (index, v);
}

//==============================================================================
/** Nella DAW, al posto della finestra del plugin: la vera si apre dal suo processo. */
class RemoteEditor final : public juce::AudioProcessorEditor
{
public:
    explicit RemoteEditor (IsolatedInstance& p) : juce::AudioProcessorEditor (p), instance (p)
    {
        showButton.onClick = [this] { instance.showRemoteEditor(); };
        addAndMakeVisible (showButton);
        setSize (380, 92);
        instance.showRemoteEditor();
    }

    ~RemoteEditor() override
    {
        instance.hideRemoteEditor();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1d22));
        g.setColour (juce::Colour (0xffc8ccd4));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (instance.hasCrashed()
                              ? instance.getName() + juce::String::fromUTF8 (" si \xc3\xa8 chiuso: rimuovilo o riavvialo dal menu del device.")
                              : instance.getName() + juce::String::fromUTF8 (" gira isolato, in un processo suo: la sua finestra \xc3\xa8 aperta a parte."),
                          getLocalBounds().reduced (14, 10).withTrimmedBottom (32), juce::Justification::centredLeft, 3);
    }

    void resized() override
    {
        showButton.setBounds (getLocalBounds().reduced (14, 10).removeFromBottom (26).removeFromLeft (180));
    }

private:
    IsolatedInstance& instance;
    juce::TextButton showButton { "Mostra la finestra" };
};

juce::AudioProcessorEditor* IsolatedInstance::createEditor()
{
    return new RemoteEditor (*this);
}

} // namespace

//==============================================================================
std::unique_ptr<juce::AudioPluginInstance> createIsolatedInstance (const juce::PluginDescription& desc,
                                                                   double sampleRate, int blockSize,
                                                                   juce::String& errorMessage,
                                                                   const juce::File& hostExecutable)
{
    if (! hostExecutable.existsAsFile())
    {
        errorMessage = "Manca " + hostExecutable.getFileName() + " accanto alla DAW";
        return {};
    }

    const auto name = createUniqueName();
    auto region = SharedRegion::create (name, sizeof (proto::SharedBlock));
    auto request = NamedSemaphore::create (name + "-a");
    auto reply = NamedSemaphore::create (name + "-b");

    if (region == nullptr || request == nullptr || reply == nullptr)
    {
        errorMessage = "Impossibile preparare la memoria condivisa per il plugin isolato";
        return {};
    }

    static_cast<proto::SharedBlock*> (region->getData())->magic = proto::magicValue;

    auto connection = std::make_shared<Connection>();

    if (! connection->launch (hostExecutable))
    {
        errorMessage = "Impossibile avviare il processo del plugin";
        return {};
    }

    juce::ValueTree msg (proto::load);

    if (auto xml = desc.createXml())
        msg.setProperty (descID, xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);

    msg.setProperty (sampleRateID, sampleRate, nullptr);
    msg.setProperty (blockSizeID, juce::jlimit (16, proto::maxBlockSize, blockSize), nullptr);
    msg.setProperty (memoryID, region->getPath(), nullptr);
    msg.setProperty (requestID, name + "-a", nullptr);
    msg.setProperty (replyID, name + "-b", nullptr);

    auto info = connection->request (msg, 120000);

    if (! info.isValid())
    {
        errorMessage = connection->dead ? juce::String::fromUTF8 ("Il plugin \xc3\xa8 andato in crash mentre si caricava")
                                        : juce::String ("Il plugin non risponde");
        return {};
    }

    if (info.hasType (proto::failed))
    {
        errorMessage = info[errorID].toString();
        return {};
    }

    return std::make_unique<IsolatedInstance> (desc, std::move (connection), std::move (region),
                                               std::move (request), std::move (reply), info);
}

bool isIsolated (const juce::AudioProcessor* p)
{
    return dynamic_cast<const IsolatedInstance*> (p) != nullptr;
}

bool hasCrashed (const juce::AudioProcessor* p)
{
    auto i = dynamic_cast<const IsolatedInstance*> (p);
    return i != nullptr && i->hasCrashed();
}

//==============================================================================
//  Lato plugin (processo ospite)
//==============================================================================
namespace
{

/** Restituisce al plugin la posizione del trasporto arrivata dalla DAW. */
class SharedPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (block == nullptr || block->hasPosition == 0)
            return {};

        PositionInfo info;
        info.setBpm (block->bpm);
        info.setPpqPosition (block->ppqPosition);
        info.setPpqPositionOfLastBarStart (block->barStartPpq);
        info.setTimeInSamples (block->timeInSamples);
        info.setTimeSignature (TimeSignature { block->timeSigNumerator, block->timeSigDenominator });
        info.setIsPlaying (block->isPlaying != 0);
        info.setIsRecording (block->isRecording != 0);
        info.setIsLooping (block->isLooping != 0);
        info.setLoopPoints (LoopPoints { block->ppqLoopStart, block->ppqLoopEnd });
        return info;
    }

    const proto::SharedBlock* block = nullptr;
};

class HostWorker final : public juce::ChildProcessWorker,
                         private juce::Thread,
                         private juce::Timer,
                         private juce::AudioProcessorParameter::Listener
{
public:
    HostWorker() : juce::Thread ("Ondaria plugin audio") {}

    ~HostWorker() override
    {
        stopTimer();
        stopThread (2000);
        window.reset();

        if (instance != nullptr)
            for (auto* p : instance->getParameters())
                p->removeListener (this);

        instance.reset();
    }

    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        auto v = fromBlock (mb);

        // I plugin si caricano e si toccano dal thread dei messaggi; questo thread
        // deve restare libero per i ping.
        juce::MessageManager::callAsync ([this, v] { handle (v); });
    }

    void handleConnectionLost() override
    {
        // La DAW non c'e' piu' (o ci ha chiuso): usciamo.
        juce::MessageManager::callAsync ([] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    }

private:
    void reply (const juce::ValueTree& request, juce::ValueTree answer)
    {
        answer.setProperty (reqID, request[reqID], nullptr);
        sendMessageToCoordinator (toBlock (answer));
    }

    void fail (const juce::ValueTree& request, const juce::String& error)
    {
        juce::ValueTree answer (proto::failed);
        answer.setProperty (errorID, error, nullptr);
        reply (request, answer);
    }

    void handle (const juce::ValueTree& v)
    {
        if (v.hasType (proto::load))                 load (v);
        else if (instance == nullptr)                return;
        else if (v.hasType (proto::prepare))         prepare (v);
        else if (v.hasType (proto::getState))        sendState (v);
        else if (v.hasType (proto::setState))        restoreState (v);
        else if (v.hasType (proto::showEditor))      openWindow();
        else if (v.hasType (proto::hideEditor))      window.reset();
    }

    void load (const juce::ValueTree& v)
    {
        juce::PluginDescription desc;

        if (auto xml = juce::parseXML (v[descID].toString()); xml == nullptr || ! desc.loadFromXml (*xml))
            return fail (v, "Descrizione del plugin non valida");

        sampleRate = (double) v[sampleRateID];
        blockSize = (int) v[blockSizeID];

        region = SharedRegion::open (v[memoryID].toString(), sizeof (proto::SharedBlock));
        requestSemaphore = NamedSemaphore::open (v[requestID].toString());
        replySemaphore = NamedSemaphore::open (v[replyID].toString());

        if (region == nullptr || requestSemaphore == nullptr || replySemaphore == nullptr)
            return fail (v, "Memoria condivisa non raggiungibile");

        block = static_cast<proto::SharedBlock*> (region->getData());

        if (block->magic != proto::magicValue)
            return fail (v, "Memoria condivisa non valida");

        addAllPluginFormats (formats);
        juce::String error;
        instance = formats.createPluginInstance (desc, sampleRate, blockSize, error);

        if (instance == nullptr)
            return fail (v, error.isNotEmpty() ? error : juce::String ("Impossibile caricare il plugin"));

        instance->enableAllBuses();
        playHead.block = block;
        instance->setPlayHead (&playHead);
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        instance->prepareToPlay (sampleRate, blockSize);

        auto params = instance->getParameters();
        dirty = std::make_unique<std::atomic<bool>[]> ((size_t) params.size());
        textDirty = std::make_unique<std::atomic<bool>[]> ((size_t) params.size());
        // Un posto in piu': AbstractFifo ne tiene uno sempre libero
        changedIndices.resize ((size_t) params.size() + 1);
        fifo.setTotalSize ((int) changedIndices.size());

        juce::ValueTree info (proto::loaded);
        info.setProperty (insID, instance->getTotalNumInputChannels(), nullptr);
        info.setProperty (outsID, instance->getTotalNumOutputChannels(), nullptr);
        info.setProperty (midiInID, instance->acceptsMidi(), nullptr);
        info.setProperty (midiOutID, instance->producesMidi(), nullptr);
        info.setProperty (editorID, instance->hasEditor(), nullptr);
        info.setProperty (tailID, instance->getTailLengthSeconds(), nullptr);
        info.setProperty (latencyID, instance->getLatencySamples(), nullptr);

        juce::MemoryBlock state;
        instance->getStateInformation (state);
        info.setProperty (dataID, juce::var (state), nullptr);

        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            juce::ValueTree pt ("PARAM");
            pt.setProperty (nameID, p->getName (128), nullptr);
            pt.setProperty (labelID, p->getLabel(), nullptr);
            pt.setProperty (defaultID, p->getDefaultValue(), nullptr);
            pt.setProperty (valueID, p->getValue(), nullptr);
            pt.setProperty (textID, p->getText (p->getValue(), proto::maxTextLength - 1), nullptr);
            pt.setProperty (stepsID, p->getNumSteps(), nullptr);
            pt.setProperty (discreteID, p->isDiscrete(), nullptr);
            pt.setProperty (booleanID, p->isBoolean(), nullptr);

            if (auto hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
                pt.setProperty (paramIdID, hosted->getParameterID(), nullptr);

            info.appendChild (pt, nullptr);
            dirty[(size_t) i] = false;
            textDirty[(size_t) i] = false;
            p->addListener (this);
        }

        lastLatency = instance->getLatencySamples();
        reply (v, info);
        startThread (juce::Thread::Priority::highest);
        startTimerHz (30);
    }

    void prepare (const juce::ValueTree& v)
    {
        {
            const juce::ScopedLock sl (processLock);
            sampleRate = (double) v[sampleRateID];
            blockSize = (int) v[blockSizeID];
            instance->releaseResources();
            instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
            instance->prepareToPlay (sampleRate, blockSize);
            lastLatency = instance->getLatencySamples();
        }

        juce::ValueTree answer (proto::prepared);
        answer.setProperty (latencyID, lastLatency.load(), nullptr);
        reply (v, answer);
    }

    void sendState (const juce::ValueTree& v)
    {
        juce::MemoryBlock state;
        instance->getStateInformation (state);
        juce::ValueTree answer (proto::state);
        answer.setProperty (dataID, juce::var (state), nullptr);
        reply (v, answer);
    }

    void restoreState (const juce::ValueTree& v)
    {
        {
            const juce::ScopedLock sl (processLock);
            auto data = binaryProperty (v, dataID);
            instance->setStateInformation (data.getData(), (int) data.getSize());

            // I valori nuovi dei parametri vanno alla DAW col prossimo blocco
            for (int i = 0; i < instance->getParameters().size(); ++i)
                markDirty (i);
        }

        // ...e anche subito, nella risposta
        juce::ValueTree answer (proto::done);

        for (auto* p : instance->getParameters())
        {
            juce::ValueTree param ("PARAM");
            param.setProperty (valueID, p->getValue(), nullptr);
            param.setProperty (textID, p->getCurrentValueAsText(), nullptr);
            answer.appendChild (param, nullptr);
        }

        reply (v, answer);
    }

    void openWindow()
    {
        if (window != nullptr)
        {
            window->toFront (true);
            return;
        }

        if (! instance->hasEditor())
            return;

        if (auto* editor = instance->createEditorIfNeeded())
        {
            window = std::make_unique<Window> (instance->getName());
            window->setContentNonOwned (editor, true);
            window->centreWithSize (editor->getWidth(), editor->getHeight());
            window->setVisible (true);
            window->toFront (true);
        }
    }

    //==============================================================================
    void run() override
    {
        while (! threadShouldExit())
        {
            if (! requestSemaphore->wait (100))
                continue;

            {
                const juce::ScopedLock sl (processLock);
                process (*block);
            }

            block->replySequence = block->requestSequence;
            replySemaphore->signal();

            // La latenza cambiata (per esempio da un'impostazione del plugin) va detta alla DAW
            if (const int l = instance->getLatencySamples(); l != lastLatency)
            {
                lastLatency = l;
                juce::ValueTree note (proto::latency);
                note.setProperty (latencyID, l, nullptr);
                sendMessageToCoordinator (toBlock (note));
            }
        }
    }

    void process (proto::SharedBlock& b)
    {
        const int num = juce::jlimit (0, proto::maxBlockSize, (int) b.numSamples);
        const int channels = juce::jmin (proto::maxChannels, juce::jmax (instance->getTotalNumInputChannels(),
                                                                         instance->getTotalNumOutputChannels()));

        for (int ch = juce::jmax (0, (int) b.numInputChannels); ch < channels; ++ch)
            juce::FloatVectorOperations::clear (b.audio[ch], num);

        float* channelPointers[proto::maxChannels];

        for (int ch = 0; ch < proto::maxChannels; ++ch)
            channelPointers[ch] = b.audio[ch];

        juce::AudioBuffer<float> buffer (channelPointers, channels, num);

        midiBuffer.clear();

        for (int i = 0; i < juce::jmin ((int) b.numMidiIn, proto::maxMidiEvents); ++i)
        {
            const auto& e = b.midiIn[i];
            midiBuffer.addEvent (e.data, juce::jlimit (1, proto::maxMidiBytes, (int) e.size), juce::jlimit (0, juce::jmax (0, num - 1), (int) e.sampleOffset));
        }

        auto& params = instance->getParameters();

        for (int i = 0; i < juce::jmin ((int) b.numParamChanges, proto::maxParamChanges); ++i)
        {
            const auto& c = b.paramChanges[i];

            if (juce::isPositiveAndBelow (c.index, params.size()))
            {
                params[c.index]->setValue (c.value);
                markDirty (c.index);   // la DAW vuole il testo del valore nuovo
            }
        }

        // Il plugin e' preparato per blocchi di blockSize: quelli piu' lunghi a pezzi
        if (num <= blockSize)
        {
            instance->processBlock (buffer, midiBuffer);
        }
        else
        {
            juce::MidiBuffer allOut;

            for (int start = 0; start < num; start += blockSize)
            {
                const int len = juce::jmin (blockSize, num - start);
                float* pieces[proto::maxChannels];

                for (int ch = 0; ch < channels; ++ch)
                    pieces[ch] = b.audio[ch] + start;

                juce::AudioBuffer<float> piece (pieces, channels, len);
                juce::MidiBuffer pieceMidi;
                pieceMidi.addEvents (midiBuffer, start, len, -start);
                instance->processBlock (piece, pieceMidi);
                allOut.addEvents (pieceMidi, 0, len, start);
            }

            midiBuffer.swapWith (allOut);
        }

        int count = 0;

        for (const auto m : midiBuffer)
        {
            if (m.numBytes > proto::maxMidiBytes || count >= proto::maxMidiEvents)
                continue;

            auto& e = b.midiOut[count++];
            e.sampleOffset = m.samplePosition;
            e.size = m.numBytes;
            std::memcpy (e.data, m.data, (size_t) m.numBytes);
        }

        b.numMidiOut = count;

        // Parametri cambiati: dalla finestra del plugin, da uno stato nuovo o da noi
        int updates = 0;
        int start1, size1, start2, size2;
        fifo.prepareToRead (juce::jmin (fifo.getNumReady(), proto::maxParamChanges), start1, size1, start2, size2);

        auto addUpdate = [&] (int index)
        {
            dirty[(size_t) index] = false;
            auto* p = params[index];
            auto& u = b.paramUpdates[updates++];
            u.index = index;
            u.value = p->getValue();
            copyText (u.text, p->getText (u.value, proto::maxTextLength - 1));

            // Alcuni formati (CLAP) danno il testo vero solo sul thread dei messaggi
            textDirty[(size_t) index] = true;
        };

        for (int i = 0; i < size1; ++i)  addUpdate (changedIndices[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i)  addUpdate (changedIndices[(size_t) (start2 + i)]);

        fifo.finishedRead (size1 + size2);
        b.numParamUpdates = updates;
    }

    void markDirty (int index)
    {
        if (dirty == nullptr || dirty[(size_t) index].exchange (true))
            return;

        const juce::SpinLock::ScopedLockType sl (fifoWriteLock);
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
            changedIndices[(size_t) start1] = index;
        else
            dirty[(size_t) index] = false;   // pieno: si perde solo l'aggiornamento del testo

        fifo.finishedWrite (size1);
    }

    void parameterValueChanged (int index, float) override             { markDirty (index); }

    void timerCallback() override
    {
        if (instance == nullptr || textDirty == nullptr)
            return;

        auto& params = instance->getParameters();
        juce::ValueTree note (proto::texts);

        for (int i = 0; i < params.size(); ++i)
            if (textDirty[(size_t) i].exchange (false))
            {
                juce::ValueTree t ("T");
                const auto value = params[i]->getValue();
                t.setProperty (paramIdID, i, nullptr);
                t.setProperty (valueID, value, nullptr);
                t.setProperty (textID, params[i]->getText (value, proto::maxTextLength - 1), nullptr);
                note.appendChild (t, nullptr);
            }

        if (note.getNumChildren() > 0)
            sendMessageToCoordinator (toBlock (note));
    }
    void parameterGestureChanged (int, bool) override                  {}

    //==============================================================================
    struct Window final : public juce::DocumentWindow
    {
        explicit Window (const juce::String& name)
            : juce::DocumentWindow (name, juce::Colour (0xff1b1d22), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
        }

        void closeButtonPressed() override      { setVisible (false); }
    };

    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<SharedRegion> region;
    std::unique_ptr<NamedSemaphore> requestSemaphore, replySemaphore;
    proto::SharedBlock* block = nullptr;
    SharedPlayHead playHead;
    juce::CriticalSection processLock;
    juce::MidiBuffer midiBuffer;
    std::unique_ptr<std::atomic<bool>[]> dirty, textDirty;
    std::vector<int> changedIndices;
    juce::AbstractFifo fifo { 1 };
    juce::SpinLock fifoWriteLock;
    std::unique_ptr<Window> window;
    double sampleRate = 44100.0;
    int blockSize = 512;
    std::atomic<int> lastLatency { 0 };
};

} // namespace

bool runHostIfRequested (const juce::String& commandLine)
{
    if (! commandLine.contains (proto::commandLineID))
        return false;

    juce::ScopedJuceInitialiser_GUI juceInit;
    auto worker = std::make_unique<HostWorker>();

    if (worker->initialiseFromCommandLine (commandLine, proto::commandLineID, 10000))
        juce::MessageManager::getInstance()->runDispatchLoop();

    // Un plugin bloccato potrebbe non lasciarci chiudere: dopo tre secondi usciamo comunque
    std::thread ([] { std::this_thread::sleep_for (std::chrono::seconds (3)); std::_Exit (0); }).detach();
    worker.reset();
    return true;
}

} // namespace sonora::sandbox
