#include "PluginEditor.h"

namespace {
const int kBufferMs[] = { 40, 60, 100, 150, 200, 300, 500 };
const int kBitrates[] = { 64, 96, 128, 160, 192, 256 };
const int kFrames[]   = { 10, 20 };
}

GigmaAudioProcessorEditor::GigmaAudioProcessorEditor (GigmaAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (480, 430);
    for (auto* c : std::initializer_list<juce::Component*> { &newMeeting, &linkBox, &copyLink, &joinBox, &joinButton, &status,
                                                             &qualityLabel, &bufferLabel, &bitrateLabel, &frameLabel,
                                                             &presetBox, &bufferBox, &bitrateBox, &frameBox,
                                                             &limiterToggle, &redundancyToggle })
        addAndMakeVisible (c);

    linkBox.setReadOnly (true);
    linkBox.setTextToShowWhenEmpty ("Meeting link appears here", juce::Colours::grey);
    joinBox.setTextToShowWhenEmpty ("…or paste a Gigma link to join another producer", juce::Colours::grey);
    status.setJustificationType (juce::Justification::topLeft);
    status.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    for (auto* l : { &qualityLabel, &bufferLabel, &bitrateLabel, &frameLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        l->setJustificationType (juce::Justification::centredLeft);
    }

    auto names = GigmaAudioProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);
    for (auto ms : kBufferMs)  bufferBox.addItem (juce::String (ms) + " ms", ms);
    for (auto kb : kBitrates)  bitrateBox.addItem (juce::String (kb) + " kbps", kb);
    for (auto f : kFrames)     frameBox.addItem (juce::String (f) + " ms", f);

    presetBox.setTooltip ("Low latency: tight but needs a clean connection. Balanced: default for the internet relay. "
                          "Stable: biggest buffer, highest bitrate, every packet sent twice.");
    bufferBox.setTooltip ("Receive buffer for what YOU hear from others. Raise it if you hear crackles or dropouts.");
    bitrateBox.setTooltip ("Opus bitrate for what you SEND.");
    frameBox.setTooltip ("Opus frame size for what you send. 20 ms is more robust, 10 ms shaves latency.");
    limiterToggle.setTooltip ("Soft-limits only the copy sent to the meeting above -1 dBFS so hot synths don't clip. "
                              "Your own channel is untouched.");

    presetBox.onChange = [this]
    {
        if (updatingQualityUi) return;
        processor.setPreset ((GigmaAudioProcessor::Preset) (presetBox.getSelectedId() - 1));
        refreshQualityUi();
    };
    for (auto* b : { &bufferBox, &bitrateBox, &frameBox })
        b->onChange = [this] { qualityChangedFromUi(); };
    limiterToggle.onClick = [this] { qualityChangedFromUi(); };
    redundancyToggle.onClick = [this] { qualityChangedFromUi(); };

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

    refreshQualityUi();
    lastStatus = processor.status();
    timerCallback();
    startTimer (1000);
}

void GigmaAudioProcessorEditor::refreshQualityUi()
{
    const juce::ScopedValueSetter<bool> svs (updatingQualityUi, true);
    const auto q = processor.quality();
    presetBox.setSelectedId ((int) processor.preset() + 1, juce::dontSendNotification);
    // Snap to the nearest listed value so a saved odd value still shows something.
    auto nearest = [] (auto& arr, int v)
    {
        int best = arr[0];
        for (auto x : arr) if (std::abs (x - v) < std::abs (best - v)) best = x;
        return best;
    };
    bufferBox.setSelectedId (nearest (kBufferMs, q.bufferMs), juce::dontSendNotification);
    bitrateBox.setSelectedId (nearest (kBitrates, q.bitrateKbps), juce::dontSendNotification);
    frameBox.setSelectedId (nearest (kFrames, q.frameMs), juce::dontSendNotification);
    limiterToggle.setToggleState (q.limiter, juce::dontSendNotification);
    redundancyToggle.setToggleState (q.redundancy > 1, juce::dontSendNotification);
}

void GigmaAudioProcessorEditor::qualityChangedFromUi()
{
    if (updatingQualityUi) return;
    auto q = processor.quality();
    if (bufferBox.getSelectedId() > 0)  q.bufferMs = bufferBox.getSelectedId();
    if (bitrateBox.getSelectedId() > 0) q.bitrateKbps = bitrateBox.getSelectedId();
    if (frameBox.getSelectedId() > 0)   q.frameMs = frameBox.getSelectedId();
    q.limiter = limiterToggle.getToggleState();
    q.redundancy = redundancyToggle.getToggleState() ? 2 : 1;
    processor.setQuality (q);
    refreshQualityUi();
}

void GigmaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0f14));
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawText ("Gigma", getLocalBounds().removeFromTop (36).reduced (12, 8),
                juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff1c2530));
    g.drawHorizontalLine (186, 12.0f, (float) getWidth() - 12.0f);
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

    // Audio quality block
    r.removeFromTop (20);
    row = r.removeFromTop (26);
    qualityLabel.setBounds (row.removeFromLeft (60));
    presetBox.setBounds (row.removeFromLeft (170));
    row.removeFromLeft (10);
    limiterToggle.setBounds (row);
    r.removeFromTop (6);
    row = r.removeFromTop (26);
    bufferLabel.setBounds (row.removeFromLeft (60));
    bufferBox.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (10);
    bitrateLabel.setBounds (row.removeFromLeft (56));
    bitrateBox.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (10);
    frameLabel.setBounds (row.removeFromLeft (48));
    frameBox.setBounds (row);
    r.removeFromTop (4);
    redundancyToggle.setBounds (r.removeFromTop (24).withTrimmedLeft (60));
    r.removeFromTop (8);
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

    // Gaps heard on our side = underruns + dropped blocks + sender xruns. Counters reset
    // on re-prepare, so guard against going backwards.
    auto delta = [] (uint64_t now, uint64_t before) { return now >= before ? now - before : now; };
    Sample smp;
    smp.gaps = delta (s.underruns, lastStatus.underruns) + delta (s.drops, lastStatus.drops)
             + delta (s.xruns, lastStatus.xruns);
    smp.resent = delta (s.resends, lastStatus.resends);
    const auto sendOver = delta (s.sendOverflows, lastStatus.sendOverflows);
    lastStatus = s;
    window.push_back (smp);
    if (window.size() > 10)
        window.erase (window.begin());
    uint64_t gaps = 0, resent = 0;
    for (auto& w : window) { gaps += w.gaps; resent += w.resent; }

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
    if (role != GigmaSessionClient::Role::none && s.peers > 0)
    {
        text << "Link (last 10 s): " << (int) gaps << " dropouts, " << (int) resent << " recovered";
        if (gaps > 3)
            text << "  — raise Buffer or pick Stable";
        text << "\n";
    }
    if (sendOver > 0)
        text << "Send buffer overflowed — DAW is rendering in bursts; try a larger I/O buffer\n";
    status.setText (text, juce::dontSendNotification);
}
