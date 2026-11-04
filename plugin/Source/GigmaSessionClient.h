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
    juce::String hostEndpoint; // for plugin guests: the host's AOO endpoint (LAN)
    juce::String hostPeerId;   // for plugin guests: the host's public id on the AOO relay
    juce::String relaySecret;  // our secret on the AOO relay (host: hostId, guest: join token)
    juce::String error;
};

struct GigmaHeartbeat
{
    bool ok = false;
    juce::StringArray peers;   // AOO peers to addSink(): "relay:<id>" or LAN "ip:port"
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

    /** true (default): reach peers via the broker relay whenever it's offered,
        so it works across any NAT. false: LAN/VPN direct "ip:port" only
        (set GIGMA_DIRECT=1 in the DAW's environment). */
    void setUseRelay (bool on);

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
    bool useRelay = true;

    /** Relay id if we use the relay for it, else its LAN endpoint. */
    juce::String peerAddress (const juce::String& relay, const juce::String& id, const juce::String& lan) const;

    juce::String postJson (const juce::String& path, const juce::String& jsonBody, int& statusCode);
};
