#include "PluginEditor.h"

GigmaAudioProcessorEditor::GigmaAudioProcessorEditor (GigmaAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (420, 220);
    addAndMakeVisible (newMeeting);
    addAndMakeVisible (linkBox);
    addAndMakeVisible (status);
    linkBox.setReadOnly (true);
    linkBox.setTextToShowWhenEmpty ("Meeting link appears here", juce::Colours::grey);
    status.setText ("Audio: AOO endpoint on this machine · broker for links only",
                    juce::dontSendNotification);
    if (processor.lastMeetingUrl().isNotEmpty())
        linkBox.setText (processor.lastMeetingUrl(), juce::dontSendNotification);
    newMeeting.onClick = [this] { createMeeting(); };
}

void GigmaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0f14));
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawText ("Gigma", getLocalBounds().removeFromTop (36).reduced (12, 8),
                juce::Justification::centredLeft);
}

void GigmaAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (36);
    newMeeting.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);
    linkBox.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);
    status.setBounds (r.removeFromTop (40));
}

void GigmaAudioProcessorEditor::createMeeting()
{
    status.setText ("Creating meeting…", juce::dontSendNotification);
    auto ep = processor.aoo().endpoint();
    auto result = processor.session().createAndClaimHost ("DAW session", ep);
    if (result.ok)
    {
        processor.setLastMeetingUrl (result.meetingUrl);
        linkBox.setText (result.meetingUrl, juce::dontSendNotification);
        status.setText ("Share the link — guests join in the browser\nAOO: " + ep,
                        juce::dontSendNotification);
    }
    else
    {
        status.setText ("Failed: " + result.error, juce::dontSendNotification);
    }
}
