/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#include "../../Source/Audio/AudioComponent.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/Processors/Settings/DataStream.h"

#include "../../Source/Processors/GenericProcessor/GenericProcessor.h"

#define private public
#include "../../Source/Utils/OpenEphysHttpServer.h"
#undef private

#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

// registerRoutes is an inline test seam that references every route. These
// unrelated application symbols are not exported by gui_testable_source and
// are never invoked by the parameter-route requests below.
namespace AccessClass
{
AudioComponent* getAudioComponent() { return nullptr; }
} // namespace AccessClass

namespace ProcessorManager
{
Array<String> getAvailableProcessors() { return {}; }
Plugin::Description getPluginDescription (String) { return {}; }
} // namespace ProcessorManager

AddProcessor::AddProcessor (Plugin::Description,
                            GenericProcessor*,
                            GenericProcessor*,
                            bool)
{
}
AddProcessor::~AddProcessor() = default;
bool AddProcessor::perform() { return false; }
bool AddProcessor::undo() { return false; }

DeleteProcessor::DeleteProcessor (GenericProcessor*) {}
DeleteProcessor::~DeleteProcessor() = default;
bool DeleteProcessor::perform() { return false; }
bool DeleteProcessor::undo() { return false; }

namespace
{
using json = nlohmann::json;
using namespace std::chrono_literals;

struct UnsafeRouteName
{
    const char* encoded;
    const char* decoded;
    int expectedStatus;
};

const std::array<UnsafeRouteName, 13> unsafeRouteNames { {
    { "%2F", "/", 404 },
    { "%5C", "\\", 400 },
    { "%2E", ".", 400 },
    { "%2E%2E", "..", 400 },
    { "%", "%", 400 },
    { "%0A", "\n", 400 },
    { "%C2%85", "\xC2\x85", 400 },
    { "%80", nullptr, 400 },
    { "%C2", nullptr, 400 },
    { "%C0%AF", nullptr, 400 },
    { "%ED%A0%80", nullptr, 400 },
    { "%F4%90%80%80", nullptr, 400 },
    { "", "", 404 },
} };

class RouteTestIntParameter : public IntParameter
{
public:
    using IntParameter::IntParameter;

    void setNextValue (var value, bool) override
    {
        ++mutationCalls;
        currentValue = static_cast<int> (value);
    }

    int mutationCalls = 0;
};

class RouteTestProcessor : public GenericProcessor
{
public:
    RouteTestProcessor() : GenericProcessor ("Route test processor", true) {}

    void process (AudioBuffer<float>&) override {}

    void addRouteTestStream (std::unique_ptr<DataStream> stream)
    {
        dataStreams.add (stream.release());
        updateChannelIndexMaps();
    }
};

class LoopbackParameterServer
{
public:
    LoopbackParameterServer (OpenEphysHttpServer& server,
                             MessageThreadCallGeneration& generation)
    {
        server.registerRoutes (httpServer, generation);
        port = httpServer.bind_to_any_port ("127.0.0.1");
        if (port <= 0)
            throw std::runtime_error ("Could not bind the parameter route test server to loopback.");

        listener = std::thread ([this] { httpServer.listen_after_bind(); });
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (! httpServer.is_running() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();

        if (! httpServer.is_running())
        {
            httpServer.stop();
            listener.join();
            throw std::runtime_error ("The parameter route test server did not start in time.");
        }
    }

    ~LoopbackParameterServer()
    {
        httpServer.stop();
        if (listener.joinable())
            listener.join();
    }

    httplib::Client client() const
    {
        return httplib::Client ("127.0.0.1", port);
    }

private:
    httplib::Server httpServer;
    int port = -1;
    std::thread listener;
};

class ParameterRouteSegmentTests : public testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        processor = std::make_unique<RouteTestProcessor>();
        processor->setNodeId (101);
        processor->addParameter (new RouteTestIntParameter (
            processor.get(),
            Parameter::PROCESSOR_SCOPE,
            "gain mode",
            "Gain mode",
            "A processor parameter with a space.",
            1,
            0,
            10));
        processor->addParameter (new RouteTestIntParameter (
            processor.get(),
            Parameter::PROCESSOR_SCOPE,
            "gain-mode_1.0",
            "Legacy gain mode",
            "A legacy processor parameter name.",
            2,
            0,
            10));

        for (const auto& name : unsafeRouteNames)
            if (name.decoded != nullptr)
                processor->addParameter (new RouteTestIntParameter (
                    processor.get(),
                    Parameter::PROCESSOR_SCOPE,
                    name.decoded,
                    "Unsafe route test parameter",
                    "Must not be reachable through a parameter route.",
                    5,
                    0,
                    10));

        auto stream = std::make_unique<DataStream> (DataStream::Settings {
            "Route test stream", "", "route.test.stream", 30000.0f, false });
        stream->addParameter (new RouteTestIntParameter (
            stream.get(),
            Parameter::STREAM_SCOPE,
            "gain mode",
            "Stream gain mode",
            "A stream parameter with a space.",
            3,
            0,
            10));
        stream->addParameter (new RouteTestIntParameter (
            stream.get(),
            Parameter::STREAM_SCOPE,
            "gain-mode_1.0",
            "Legacy stream gain mode",
            "A legacy stream parameter name.",
            4,
            0,
            10));


        for (const auto& name : unsafeRouteNames)
            if (name.decoded != nullptr)
                stream->addParameter (new RouteTestIntParameter (
                    stream.get(),
                    Parameter::STREAM_SCOPE,
                    name.decoded,
                    "Unsafe stream route test parameter",
                    "Must not be reachable through a parameter route.",
                    6,
                    0,
                    10));

        streamId = stream->getStreamId();
        processor->addRouteTestStream (std::move (stream));

        auto added = graph.addNode (
            std::unique_ptr<AudioProcessor> (processor.release()),
            AudioProcessorGraph::NodeID (101));
        ASSERT_NE (added, nullptr);
        processorInGraph = static_cast<RouteTestProcessor*> (added->getProcessor());
        server = std::make_unique<OpenEphysHttpServer> (audio, graph);
        loopback = std::make_unique<LoopbackParameterServer> (*server, generation);
    }

    void TearDown() override
    {
        loopback.reset();
        server.reset();
    }

    httplib::Result putWhilePumpingMessageThread (
        const std::string& path,
        const std::string& body)
    {
        auto client = loopback->client();
        client.set_connection_timeout (2s);
        client.set_read_timeout (2s);
        client.set_write_timeout (2s);
        auto response = std::async (
            std::launch::async,
            [&client, &path, &body]
            {
                return client.Put (path.c_str(), body, "application/json");
            });
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        bool readyBeforeDeadline = false;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (response.wait_for (0ms) == std::future_status::ready)
            {
                readyBeforeDeadline = true;
                break;
            }
            MessageManager::getInstance()->runDispatchLoopUntil (1);
        }

        if (! readyBeforeDeadline)
        {
            client.stop();
            throw std::runtime_error ("Parameter PUT did not complete before the test deadline.");
        }

        return response.get();
    }

    AudioComponent audio;
    ProcessorGraph graph { true };
    MessageThreadCallGeneration generation;
    std::unique_ptr<RouteTestProcessor> processor;
    RouteTestProcessor* processorInGraph = nullptr;
    uint16 streamId = 0;
    std::unique_ptr<OpenEphysHttpServer> server;
    std::unique_ptr<LoopbackParameterServer> loopback;
};

TEST_F (ParameterRouteSegmentTests,
        DecodedSpaceAndLegacyNamesCanReadAndWriteProcessorAndStreamParameters)
{
    auto client = loopback->client();

    const auto processorList = client.Get ("/api/processors/101/parameters");
    ASSERT_TRUE (processorList);
    ASSERT_EQ (processorList->status, 200);
    const auto listedProcessorParameter = json::parse (processorList->body)["parameters"].at (0);
    const auto processorParameterName = listedProcessorParameter["name"].get<std::string>();
    ASSERT_EQ (processorParameterName, "gain mode");
    EXPECT_NE (listedProcessorParameter["key"].get<std::string>(), processorParameterName);

    auto encodedProcessorParameterName = processorParameterName;
    const auto processorSpace = encodedProcessorParameterName.find (' ');
    ASSERT_NE (processorSpace, std::string::npos);
    encodedProcessorParameterName.replace (processorSpace, 1, "%20");
    const auto processorParameterPath =
        "/api/processors/101/parameters/" + encodedProcessorParameterName;

    const auto streamList = client.Get ("/api/processors/101/streams/0/parameters");
    ASSERT_TRUE (streamList);
    ASSERT_EQ (streamList->status, 200);
    const auto listedStreamParameter = json::parse (streamList->body)["parameters"].at (0);
    const auto streamParameterName = listedStreamParameter["name"].get<std::string>();
    ASSERT_EQ (streamParameterName, "gain mode");
    EXPECT_NE (listedStreamParameter["key"].get<std::string>(), streamParameterName);

    auto encodedStreamParameterName = streamParameterName;
    const auto streamSpace = encodedStreamParameterName.find (' ');
    ASSERT_NE (streamSpace, std::string::npos);
    encodedStreamParameterName.replace (streamSpace, 1, "%20");
    const auto streamParameterPath =
        "/api/processors/101/streams/0/parameters/" + encodedStreamParameterName;

    const auto processorGet = client.Get (processorParameterPath.c_str());
    ASSERT_TRUE (processorGet);
    ASSERT_EQ (processorGet->status, 200);
    EXPECT_EQ (json::parse (processorGet->body)["name"], "gain mode");

    const auto streamGet = client.Get (streamParameterPath.c_str());
    ASSERT_TRUE (streamGet);
    ASSERT_EQ (streamGet->status, 200);
    EXPECT_EQ (json::parse (streamGet->body)["name"], "gain mode");

    const auto processorPut = putWhilePumpingMessageThread (
        processorParameterPath, "{\"value\": 7}");
    ASSERT_TRUE (processorPut);
    ASSERT_EQ (processorPut->status, 200);
    EXPECT_EQ (static_cast<int> (processorInGraph->getParameter ("gain mode")->getValue()), 7);
    EXPECT_EQ (static_cast<RouteTestIntParameter*> (
                   processorInGraph->getParameter ("gain mode"))->mutationCalls,
               1);

    const auto streamPut = putWhilePumpingMessageThread (
        streamParameterPath, "{\"value\": 8}");
    ASSERT_TRUE (streamPut);
    ASSERT_EQ (streamPut->status, 200);
    EXPECT_EQ (static_cast<int> (processorInGraph->getDataStream (streamId)->getParameter ("gain mode")->getValue()), 8);
    EXPECT_EQ (static_cast<RouteTestIntParameter*> (
                   processorInGraph->getDataStream (streamId)->getParameter ("gain mode"))->mutationCalls,
               1);

    const auto processorLegacy = client.Get ("/api/processors/101/parameters/gain-mode_1.0");
    ASSERT_TRUE (processorLegacy);
    EXPECT_EQ (processorLegacy->status, 200);
    EXPECT_EQ (json::parse (processorLegacy->body)["name"], "gain-mode_1.0");

    const auto streamLegacy = client.Get ("/api/processors/101/streams/0/parameters/gain-mode_1.0");
    ASSERT_TRUE (streamLegacy);
    EXPECT_EQ (streamLegacy->status, 200);
    EXPECT_EQ (json::parse (streamLegacy->body)["name"], "gain-mode_1.0");
}

TEST_F (ParameterRouteSegmentTests,
        RejectsUnsafeOrMalformedNamesBeforeParameterLookupOrMutation)
{
    for (const auto& name : unsafeRouteNames)
    {
        auto client = loopback->client();
        const auto processorGet = client.Get (
            ("/api/processors/101/parameters/" + std::string (name.encoded)).c_str());
        ASSERT_TRUE (processorGet) << name.encoded;
        EXPECT_EQ (processorGet->status, name.expectedStatus) << name.encoded;
        EXPECT_TRUE (processorGet->body.empty()) << name.encoded;

        const auto streamGet = client.Get (
            ("/api/processors/101/streams/0/parameters/" + std::string (name.encoded)).c_str());
        ASSERT_TRUE (streamGet) << name.encoded;
        EXPECT_EQ (streamGet->status, name.expectedStatus) << name.encoded;
        EXPECT_TRUE (streamGet->body.empty()) << name.encoded;

        const auto processorPut = putWhilePumpingMessageThread (
            "/api/processors/101/parameters/" + std::string (name.encoded), "{\"value\": 9}");
        ASSERT_TRUE (processorPut) << name.encoded;
        EXPECT_EQ (processorPut->status, name.expectedStatus) << name.encoded;
        EXPECT_TRUE (processorPut->body.empty()) << name.encoded;

        const auto streamPut = putWhilePumpingMessageThread (
            "/api/processors/101/streams/0/parameters/" + std::string (name.encoded), "{\"value\": 9}");
        ASSERT_TRUE (streamPut) << name.encoded;
        EXPECT_EQ (streamPut->status, name.expectedStatus) << name.encoded;
        EXPECT_TRUE (streamPut->body.empty()) << name.encoded;

        if (name.decoded != nullptr)
        {
            auto* processorParameter = processorInGraph->getParameter (name.decoded);
            ASSERT_NE (processorParameter, nullptr) << name.encoded;
            EXPECT_EQ (static_cast<int> (processorParameter->getValue()), 5) << name.encoded;
            EXPECT_EQ (static_cast<RouteTestIntParameter*> (processorParameter)->mutationCalls, 0)
                << name.encoded;

            auto* streamParameter =
                processorInGraph->getDataStream (streamId)->getParameter (name.decoded);
            ASSERT_NE (streamParameter, nullptr) << name.encoded;
            EXPECT_EQ (static_cast<int> (streamParameter->getValue()), 6) << name.encoded;
            EXPECT_EQ (static_cast<RouteTestIntParameter*> (streamParameter)->mutationCalls, 0)
                << name.encoded;
        }
    }
}
} // namespace
