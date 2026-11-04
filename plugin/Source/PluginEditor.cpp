#include "PluginEditor.h"

GigmaAudioProcessorEditor::GigmaAudioProcessorEditor (GigmaAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (460, 300);
    for (auto* c : std::initializer_list<juce::Component*> { &newMeeting, &linkBox, &copyLink, &joinBox, &joinButton, &status })
        addAndMakeVisible (c);

    linkBox.setReadOnly (true);
    linkBox.setTextToShowWhenEmpty ("Meeting link appears here", juce::Colours::grey);
    joinBox.setTextToShowWhenEmpty ("…or paste a Gigma link to join another producer", juce::Colours::grey);
    status.setJustificationType (juce::Justification::topLeft);
    status.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    if (processor.lastMeetingUrl().isNotEmpty())
        linkBox.setText (processor.lastMeetingUrl(), juce::dontSendNotification);

    newMeeting.onClick = [this] { createMeeting(); };
    joinButton.onClick = [this] { joinMeeting(); };
    joinBox.onReturnKey = [this] { joinMeeting(); };
    copyLink.onClick = [this]
    {
        if (linkBox.getText().isNotEmpty())
            juce::SystemClipboard::copyTextToClipboard (linkBox.getText());
    };

    timerCallback();
    startTimer (1000);
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
    newMeeting.setBounds (r.removeFromTop (30));
    r.removeFromTop (6);
    auto row = r.removeFromTop (30);
    copyLink.setBounds (row.removeFromRight (60));
    row.removeFromRight (6);
    linkBox.setBounds (row);
    r.removeFromTop (12);
    row = r.removeFromTop (30);
    joinButton.setBounds (row.removeFromRight (120));
    row.removeFromRight (6);
    joinBox.setBounds (row);
    r.removeFromTop (10);
    status.setBounds (r);
}

void GigmaAudioProcessorEditor::createMeeting()
{
    headline = "Creating meeting…";
    timerCallback();
    auto result = processor.hostNewMeeting();
    if (result.ok)
    {
        linkBox.setText (result.meetingUrl, juce::dontSendNotification);
        headline = "Hosting — share the link. Browser guests listen; plugin guests jam.";
    }
    else
        headline = "Failed: " + result.error;
    timerCallback();
}

void GigmaAudioProcessorEditor::joinMeeting()
{
    headline = "Joining…";
    timerCallback();
    auto result = processor.joinMeeting (joinBox.getText());
    if (result.ok)
    {
        linkBox.setText (result.meetingUrl, juce::dontSendNotification);
        headline = result.hostEndpoint.isNotEmpty() ? juce::String ("Joined — connected to host")
                                                    : juce::String ("Joined — waiting for host plugin");
    }
    else
        headline = "Join failed: " + result.error;
    timerCallback();
}

void GigmaAudioProcessorEditor::timerCallback()
{
    const auto s = processor.status();
    auto ep = juce::String (processor.aoo().endpoint());
    const auto role = processor.session().role();

    juce::String text = headline.isNotEmpty() ? headline + "\n" : juce::String();
    if (ep.isEmpty())
        text << "Audio: not running (press play / open a track)\n";
    else if (s.peerRelayOn)
        text << "Internet: " << (s.peerRelayUp ? "connected via Gigma relay" : "connecting to Gigma relay…")
             << "  ·  LAN " << ep << "\n";
    else
        text << "AOO (LAN/VPN direct): " << ep << "\n";
    if (role == GigmaSessionClient::Role::host)
        text << "Plugin peers: " << s.peers << "  ·  Browser listeners: " << s.listeners
             << (s.relayOn ? "  (relay on)" : "") << "\n";
    else if (role == GigmaSessionClient::Role::guest)
        text << "Host: " << (s.peers > 0 ? "connected" : "waiting") << "\n";
    if (role != GigmaSessionClient::Role::none && ! s.brokerOk)
        text << "Broker: reconnecting…\n";
    status.setText (text, juce::dontSendNotification);
}
