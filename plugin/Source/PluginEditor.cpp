#include "PluginEditor.h"

GigmaAudioProcessorEditor::GigmaAudioProcessorEditor (GigmaAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (420, 200);
    addAndMakeVisible (newMeeting);
    addAndMakeVisible (linkBox);
    addAndMakeVisible (status);
    linkBox.setReadOnly (true);
    linkBox.setTextToShowWhenEmpty ("Meeting link appears here", juce::Colours::grey);
    status.setText ("Broker: session links only — audio stays in this plugin", juce::dontSendNotification);
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
    status.setBounds (r.removeFromTop (24));
}

void GigmaAudioProcessorEditor::createMeeting()
{
    status.setText ("Creating meeting…", juce::dontSendNotification);
    // Network call should move off the message thread; scaffold keeps it simple.
    auto result = processor.session().createAndClaimHost ("DAW session");
    if (result.ok)
    {
        linkBox.setText (result.meetingUrl, juce::dontSendNotification);
        status.setText ("Share the link — guests join in the browser", juce::dontSendNotification);
    }
    else
    {
        status.setText ("Failed: " + result.error, juce::dontSendNotification);
    }
}
