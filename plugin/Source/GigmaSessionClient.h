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

class GigmaSessionClient
{
public:
    explicit GigmaSessionClient (juce::String brokerUrl = GIGMA_DEFAULT_BROKER_URL);

    GigmaSessionResult createAndClaimHost (const juce::String& title,
                                           const juce::String& aooEndpoint = {});
    bool heartbeat (const juce::String& aooEndpoint = {});

    juce::String currentSessionId() const { return sessionId; }
    juce::String currentMeetingUrl() const { return meetingUrl; }

private:
    juce::String brokerUrl;
    juce::String sessionId;
    juce::String hostId;
    juce::String meetingUrl;

    juce::String postJson (const juce::String& path, const juce::String& jsonBody, int& statusCode);
};
