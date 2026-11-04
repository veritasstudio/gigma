#pragma once
#include <JuceHeader.h>

struct GigmaSessionResult
{
    bool ok = false;
    juce::String sessionId;
    juce::String hostId;
    juce::String meetingUrl;
    juce::String error;
};

// Talks to the Go broker for meeting links / host claim only (not audio).
class GigmaSessionClient
{
public:
    explicit GigmaSessionClient (juce::String brokerUrl = GIGMA_DEFAULT_BROKER_URL);

    GigmaSessionResult createAndClaimHost (const juce::String& title);
    bool heartbeat();

    juce::String currentSessionId() const { return sessionId; }
    juce::String currentMeetingUrl() const { return meetingUrl; }

private:
    juce::String brokerUrl;
    juce::String sessionId;
    juce::String hostId;
    juce::String meetingUrl;

    juce::String postJson (const juce::String& path, const juce::String& jsonBody, int& statusCode);
};
