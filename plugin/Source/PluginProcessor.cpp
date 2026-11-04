#include "PluginProcessor.h"
#include "PluginEditor.h"

GigmaAudioProcessor::GigmaAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

GigmaAudioProcessor::~GigmaAudioProcessor() = default;

void GigmaAudioProcessor::prepareToPlay (double, int) {}
void GigmaAudioProcessor::releaseResources() {}

void GigmaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Scaffold: pass-through. Network I/O (SonoBus-class endpoint) lands here later.
    juce::ignoreUnused (buffer);
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
