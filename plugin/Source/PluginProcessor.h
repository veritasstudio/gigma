#pragma once
#include <JuceHeader.h>
#include "GigmaSessionClient.h"
#include "GigmaAooEngine.h"

class GigmaAudioProcessor : public juce::AudioProcessor
{
public:
    GigmaAudioProcessor();
    ~GigmaAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Gigma"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    GigmaSessionClient& session() { return sessionClient; }
    GigmaAooEngine& aoo() { return aooEngine; }
    juce::String lastMeetingUrl() const { return meetingUrl; }
    void setLastMeetingUrl (juce::String u) { meetingUrl = std::move (u); }

private:
    GigmaSessionClient sessionClient;
    GigmaAooEngine aooEngine;
    juce::String meetingUrl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GigmaAudioProcessor)
};
