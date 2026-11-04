#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class GigmaAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit GigmaAudioProcessorEditor (GigmaAudioProcessor&);
    ~GigmaAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GigmaAudioProcessor& processor;
    juce::TextButton newMeeting { "New meeting (host)" };
    juce::TextEditor linkBox;
    juce::TextButton copyLink { "Copy" };
    juce::TextEditor joinBox;
    juce::TextButton joinButton { "Join as plugin" };
    juce::Label status;
    juce::String headline;

    void createMeeting();
    void joinMeeting();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GigmaAudioProcessorEditor)
};
