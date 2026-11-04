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
    startThread();
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

void GigmaAudioProcessor::getStateInformation (juce::MemoryBlock&) {}
void GigmaAudioProcessor::setStateInformation (const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GigmaAudioProcessor();
}
