#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class GigmaAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GigmaAudioProcessorEditor (GigmaAudioProcessor&);
    ~GigmaAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GigmaAudioProcessor& processor;
    juce::TextButton newMeeting { "New meeting" };
    juce::TextEditor linkBox;
    juce::Label status;

    void createMeeting();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GigmaAudioProcessorEditor)
};
