#include "PluginProcessor.h"
#include "PluginEditor.h"

GigmaAudioProcessor::GigmaAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

GigmaAudioProcessor::~GigmaAudioProcessor()
{
    aooEngine.release();
}

void GigmaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    aooEngine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    try
    {
        auto ep = aooEngine.startNetwork (0);
        juce::ignoreUnused (ep);
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
    // Capture DAW input → AOO send; AOO receive → DAW output (SonoBus-style).
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
