#include "GigmaSessionClient.h"

namespace {
juce::String jsonStr (const juce::String& s)
{
    return juce::JSON::toString (juce::var (s));
}
}

GigmaSessionClient::GigmaSessionClient (juce::String url)
    : brokerUrl (std::move (url))
{
}

void GigmaSessionClient::setUseRelay (bool on)
{
    const juce::ScopedLock sl (lock);
    useRelay = on;
}

juce::String GigmaSessionClient::peerAddress (const juce::String& relay, const juce::String& id,
                                              const juce::String& lan) const
{
    bool relayOn;
    {
        const juce::ScopedLock sl (lock);
        relayOn = useRelay;
    }
    if (relayOn && relay.isNotEmpty() && id.length() == 16)
        return "relay:" + id;
    return lan;
}

juce::String GigmaSessionClient::postJson (const juce::String& path, const juce::String& jsonBody, int& statusCode)
{
    statusCode = 0;
    auto url = juce::URL (brokerUrl + path).withPOSTData (jsonBody);
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withConnectionTimeoutMs (8000)
                    .withExtraHeaders ("Content-Type: application/json\r\n")
                    .withHttpRequestCmd ("POST")
                    .withStatusCode (&statusCode);

    if (auto stream = url.createInputStream (opts))
        return stream->readEntireStreamAsString();
    statusCode = 0;
    return {};
}

GigmaSessionResult GigmaSessionClient::createAndClaimHost (const juce::String& title,
                                                           const juce::String& aooEndpoint)
{
    GigmaSessionResult out;
    int status = 0;
    auto created = postJson ("/api/sessions", "{\"title\":" + jsonStr (title) + "}", status);
    if (status < 200 || status >= 300 || created.isEmpty())
    {
        out.error = "create session HTTP " + juce::String (status);
        return out;
    }

    auto createTree = juce::JSON::parse (created);
    auto sid = createTree.getProperty ("id", {}).toString();
    auto murl = createTree.getProperty ("url", {}).toString();
    if (sid.isEmpty())
    {
        out.error = "missing session id";
        return out;
    }

    juce::String body = "{\"label\":\"Gigma Plugin\"";
    if (aooEndpoint.isNotEmpty())
        body += ",\"aooEndpoint\":" + jsonStr (aooEndpoint);
    body += "}";

    auto claimed = postJson ("/api/sessions/" + sid + "/host", body, status);
    if (status < 200 || status >= 300)
    {
        out.error = "claim host HTTP " + juce::String (status);
        return out;
    }
    auto hostTree = juce::JSON::parse (claimed);
    auto hid = hostTree.getProperty ("host", juce::var()).getProperty ("hostId", {}).toString();
    if (auto urlProp = hostTree.getProperty ("url", {}).toString(); urlProp.isNotEmpty())
        murl = urlProp;

    {
        const juce::ScopedLock sl (lock);
        currentRole = Role::host;
        sessionId = sid;
        hostId = hid;
        guestId = {};
        meetingUrl = murl;
    }

    out.ok = true;
    out.sessionId = sid;
    out.hostId = hid;
    out.meetingUrl = murl;
    out.relay = hostTree.getProperty ("relay", {}).toString();
    out.relaySecret = hid;
    return out;
}

GigmaSessionResult GigmaSessionClient::joinAsPlugin (const juce::String& linkOrId,
                                                     const juce::String& name,
                                                     const juce::String& aooEndpoint)
{
    GigmaSessionResult out;
    auto sid = linkOrId.trim();
    if (sid.contains ("/m/"))
        sid = sid.fromLastOccurrenceOf ("/m/", false, false).upToFirstOccurrenceOf ("/", false, false)
                 .upToFirstOccurrenceOf ("?", false, false);
    if (sid.isEmpty())
    {
        out.error = "paste a meeting link";
        return out;
    }

    int status = 0;
    juce::String body = "{\"name\":" + jsonStr (name.isNotEmpty() ? name : juce::String ("Gigma Plugin"))
                      + ",\"kind\":\"plugin\",\"aooEndpoint\":" + jsonStr (aooEndpoint) + "}";
    auto res = postJson ("/api/sessions/" + sid + "/join", body, status);
    if (status < 200 || status >= 300)
    {
        out.error = status == 404 ? juce::String ("meeting not found") : "join HTTP " + juce::String (status);
        return out;
    }
    auto tree = juce::JSON::parse (res);
    auto gid = tree.getProperty ("guest", juce::var()).getProperty ("id", {}).toString();

    {
        const juce::ScopedLock sl (lock);
        currentRole = Role::guest;
        sessionId = sid;
        hostId = {};
        guestId = gid;
        meetingUrl = brokerUrl + "/m/" + sid;
    }
    out.ok = true;
    out.sessionId = sid;
    out.guestId = gid;
    out.meetingUrl = brokerUrl + "/m/" + sid;
    out.hostEndpoint = tree.getProperty ("aooEndpoint", {}).toString();
    out.hostPeerId = tree.getProperty ("hostPeerId", {}).toString();
    out.relaySecret = tree.getProperty ("token", {}).toString();
    out.relay = tree.getProperty ("relay", {}).toString();
    return out;
}

GigmaHeartbeat GigmaSessionClient::heartbeat (const juce::String& aooEndpoint)
{
    GigmaHeartbeat hb;
    Role r; juce::String sid, hid, gid;
    {
        const juce::ScopedLock sl (lock);
        r = currentRole; sid = sessionId; hid = hostId; gid = guestId;
    }
    if (r == Role::none || sid.isEmpty())
        return hb;

    int status = 0;
    juce::String res;
    if (r == Role::host)
    {
        juce::String body = "{\"hostId\":" + jsonStr (hid);
        if (aooEndpoint.isNotEmpty())
            body += ",\"aooEndpoint\":" + jsonStr (aooEndpoint);
        body += "}";
        res = postJson ("/api/sessions/" + sid + "/host/heartbeat", body, status);
    }
    else
    {
        juce::String body = "{\"aooEndpoint\":" + jsonStr (aooEndpoint) + "}";
        res = postJson ("/api/sessions/" + sid + "/guests/" + gid + "/heartbeat", body, status);
    }
    if (status < 200 || status >= 300)
        return hb;

    auto tree = juce::JSON::parse (res);
    hb.ok = true;
    hb.listeners = (int) tree.getProperty ("listeners", 0);
    hb.relay = tree.getProperty ("relay", {}).toString();
    hb.hostLive = (bool) tree.getProperty ("hostLive", false);
    if (r == Role::host)
    {
        if (auto* arr = tree.getProperty ("peers", juce::var()).getArray())
            for (auto& p : *arr)
                if (auto ep = peerAddress (hb.relay, p.getProperty ("id", {}).toString(),
                                           p.getProperty ("aooEndpoint", {}).toString()); ep.isNotEmpty())
                    hb.peers.addIfNotAlreadyThere (ep);
    }
    else if (auto ep = peerAddress (hb.relay, tree.getProperty ("hostPeerId", {}).toString(),
                                    tree.getProperty ("aooEndpoint", {}).toString()); ep.isNotEmpty())
    {
        hb.peers.add (ep);
    }
    return hb;
}

GigmaSessionClient::Role GigmaSessionClient::role() const { const juce::ScopedLock sl (lock); return currentRole; }
juce::String GigmaSessionClient::currentSessionId() const { const juce::ScopedLock sl (lock); return sessionId; }
juce::String GigmaSessionClient::currentHostId() const { const juce::ScopedLock sl (lock); return hostId; }
juce::String GigmaSessionClient::currentMeetingUrl() const { const juce::ScopedLock sl (lock); return meetingUrl; }

void GigmaSessionClient::leave()
{
    const juce::ScopedLock sl (lock);
    currentRole = Role::none;
    sessionId = hostId = guestId = meetingUrl = {};
}
