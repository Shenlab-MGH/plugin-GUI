/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#pragma once

#include <atomic>
#include <memory>

#include "HttpServerLifecycle.h"
#include "httplib.h"

class HttpRequestAdmission
{
public:
    bool isOpen() const noexcept
    {
        return open_.load (std::memory_order_acquire);
    }

    void close() noexcept
    {
        open_.store (false, std::memory_order_release);
    }

    httplib::Server::HandlerResponse handle (
        const httplib::Request&,
        httplib::Response& response) const
    {
        if (isOpen())
            return httplib::Server::HandlerResponse::Unhandled;

        response.status = 503;
        response.set_content (
            R"({"error":{"code":"server_stopping","message":"The HTTP server is stopping."},"ok":false})",
            "application/json");
        return httplib::Server::HandlerResponse::Handled;
    }

private:
    std::atomic<bool> open_ { true };
};

inline void bindHttpRequestAdmission (
    httplib::Server& server,
    HttpServerLifecycle::Listener& listener)
{
    const auto admission = std::make_shared<HttpRequestAdmission>();
    server.set_pre_routing_handler (
        [admission] (const httplib::Request& request, httplib::Response& response)
        {
            return admission->handle (request, response);
        });
    listener.closeAdmission = [admission] { admission->close(); };
}
