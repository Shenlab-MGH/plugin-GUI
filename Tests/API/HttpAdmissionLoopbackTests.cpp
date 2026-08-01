/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#include "../../Source/Utils/HttpRequestAdmission.h"
#include "../../Source/Utils/HttpServerLifecycle.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <utility>

namespace
{
using namespace std::chrono_literals;

constexpr auto probeStatus = 299;
constexpr auto probeBody = "open-admission-sentinel";
constexpr auto probeContentType = "text/plain";
constexpr auto stoppingStatus = 503;
constexpr auto stoppingBody = R"({"error":{"code":"server_stopping","message":"The HTTP server is stopping."},"ok":false})";
constexpr auto stoppingContentType = "application/json";

[[noreturn]] void abortWithDiagnostic (const char* diagnostic)
{
    std::fputs (diagnostic, stderr);
    std::fflush (stderr);
    std::abort();
}

template <typename Callback>
class ScopeExit
{
public:
    explicit ScopeExit (Callback callback) : callback_ (std::move (callback)) {}

    ~ScopeExit()
    {
        if (active_)
            callback_();
    }

    void dismiss() noexcept { active_ = false; }

private:
    Callback callback_;
    bool active_ = true;
};

template <typename Callback>
ScopeExit<Callback> makeScopeExit (Callback callback)
{
    return ScopeExit<Callback> (std::move (callback));
}

bool waitUntil (
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (! predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        std::this_thread::yield();
    }

    return true;
}

struct AdmissionCloseProbe
{
    std::promise<void> admissionClosedPromise;
    std::shared_future<void> admissionClosed =
        admissionClosedPromise.get_future().share();
    std::promise<void> releaseCloseHookPromise;
    std::shared_future<void> releaseCloseHook =
        releaseCloseHookPromise.get_future().share();
    std::atomic<bool> closeHookReleased { false };
};

void releaseCloseHookOrAbort (const std::shared_ptr<AdmissionCloseProbe>& probe)
{
    if (probe->closeHookReleased.exchange (true))
        return;

    try
    {
        probe->releaseCloseHookPromise.set_value();
    }
    catch (...)
    {
        abortWithDiagnostic (
            "HttpAdmissionLoopbackTests: close-hook release failed.\n");
    }
}

struct StopThreadProbe
{
    std::atomic<bool> started { false };
    std::atomic<bool> stopReturnedTrue { false };
    std::atomic<bool> stopThrew { false };
    std::promise<void> completionPromise;
    std::shared_future<void> completion =
        completionPromise.get_future().share();
    std::thread thread;
};

struct RouteProbe
{
    std::atomic<int> calls { 0 };
};

void startStopThreadOrAbort (
    const std::shared_ptr<StopThreadProbe>& probe,
    HttpServerLifecycle& lifecycle)
{
    if (probe->started.exchange (true))
        return;

    try
    {
        probe->thread = std::thread (
            [probe, &lifecycle]
            {
                try
                {
                    probe->stopReturnedTrue = lifecycle.stop();
                }
                catch (...)
                {
                    probe->stopThrew = true;
                }

                try
                {
                    probe->completionPromise.set_value();
                }
                catch (...)
                {
                    std::abort();
                }
            });
    }
    catch (...)
    {
        abortWithDiagnostic (
            "HttpAdmissionLoopbackTests: stop thread creation failed.\n");
    }
}

void waitForStopThenJoinOrAbort (
    const std::shared_ptr<StopThreadProbe>& probe,
    std::chrono::milliseconds timeout)
{
    if (! probe->started.load() || ! probe->thread.joinable())
    {
        abortWithDiagnostic (
            "HttpAdmissionLoopbackTests: no joinable stop thread.\n");
    }

    if (probe->completion.wait_for (timeout) != std::future_status::ready)
    {
        // C++17 has no safe cancellation or timed join. Continuing would
        // destroy a live lifecycle/listener, so fail closed instead.
        abortWithDiagnostic (
            "HttpAdmissionLoopbackTests: bounded lifecycle stop timed out; aborting rather than destroying a live thread.\n");
    }

    probe->thread.join();
    if (probe->stopThrew.load() || ! probe->stopReturnedTrue.load())
    {
        abortWithDiagnostic (
            "HttpAdmissionLoopbackTests: lifecycle stop did not finish cleanly.\n");
    }
}

} // namespace

TEST (HttpAdmissionLoopbackTests,
      ClosingTheBoundListenerRejectsLoopbackRequestsBeforeRoutesAndTransportStop)
{
    auto server = std::make_shared<httplib::Server>();
    const auto port = server->bind_to_any_port ("127.0.0.1");
    ASSERT_GT (port, 0);

    auto routeProbe = std::make_shared<RouteProbe>();
    auto listener = std::shared_ptr<HttpServerLifecycle::Listener>();
    auto closeProbe = std::make_shared<AdmissionCloseProbe>();
    auto stopProbe = std::make_shared<StopThreadProbe>();
    HttpServerLifecycle lifecycle (
        [&]
        {
            auto created = std::make_shared<HttpServerLifecycle::Listener>();
            created->listen = [server] { return server->listen_after_bind(); };
            created->isRunning = [server] { return server->is_running(); };
            created->stop = [server] { server->stop(); };

            // This is the production transport seam. The test must not
            // install its own gate or pre-routing handler.
            bindHttpRequestAdmission (*server, *created);

            created->registerRoutes = [routeProbe, server] (MessageThreadCallGeneration&)
            {
                server->Get (
                    "/probe",
                    [routeProbe] (const httplib::Request&, httplib::Response& response)
                    {
                        ++routeProbe->calls;
                        response.status = probeStatus;
                        response.set_content (probeBody, probeContentType);
                    });
            };
            listener = created;
            return created;
        },
        2s);

    auto cleanup = makeScopeExit (
        [&]
        {
            releaseCloseHookOrAbort (closeProbe);
            startStopThreadOrAbort (stopProbe, lifecycle);
            waitForStopThenJoinOrAbort (stopProbe, 2s);
        });

    lifecycle.start();
    ASSERT_TRUE (lifecycle.waitForState (ListenerStartGate::State::committed, 2s));
    ASSERT_TRUE (listener);
    ASSERT_TRUE (waitUntil ([server] { return server->is_running(); }, 2s));

    httplib::Client client ("127.0.0.1", port);
    client.set_connection_timeout (2, 0);
    client.set_read_timeout (2, 0);
    client.set_write_timeout (2, 0);
    const auto preCloseResponse = client.Get ("/probe");
    ASSERT_TRUE (preCloseResponse);
    EXPECT_EQ (preCloseResponse->status, probeStatus);
    EXPECT_EQ (
        preCloseResponse->get_header_value ("Content-Type"),
        probeContentType);
    EXPECT_EQ (preCloseResponse->body, probeBody);
    ASSERT_EQ (routeProbe->calls.load(), 1);

    const auto realCloseAdmission = listener->closeAdmission;
    ASSERT_TRUE (static_cast<bool> (realCloseAdmission));
    listener->closeAdmission = [realCloseAdmission, closeProbe]
    {
        realCloseAdmission();
        try
        {
            closeProbe->admissionClosedPromise.set_value();
        }
        catch (...)
        {
            std::abort();
        }
        closeProbe->releaseCloseHook.wait();
    };

    startStopThreadOrAbort (stopProbe, lifecycle);
    ASSERT_EQ (closeProbe->admissionClosed.wait_for (2s), std::future_status::ready);

    const auto closedGetResponse = client.Get ("/probe");
    ASSERT_TRUE (closedGetResponse);
    EXPECT_EQ (closedGetResponse->status, stoppingStatus);
    EXPECT_EQ (
        closedGetResponse->get_header_value ("Content-Type"),
        stoppingContentType);
    EXPECT_EQ (closedGetResponse->body, stoppingBody);

    const auto closedPutResponse = client.Put ("/probe");
    ASSERT_TRUE (closedPutResponse);
    EXPECT_EQ (closedPutResponse->status, stoppingStatus);
    EXPECT_EQ (
        closedPutResponse->get_header_value ("Content-Type"),
        stoppingContentType);
    EXPECT_EQ (closedPutResponse->body, stoppingBody);
    EXPECT_EQ (routeProbe->calls.load(), 1);

    releaseCloseHookOrAbort (closeProbe);
    waitForStopThenJoinOrAbort (stopProbe, 2s);
    cleanup.dismiss();
}
