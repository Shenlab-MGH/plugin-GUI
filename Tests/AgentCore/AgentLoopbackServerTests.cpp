#include "../../Source/Agent/AgentLoopbackServer.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit (1);
    }
}

class FakeExecutor final : public AgentTransportExecutor
{
public:
    AgentTransportApplyResult apply (
        const AgentTransportRequest& request) override
    {
        return {
            AgentTransportApplyOutcome::completed,
            {
                AgentTransportPlanOutcome::proposalReady,
                request.requestId,
                AgentObservedMode::idle,
                request.targetMode,
                request.expectedRevision,
                {}
            },
            {
                "1.0.2-agent-v0.0.1",
                request.targetMode,
                request.expectedRevision + 1
            }
        };
    }
};
}

int main()
{
    std::vector<std::function<void()>> scheduled;
    auto endpoint = AgentTransportEndpoint::create (
        [&scheduled] (std::function<void()> callback)
        {
            scheduled.push_back (std::move (callback));
            return true;
        },
        {
            "1.0.2-agent-v0.0.1",
            AgentObservedMode::idle,
            1
        });
    auto executor = std::make_shared<FakeExecutor>();
    endpoint->attachExecutor (executor);

    const std::string token (
        "0123456789abcdef0123456789abcdef");
    AgentLoopbackServer observeOnlyServer (
        endpoint,
        token,
        0,
        false);
    require (observeOnlyServer.start(),
             "The observe-only server must bind");
    const auto observePort =
        observeOnlyServer.getBoundPort();
    httplib::Client observeClient (
        "127.0.0.1",
        observePort);
    const httplib::Headers authorized {
        { "Authorization", "Bearer " + token }
    };
    const auto observeStatus =
        observeClient.Get ("/v1/status", authorized);
    require (
        observeStatus
            && observeStatus->body.find (
                   "\"mutation_allowed\":false")
                   != std::string::npos,
        "Observe-only status must disable mutation");
    const auto blocked = observeClient.Post (
        "/v1/transport/requests",
        authorized,
        "{}",
        "application/json");
    require (blocked && blocked->status == 403,
             "Observe-only POST must fail before parsing");
    observeOnlyServer.stop();

    AgentLoopbackServer server (endpoint, token, 0, true);
    require (server.start(),
             "The loopback server must bind for integration testing");
    const auto port = server.getBoundPort();
    require (port > 0,
             "Dynamic binding must expose the selected port");

    httplib::Client client ("127.0.0.1", port);
    const auto unauthorized = client.Get ("/v1/status");
    require (unauthorized && unauthorized->status == 401,
             "Status must reject a missing bearer token");

    const auto status = client.Get ("/v1/status", authorized);
    require (status && status->status == 200,
             "Authorized status must succeed");
    require (status->body.find ("\"phase\":\"READY\"")
                 != std::string::npos,
             "Status must come from the attached endpoint");

    const auto receipt = client.Post (
        "/v1/transport/requests",
        authorized,
        std::string (
            R"({"request_id":"http-cpp-1","expected_session_id":")")
            + server.getSessionId()
            + R"(","target_mode":"ACQUIRE","expected_revision":1})",
        "application/json");
    require (receipt && receipt->status == 202,
             "A valid target-state request must be accepted");
    require (receipt->body.find ("\"state\":\"PENDING\"")
                 != std::string::npos,
             "Accepted HTTP mutation must return PENDING");
    require (scheduled.size() == 1,
             "HTTP must schedule exactly one endpoint callback");

    const auto wrongSession = client.Post (
        "/v1/transport/requests",
        authorized,
        R"({"request_id":"wrong-session","expected_session_id":"old-session","target_mode":"IDLE","expected_revision":1})",
        "application/json");
    require (wrongSession && wrongSession->status == 409,
             "A request from a different process session must be rejected");

    scheduled.front()();

    const auto result = client.Get (
        "/v1/transport/requests/http-cpp-1",
        authorized);
    require (result && result->status == 200,
             "Completed request lookup must succeed");
    require (result->body.find ("\"state\":\"COMPLETED\"")
                 != std::string::npos,
             "Lookup must expose endpoint completion");
    require (result->body.find ("\"final_mode\":\"ACQUIRE\"")
                 != std::string::npos,
             "Lookup must expose verified final mode");

    server.stop();
    require (server.getBoundPort() == -1,
             "Stop must clear the bound port");

    for (int restart = 0; restart < 10; ++restart)
    {
        require (server.start(),
                 "A stopped server must support a clean restart");
        server.stop();
    }
    std::cout << "PASS AgentLoopbackServerTests\n";
    return 0;
}
