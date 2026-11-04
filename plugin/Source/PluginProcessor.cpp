#include "PluginProcessor.h"
#include "PluginEditor.h"

GigmaAudioProcessor::GigmaAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Gigma broker sync")
{
    // Internet relay by default (works across any NAT). GIGMA_DIRECT=1 forces
    // LAN/VPN-only direct UDP for the lowest possible latency.
    sessionClient.setUseRelay (juce::SystemStats::getEnvironmentVariable ("GIGMA_DIRECT", {}).isEmpty());
    loadUserDefaults();   // last quality the user picked (project state overrides later)
    startThread();
}

namespace {
juce::File gigmaSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Gigma").getChildFile ("quality.xml");
}

juce::ValueTree qualityToTree (GigmaAudioProcessor::Preset p, const GigmaAooEngine::Quality& q)
{
    juce::ValueTree t ("GigmaQuality");
    t.setProperty ("preset", (int) p, nullptr);
    t.setProperty ("bufferMs", q.bufferMs, nullptr);
    t.setProperty ("bitrateKbps", q.bitrateKbps, nullptr);
    t.setProperty ("frameMs", q.frameMs, nullptr);
    t.setProperty ("redundancy", q.redundancy, nullptr);
    t.setProperty ("limiter", q.limiter, nullptr);
    return t;
}

bool treeToQuality (const juce::ValueTree& t, GigmaAudioProcessor::Preset& p, GigmaAooEngine::Quality& q)
{
    if (! t.isValid() || ! t.hasType ("GigmaQuality"))
        return false;
    const GigmaAooEngine::Quality d;
    p = (GigmaAudioProcessor::Preset) juce::jlimit (0, 3, (int) t.getProperty ("preset", 1));
    q.bufferMs = t.getProperty ("bufferMs", d.bufferMs);
    q.bitrateKbps = t.getProperty ("bitrateKbps", d.bitrateKbps);
    q.frameMs = t.getProperty ("frameMs", d.frameMs);
    q.redundancy = t.getProperty ("redundancy", d.redundancy);
    q.limiter = t.getProperty ("limiter", d.limiter);
    return true;
}
} // namespace

GigmaAooEngine::Quality GigmaAudioProcessor::presetQuality (Preset p)
{
    GigmaAooEngine::Quality q;
    switch (p)
    {
        case Preset::lowLatency: q.bufferMs = 60;  q.bitrateKbps = 128; q.frameMs = 10; q.redundancy = 1; break;
        case Preset::stable:     q.bufferMs = 300; q.bitrateKbps = 192; q.frameMs = 20; q.redundancy = 2; break;
        case Preset::balanced:
        case Preset::custom:
        default:                 q.bufferMs = 150; q.bitrateKbps = 160; q.frameMs = 20; q.redundancy = 1; break;
    }
    q.limiter = true;
    return q;
}

void GigmaAudioProcessor::applyQuality (Preset p, const GigmaAooEngine::Quality& q, bool saveDefault)
{
    currentPreset = p;
    aooEngine.setQuality (q);
    if (saveDefault)
        saveUserDefaults();
}

void GigmaAudioProcessor::setPreset (Preset p)
{
    if (p == Preset::custom)
        applyQuality (p, aooEngine.quality(), true);
    else
        applyQuality (p, presetQuality (p), true);
}

void GigmaAudioProcessor::setQuality (const GigmaAooEngine::Quality& q)
{
    auto p = Preset::custom;
    for (auto c : { Preset::lowLatency, Preset::balanced, Preset::stable })
        if (presetQuality (c) == q)
            p = c;
    applyQuality (p, q, true);
}

void GigmaAudioProcessor::loadUserDefaults()
{
    Preset p = Preset::balanced;
    GigmaAooEngine::Quality q = presetQuality (p);
    if (auto xml = juce::XmlDocument::parse (gigmaSettingsFile()))
        treeToQuality (juce::ValueTree::fromXml (*xml), p, q);
    applyQuality (p, q, false);
}

void GigmaAudioProcessor::saveUserDefaults()
{
    auto f = gigmaSettingsFile();
    f.getParentDirectory().createDirectory();
    if (auto xml = qualityToTree (currentPreset.load(), aooEngine.quality()).createXml())
        xml->writeTo (f);
}

GigmaAudioProcessor::~GigmaAudioProcessor()
{
    stopThread (10000);
    aooEngine.release();
}

GigmaSessionResult GigmaAudioProcessor::hostNewMeeting()
{
    auto result = sessionClient.createAndClaimHost ("DAW session", aooEngine.endpoint());
    if (result.ok)
    {
        aooEngine.setRelay (result.relay.toStdString(), result.sessionId.toStdString(),
                            result.hostId.toStdString());
        aooEngine.setPeerRelay (result.relay.toStdString(), result.sessionId.toStdString(),
                                result.relaySecret.toStdString());
        aooEngine.syncPeers ({});
        meetingUrl = result.meetingUrl;
        notify();
    }
    return result;
}

GigmaSessionResult GigmaAudioProcessor::joinMeeting (const juce::String& linkOrId)
{
    auto result = sessionClient.joinAsPlugin (linkOrId, juce::SystemStats::getComputerName(),
                                              aooEngine.endpoint());
    if (result.ok)
    {
        aooEngine.setRelay ({}, {}, {});       // guests don't feed the browser relay
        aooEngine.setRelayEnabled (false);
        aooEngine.setPeerRelay (result.relay.toStdString(), result.sessionId.toStdString(),
                                result.relaySecret.toStdString());
        std::set<std::string> wanted;
        const bool viaRelay = aooEngine.peerRelayConfigured() && result.hostPeerId.length() == 16
                              && juce::SystemStats::getEnvironmentVariable ("GIGMA_DIRECT", {}).isEmpty();
        if (viaRelay)
            wanted.insert ("relay:" + result.hostPeerId.toStdString());
        else if (result.hostEndpoint.isNotEmpty())
            wanted.insert (result.hostEndpoint.toStdString());
        aooEngine.syncPeers (wanted);          // send our audio back to the host
        meetingUrl = result.meetingUrl;
        notify();
    }
    return result;
}

GigmaAudioProcessor::Status GigmaAudioProcessor::status() const
{
    Status s;
    s.peers = statPeers.load();
    s.listeners = statListeners.load();
    s.relayOn = aooEngine.relayActive();
    s.brokerOk = statBrokerOk.load();
    s.hostLive = statHostLive.load();
    s.peerRelayOn = aooEngine.peerRelayConfigured();
    s.peerRelayUp = aooEngine.peerRelayUp();
    const auto st = aooEngine.stats();
    s.underruns = st.underruns; s.drops = st.drops; s.resends = st.resends;
    s.xruns = st.xruns; s.sendOverflows = st.sendOverflows;
    return s;
}

void GigmaAudioProcessor::run()
{
    while (! threadShouldExit())
    {
        syncWithBroker();
        wait (2000);
    }
}

void GigmaAudioProcessor::syncWithBroker()
{
    if (sessionClient.role() == GigmaSessionClient::Role::none)
        return;
    const auto ep = aooEngine.endpoint();
    auto hb = sessionClient.heartbeat (ep);
    statBrokerOk = hb.ok;
    if (! hb.ok)
        return;

    std::set<std::string> wanted;
    for (auto& p : hb.peers)
        wanted.insert (p.toStdString());
    aooEngine.syncPeers (wanted);
    statPeers = (int) aooEngine.numPeers();
    statListeners = hb.listeners;
    statHostLive = hb.hostLive;

    if (sessionClient.role() == GigmaSessionClient::Role::host)
    {
        if (hb.relay.isNotEmpty())
            aooEngine.setRelay (hb.relay.toStdString(),
                                sessionClient.currentSessionId().toStdString(),
                                sessionClient.currentHostId().toStdString());
        aooEngine.setRelayEnabled (hb.listeners > 0);
    }
}

void GigmaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    aooEngine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    try
    {
        aooEngine.startNetwork (lastUdpPort);
        lastUdpPort = aooEngine.port();
        notify();   // re-advertise endpoint + re-add peers right away
    }
    catch (...)
    {
        // Network may fail in offline render; audio path still processes locally.
    }
}

void GigmaAudioProcessor::releaseResources()
{
    aooEngine.release();
}

bool GigmaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}

void GigmaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto* in = buffer.getArrayOfReadPointers();
    auto* out = buffer.getArrayOfWritePointers();
    // Insert FX: capture DAW input → AOO send; output = dry input + remote peers.
    // The engine never mutes the channel (passes dry through when not ready).
    aooEngine.process (reinterpret_cast<const float* const*> (in),
                       reinterpret_cast<float* const*> (out),
                       buffer.getNumChannels(),
                       buffer.getNumSamples());
}

juce::AudioProcessorEditor* GigmaAudioProcessor::createEditor()
{
    return new GigmaAudioProcessorEditor (*this);
}

void GigmaAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state ("GigmaState");
    state.appendChild (qualityToTree (currentPreset.load(), aooEngine.quality()), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void GigmaAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml)
        return;
    Preset p; GigmaAooEngine::Quality q;
    if (treeToQuality (juce::ValueTree::fromXml (*xml).getChildWithName ("GigmaQuality"), p, q))
        applyQuality (p, q, false);   // project setting wins, don't touch the global default
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GigmaAudioProcessor();
}
