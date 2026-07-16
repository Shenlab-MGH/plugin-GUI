/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ------------------------------------------------------------------
*/

#include "AgentLoopbackServer.h"

#include "AgentControlProtocol.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

AgentLoopbackServer::AgentLoopbackServer (
    std::shared_ptr<AgentTransportEndpoint> endpointToUse,
    std::string tokenToUse,
    int portToUse,
    bool mutationEnabledToUse)
    : endpoint (std::move (endpointToUse)),
      bearerToken (std::move (tokenToUse)),
      sessionId (createSessionId()),
      requestedPort (portToUse),
      mutationEnabled (mutationEnabledToUse)
{
    configureRoutes();
}

AgentLoopbackServer::~AgentLoopbackServer()
{
    stop();
}

bool AgentLoopbackServer::start()
{
    if (started.load())
        return true;

    if (worker.joinable())
        worker.join();

    if (! endpoint || bearerToken.size() < 32
        || requestedPort < 0 || requestedPort > 65535)
    {
        return false;
    }

    const auto selectedPort =
        requestedPort == 0
        ? server.bind_to_any_port ("127.0.0.1")
        : (server.bind_to_port (
               "127.0.0.1",
               requestedPort)
               ? requestedPort
               : -1);
    if (selectedPort < 0)
        return false;

    boundPort.store (selectedPort);
    started.store (true);
    worker = std::thread ([this]
    {
        run();
    });

    // cpp-httplib stop() only closes the socket after listen_internal()
    // marks the server running. Do not report a successful start before that
    // point, otherwise an immediate stop could miss the bound socket and hang
    // forever while joining the listener thread.
    while (! server.is_running() && started.load())
        std::this_thread::yield();

    if (! server.is_running())
    {
        if (worker.joinable())
            worker.join();
        boundPort.store (-1);
        return false;
    }

    return true;
}

void AgentLoopbackServer::stop()
{
    server.stop();
    if (worker.joinable())
        worker.join();
    started.store (false);
    boundPort.store (-1);
}

int AgentLoopbackServer::getBoundPort() const
{
    return boundPort.load();
}

const std::string& AgentLoopbackServer::getSessionId() const
{
    return sessionId;
}

void AgentLoopbackServer::run()
{
    server.listen_after_bind();
    started.store (false);
}

void AgentLoopbackServer::configureRoutes()
{
    server.Get ("/v1/status",
                [this] (const httplib::Request& request,
                        httplib::Response& response)
                {
                    if (! authorize (request, response))
                        return;

                    setJson (
                        response,
                        200,
                        AgentControlProtocol::serializeStatus (
                            endpoint->serviceSnapshot(),
                            sessionId,
                            mutationEnabled));
                });

    server.Get (R"(/v1/transport/requests/(.+))",
                [this] (const httplib::Request& request,
                        httplib::Response& response)
                {
                    if (! authorize (request, response))
                        return;

                    const auto requestId =
                        request.matches[1].str();
                    const auto lookup =
                        endpoint->query (requestId);
                    setJson (
                        response,
                        lookup.state
                                == AgentMailboxRequestState::unknown
                            ? 404
                            : 200,
                        AgentControlProtocol::serializeRequestLookup (
                            requestId,
                            lookup,
                            sessionId));
                });

    server.Post ("/v1/transport/requests",
                 [this] (const httplib::Request& request,
                         httplib::Response& response)
                 {
                     if (! authorize (request, response))
                         return;

                     if (! mutationEnabled)
                     {
                         setJson (
                             response,
                             403,
                             R"({"error":"MUTATION_NOT_ARMED"})");
                         return;
                     }

                     if (request.body.empty()
                         || request.body.size() > 16384)
                     {
                         setJson (
                             response,
                             400,
                             R"({"error":"INVALID_BODY"})");
                         return;
                     }

                     const auto parsed =
                         AgentControlProtocol::parseTransportRequest (
                             request.body);
                     if (! parsed.request)
                     {
                         setJson (
                             response,
                             400,
                             std::string ("{\"error\":\"")
                                 + parsed.error
                                 + "\"}");
                         return;
                     }

                     if (! constantTimeEqual (
                             parsed.expectedSessionId,
                             sessionId))
                     {
                         setJson (
                             response,
                             409,
                             R"({"error":"SESSION_MISMATCH"})");
                         return;
                     }

                     const auto outcome =
                         endpoint->submit (*parsed.request);
                     int status = 409;
                     if (outcome
                         == AgentMailboxSubmitOutcome::accepted)
                         status = 202;
                     else if (
                         outcome
                         == AgentMailboxSubmitOutcome::duplicate)
                         status = 200;
                     else if (
                         outcome
                             == AgentMailboxSubmitOutcome::invalidRequest)
                         status = 400;
                     else if (
                         outcome
                             == AgentMailboxSubmitOutcome::shuttingDown
                         || outcome
                             == AgentMailboxSubmitOutcome::unavailable)
                         status = 503;

                     setJson (
                         response,
                         status,
                         AgentControlProtocol::serializeSubmitReceipt (
                             parsed.request->requestId,
                             outcome,
                             sessionId));
                 });
}

bool AgentLoopbackServer::authorize (
    const httplib::Request& request,
    httplib::Response& response) const
{
    const auto supplied =
        request.get_header_value ("Authorization");
    const auto expected = "Bearer " + bearerToken;

    if (constantTimeEqual (supplied, expected))
        return true;

    setJson (
        response,
        401,
        R"({"error":"UNAUTHORIZED"})");
    return false;
}

bool AgentLoopbackServer::constantTimeEqual (
    const std::string& lhs,
    const std::string& rhs)
{
    const auto length = std::max (lhs.size(), rhs.size());
    std::size_t difference = lhs.size() ^ rhs.size();

    for (std::size_t index = 0; index < length; ++index)
    {
        const unsigned char left =
            index < lhs.size()
            ? static_cast<unsigned char> (lhs[index])
            : 0;
        const unsigned char right =
            index < rhs.size()
            ? static_cast<unsigned char> (rhs[index])
            : 0;
        difference |= left ^ right;
    }

    return difference == 0;
}

void AgentLoopbackServer::setJson (
    httplib::Response& response,
    int status,
    const std::string& body)
{
    response.status = status;
    response.set_header ("Cache-Control", "no-store");
    response.set_header (
        "X-Content-Type-Options",
        "nosniff");
    response.set_content (body, "application/json");
}

std::string AgentLoopbackServer::createSessionId()
{
    std::random_device random;
    std::ostringstream stream;
    stream << std::hex << std::setfill ('0');
    for (int index = 0; index < 4; ++index)
        stream << std::setw (8) << random();
    return stream.str();
}
