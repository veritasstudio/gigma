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

    // Audio quality
    juce::Label qualityLabel { {}, "Quality" }, bufferLabel { {}, "Buffer" },
                bitrateLabel { {}, "Bitrate" }, frameLabel { {}, "Frame" };
    juce::ComboBox presetBox, bufferBox, bitrateBox, frameBox;
    juce::ToggleButton limiterToggle { "Safety limiter" };
    juce::ToggleButton redundancyToggle { "Send twice (lossy Wi-Fi)" };
    bool updatingQualityUi = false;

    // Health readout: deltas over a sliding window
    GigmaAudioProcessor::Status lastStatus;
    struct Sample { uint64_t gaps = 0, resent = 0; };
    std::vector<Sample> window;   // one per timer tick (1 s)

    void createMeeting();
    void joinMeeting();
    void timerCallback() override;
    void refreshQualityUi();
    void qualityChangedFromUi();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GigmaAudioProcessorEditor)
};
