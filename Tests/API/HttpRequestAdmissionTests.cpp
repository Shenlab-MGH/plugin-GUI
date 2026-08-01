/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#include "../../Source/Utils/HttpRequestAdmission.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace
{
constexpr auto stoppingBody = R"({"error":{"code":"server_stopping","message":"The HTTP server is stopping."},"ok":false})";
using namespace std::chrono_literals;

httplib::Response sentinelResponse()
{
    httplib::Response response;
    response.version = "HTTP/1.1";
    response.status = 299;
    response.reason = "Sentinel";
    response.set_header ("X-Admission-Sentinel", "unchanged");
    response.set_content ("open-admission-sentinel", "text/plain");
    response.location = "/sentinel";
    return response;
}

void expectUnchanged (
    const httplib::Response& actual,
    const httplib::Response& expected)
{
    EXPECT_EQ (actual.version, expected.version);
    EXPECT_EQ (actual.status, expected.status);
    EXPECT_EQ (actual.reason, expected.reason);
    EXPECT_EQ (actual.headers, expected.headers);
    EXPECT_EQ (actual.body, expected.body);
    EXPECT_EQ (actual.location, expected.location);
}

bool hasStoppingResponse (const httplib::Response& response)
{
    return response.status == 503
           && response.get_header_value ("Content-Type")
                  == "application/json"
           && response.body == stoppingBody;
}

class BoundedWorkerBatch
{
public:
    BoundedWorkerBatch (int workerCount, std::function<void()> operation)
        : completion_ (std::make_shared<Completion> (workerCount))
    {
        try
        {
            for (int worker = 0; worker < workerCount; ++worker)
            {
                workers_.emplace_back (
                    [operation, completion = completion_]() mutable
                    {
                        CompletionSignal signal { completion };
                        try
                        {
                            operation();
                        }
                        catch (...)
                        {
                            completion->operationThrew = true;
                        }
                    });
            }
        }
        catch (...)
        {
            abortWithDiagnostic (
                "HttpRequestAdmissionTests: worker batch creation failed.\n");
        }
    }

    ~BoundedWorkerBatch()
    {
        for (const auto& worker : workers_)
        {
            if (worker.joinable())
            {
                abortWithDiagnostic (
                    "HttpRequestAdmissionTests: worker batch was not joined.\n");
            }
        }
    }

    bool waitThenJoin (std::chrono::milliseconds timeout)
    {
        if (completion_->allFinished.wait_for (timeout)
            != std::future_status::ready)
        {
            abortWithDiagnostic (
                "HttpRequestAdmissionTests: worker batch timed out.\n");
        }

        for (auto& worker : workers_)
            worker.join();
        return ! completion_->operationThrew.load();
    }

private:
    struct Completion
    {
        explicit Completion (int workerCount) : remaining (workerCount) {}

        std::atomic<int> remaining;
        std::atomic<bool> operationThrew { false };
        std::promise<void> completed;
        std::shared_future<void> allFinished = completed.get_future().share();
    };

    struct CompletionSignal
    {
        std::shared_ptr<Completion> completion;

        ~CompletionSignal()
        {
            if (completion->remaining.fetch_sub (1) == 1)
            {
                try
                {
                    completion->completed.set_value();
                }
                catch (...)
                {
                }
            }
        }
    };

    [[noreturn]] static void abortWithDiagnostic (const char* diagnostic)
    {
        std::fputs (diagnostic, stderr);
        std::fflush (stderr);
        std::abort();
    }

    std::shared_ptr<Completion> completion_;
    std::vector<std::thread> workers_;
};

struct ConcurrentAdmissionProbe
{
    std::shared_ptr<HttpRequestAdmission> admission =
        std::make_shared<HttpRequestAdmission>();
    std::promise<void> releaseClosers;
    std::shared_future<void> closersReleased =
        releaseClosers.get_future().share();
    std::atomic<int> observersThatDidNotSeeStoppingResponse { 0 };
};
} // namespace

TEST (HttpRequestAdmissionTests,
      FreshAdmissionLeavesTheRequestForRoutesAndDoesNotChangeTheResponse)
{
    HttpRequestAdmission admission;
    httplib::Request request;
    request.method = "GET";
    request.path = "/api/sentinel";
    auto response = sentinelResponse();
    const auto expectedResponse = response;

    EXPECT_TRUE (admission.isOpen());
    EXPECT_EQ (
        admission.handle (request, response),
        httplib::Server::HandlerResponse::Unhandled);
    expectUnchanged (response, expectedResponse);
}

TEST (HttpRequestAdmissionTests,
      ClosedAdmissionReturnsTheExactStoppingResponseForGetAndPutRequests)
{
    HttpRequestAdmission admission;
    admission.close();

    for (const auto* method : { "GET", "PUT" })
    {
        httplib::Request request;
        request.method = method;
        request.path = "/api/sentinel";
        if (request.method == "PUT")
        {
            request.body = "{\"sentinel\":true}";
            request.set_header ("Content-Type", "application/json");
        }

        auto response = sentinelResponse();

        EXPECT_EQ (
            admission.handle (request, response),
            httplib::Server::HandlerResponse::Handled);
        EXPECT_EQ (response.status, 503);
        EXPECT_EQ (
            response.get_header_value ("Content-Type"),
            "application/json");
        EXPECT_EQ (response.body, stoppingBody);
    }
}

TEST (HttpRequestAdmissionTests,
      ConcurrentCloseCallsAreIdempotentAndJoinedObserversSeeClosedAdmission)
{
    const auto probe = std::make_shared<ConcurrentAdmissionProbe>();
    BoundedWorkerBatch closers (
        16,
        [probe]
        {
            probe->closersReleased.wait();
            probe->admission->close();
        });

    probe->releaseClosers.set_value();
    ASSERT_TRUE (closers.waitThenJoin (1s));

    BoundedWorkerBatch observers (
        16,
        [probe]
        {
            httplib::Request request;
            request.method = "GET";
            request.path = "/api/sentinel";
            auto response = sentinelResponse();

            if (probe->admission->handle (request, response)
                    != httplib::Server::HandlerResponse::Handled
                || ! hasStoppingResponse (response))
            {
                ++probe->observersThatDidNotSeeStoppingResponse;
            }
        });

    ASSERT_TRUE (observers.waitThenJoin (1s));
    EXPECT_EQ (probe->observersThatDidNotSeeStoppingResponse.load(), 0);
}
