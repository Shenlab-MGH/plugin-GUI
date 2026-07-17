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

#pragma once

#include "AgentTransportEndpoint.h"
#include "AgentExperimentDirectoryEndpoint.h"
#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
#endif
#include "../Utils/httplib.h"
#if defined (__clang__)
#pragma clang diagnostic pop
#endif

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class AgentLoopbackServer final
{
public:
    AgentLoopbackServer (
        std::shared_ptr<AgentTransportEndpoint> endpoint,
        std::string bearerToken,
        int port = 37498,
        bool mutationEnabled = false,
        std::shared_ptr<AgentExperimentDirectoryEndpoint>
            directoryEndpoint = nullptr);

    ~AgentLoopbackServer();

    bool start();
    void stop();
    int getBoundPort() const;
    const std::string& getSessionId() const;

private:
    void run();
    void configureRoutes();

    bool authorize (
        const httplib::Request& request,
        httplib::Response& response) const;

    static bool constantTimeEqual (
        const std::string& lhs,
        const std::string& rhs);

    static void setJson (
        httplib::Response& response,
        int status,
        const std::string& body);
    static std::string createSessionId();

    std::shared_ptr<AgentTransportEndpoint> endpoint;
    std::shared_ptr<AgentExperimentDirectoryEndpoint>
        directoryEndpoint;
    std::string bearerToken;
    std::string sessionId;
    int requestedPort;
    bool mutationEnabled;
    std::atomic<int> boundPort { -1 };
    httplib::Server server;
    std::thread worker;
    std::atomic<bool> started { false };
};
