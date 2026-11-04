#include "GigmaSessionClient.h"

GigmaSessionClient::GigmaSessionClient (juce::String url)
    : brokerUrl (std::move (url))
{
}

juce::String GigmaSessionClient::postJson (const juce::String& path, const juce::String& jsonBody, int& statusCode)
{
    juce::URL url (brokerUrl + path);
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (8000)
                    .withExtraHeaders ("Content-Type: application/json\r\n")
                    .withHttpRequestCmd ("POST")
                    .withDataToSubmit (jsonBody, "application/json");

    // JUCE version differences: keep scaffold compiling against JUCE 7/8 with curl enabled.
    if (auto stream = url.createInputStream (opts))
    {
        statusCode = stream->getStatusCode();
        return stream->readEntireStreamAsString();
    }
    statusCode = 0;
    return {};
}

GigmaSessionResult GigmaSessionClient::createAndClaimHost (const juce::String& title)
{
    GigmaSessionResult out;
    int status = 0;
    auto created = postJson ("/api/sessions",
                             "{\"title\":\"" + title.replace ("\"", "\\\"") + "\"}", status);
    if (status < 200 || status >= 300 || created.isEmpty())
    {
        out.error = "create session HTTP " + juce::String (status);
        return out;
    }

    auto createTree = juce::JSON::parse (created);
    sessionId = createTree.getProperty ("id", {}).toString();
    meetingUrl = createTree.getProperty ("url", {}).toString();
    if (sessionId.isEmpty())
    {
        out.error = "missing session id";
        return out;
    }

    auto claimed = postJson ("/api/sessions/" + sessionId + "/host",
                             "{\"label\":\"Gigma Plugin\"}", status);
    if (status < 200 || status >= 300)
    {
        out.error = "claim host HTTP " + juce::String (status);
        return out;
    }
    auto hostTree = juce::JSON::parse (claimed);
    hostId = hostTree.getProperty ("host", juce::var()).getProperty ("hostId", {}).toString();
    if (auto urlProp = hostTree.getProperty ("url", {}); urlProp.toString().isNotEmpty())
        meetingUrl = urlProp.toString();

    out.ok = true;
    out.sessionId = sessionId;
    out.hostId = hostId;
    out.meetingUrl = meetingUrl;
    return out;
}

bool GigmaSessionClient::heartbeat()
{
    if (sessionId.isEmpty() || hostId.isEmpty())
        return false;
    int status = 0;
    postJson ("/api/sessions/" + sessionId + "/host/heartbeat",
              "{\"hostId\":\"" + hostId + "\"}", status);
    return status >= 200 && status < 300;
}
