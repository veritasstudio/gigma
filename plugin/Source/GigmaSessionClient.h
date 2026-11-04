#pragma once
#include <JuceHeader.h>

struct GigmaSessionResult
{
    bool ok = false;
    juce::String sessionId;
    juce::String hostId;
    juce::String guestId;
    juce::String meetingUrl;
    juce::String relay;        // "ip:port" of the broker's browser-audio UDP ingest
    juce::String hostEndpoint; // for plugin guests: the host's AOO endpoint
    juce::String error;
};

struct GigmaHeartbeat
{
    bool ok = false;
    juce::StringArray peers;   // AOO endpoints to addSink()
    int listeners = 0;         // browser guests on the relay
    juce::String relay;
    bool hostLive = false;     // guest mode
};

/** Talks to the Gigma broker. Thread-safe: UI creates/joins, worker heartbeats. */
class GigmaSessionClient
{
public:
    enum class Role { none, host, guest };

    explicit GigmaSessionClient (juce::String brokerUrl = GIGMA_DEFAULT_BROKER_URL);

    GigmaSessionResult createAndClaimHost (const juce::String& title,
                                           const juce::String& aooEndpoint = {});
    /** Join someone else's meeting as a plugin guest (link or bare id). */
    GigmaSessionResult joinAsPlugin (const juce::String& meetingLinkOrId,
                                     const juce::String& name,
                                     const juce::String& aooEndpoint);

    /** Host: refresh endpoint, get plugin peers + browser listener count.
        Guest: refresh endpoint, get host endpoint (returned in peers). */
    GigmaHeartbeat heartbeat (const juce::String& aooEndpoint = {});

    Role role() const;
    juce::String currentSessionId() const;
    juce::String currentHostId() const;
    juce::String currentMeetingUrl() const;
    void leave();

private:
    juce::String brokerUrl;
    mutable juce::CriticalSection lock;
    Role currentRole = Role::none;
    juce::String sessionId;
    juce::String hostId;
    juce::String guestId;
    juce::String meetingUrl;

    juce::String postJson (const juce::String& path, const juce::String& jsonBody, int& statusCode);
};
