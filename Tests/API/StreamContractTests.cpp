/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#include "../../Source/Processors/GenericProcessor/GenericProcessor.h"
#include "../../Source/Processors/Settings/DataStream.h"

#define private public
#include "../../Source/Utils/OpenEphysHttpServer.h"
#undef private

#include "gtest/gtest.h"

#include <memory>
#include <utility>

namespace
{
using json = nlohmann::json;

class ContractDataStream final : public DataStream
{
public:
    ContractDataStream (Settings settings, int sourceId, String sourceName)
        : DataStream (std::move (settings))
    {
        setSourceNodeId (sourceId);
        setSourceNodeName (std::move (sourceName));
    }
};

class StreamContractProcessor final : public GenericProcessor
{
public:
    StreamContractProcessor() : GenericProcessor ("Stream contract processor", true) {}

    void process (AudioBuffer<float>&) override {}

    DataStream* addStream (DataStream::Settings settings,
                           int sourceId,
                           String sourceName)
    {
        auto stream = std::make_unique<ContractDataStream> (
            std::move (settings), sourceId, std::move (sourceName));
        auto* result = stream.get();
        dataStreams.add (stream.release());
        updateChannelIndexMaps();
        return result;
    }

    void moveStream (int currentIndex, int newIndex)
    {
        dataStreams.move (currentIndex, newIndex);
        updateChannelIndexMaps();
    }
};

DataStream::Settings probeStreamSettings (String identifier = "imec.ap")
{
    return {
        "Probe AP",
        "Neuropixels action-potential stream",
        std::move (identifier),
        30000.0f,
        true
    };
}

json serialiseStream (StreamContractProcessor& processor,
                      const DataStream* stream,
                      int streamIndex = 0)
{
    json document;
    OpenEphysHttpServer::stream_to_json (
        &processor, stream, streamIndex, &document);
    return document;
}

json serialiseProcessor (StreamContractProcessor& processor)
{
    json document;
    OpenEphysHttpServer::processor_to_json (&processor, &document);
    return document;
}
} // namespace

TEST (StreamContractTests, RetainsLegacyFieldsAndAddsIdentityMetadata)
{
    StreamContractProcessor processor;
    const auto* stream = processor.addStream (
        probeStreamSettings(), 101, "Neuropixels PXI");

    const auto document = serialiseStream (processor, stream);

    ASSERT_TRUE (document.contains ("name"));
    ASSERT_TRUE (document.contains ("source_id"));
    ASSERT_TRUE (document.contains ("sample_rate"));
    ASSERT_TRUE (document.contains ("channel_count"));
    ASSERT_TRUE (document.contains ("parameters"));
    EXPECT_TRUE (document["name"].is_string());
    EXPECT_TRUE (document["source_id"].is_number_integer());
    EXPECT_TRUE (document["sample_rate"].is_number());
    EXPECT_TRUE (document["channel_count"].is_number_integer());
    EXPECT_TRUE (document["parameters"].is_array());
    EXPECT_EQ (document["name"], "Probe AP");
    EXPECT_EQ (document["source_id"], 101);
    EXPECT_FLOAT_EQ (document["sample_rate"].get<float>(), 30000.0f);
    EXPECT_EQ (document["channel_count"], 0);
    EXPECT_TRUE (document["parameters"].empty());

    ASSERT_TRUE (document.contains ("stream_index"));
    ASSERT_TRUE (document.contains ("runtime_id"));
    ASSERT_TRUE (document.contains ("source_name"));
    ASSERT_TRUE (document.contains ("description"));
    ASSERT_TRUE (document.contains ("identifier"));
    ASSERT_TRUE (document.contains ("generates_timestamps"));
    ASSERT_TRUE (document.contains ("identity"));
    EXPECT_EQ (document["stream_index"], 0);
    EXPECT_EQ (document["runtime_id"], stream->getStreamId());
    EXPECT_EQ (document["source_name"], "Neuropixels PXI");
    EXPECT_EQ (document["description"], "Neuropixels action-potential stream");
    EXPECT_EQ (document["identifier"], "imec.ap");
    EXPECT_EQ (document["generates_timestamps"], true);
    EXPECT_EQ (
        document["identity"],
        json ({ { "available", true },
                { "scope", "configuration" },
                { "source_id", 101 },
                { "identifier", "imec.ap" } }));
}

TEST (StreamContractTests, CompoundIdentityDistinguishesSameNamedStreams)
{
    StreamContractProcessor processor;
    processor.addStream (probeStreamSettings(), 101, "First probe");
    processor.addStream (probeStreamSettings(), 202, "Second probe");

    const auto document = serialiseProcessor (processor);
    ASSERT_EQ (document["streams"].size(), 2u);
    EXPECT_EQ (document["streams"][0]["name"], "Probe AP");
    EXPECT_EQ (document["streams"][1]["name"], "Probe AP");
    EXPECT_NE (
        document["streams"][0]["identity"]["source_id"],
        document["streams"][1]["identity"]["source_id"]);
    EXPECT_EQ (document["streams"][0]["identity"]["identifier"], "imec.ap");
    EXPECT_EQ (document["streams"][1]["identity"]["identifier"], "imec.ap");
}

TEST (StreamContractTests, EmptyIdentifierDoesNotFabricateStableIdentity)
{
    StreamContractProcessor processor;
    processor.setNodeId (100);
    const auto* stream = processor.addStream (
        probeStreamSettings (""), 101, "Neuropixels PXI");

    const auto document = serialiseStream (processor, stream);

    EXPECT_EQ (document["identifier"], "");
    ASSERT_TRUE (document["identity"].is_object());
    EXPECT_EQ (document["identity"]["available"], false);
    EXPECT_EQ (document["identity"]["scope"], "configuration");
    EXPECT_EQ (document["identity"]["source_id"], 101);
    EXPECT_EQ (document["identity"]["identifier"], "");
    EXPECT_EQ (
        document["uia"],
        json ({ { "automation_id",
                  "oe.processor.100.streams.table.source_101.stream_probe_ap" },
                { "scope", "configuration" },
                { "uses_display_name_fallback", true } }));
}

TEST (StreamContractTests, ExposesMatchingConfigurationScopedUiaLocator)
{
    StreamContractProcessor processor;
    processor.setNodeId (100);
    const auto* stream = processor.addStream (
        probeStreamSettings ("imec.ap"), 101, "Neuropixels PXI");

    const auto document = serialiseStream (processor, stream);

    EXPECT_EQ (
        document["uia"],
        json ({ { "automation_id",
                  "oe.processor.100.streams.table.source_101.stream_imec_ap" },
                { "scope", "configuration" },
                { "uses_display_name_fallback", false } }));
}

TEST (StreamContractTests, DisambiguatesSanitisedUiaLocatorCollisions)
{
    StreamContractProcessor processor;
    processor.setNodeId (100);
    processor.addStream (
        probeStreamSettings ("probe.ap"), 101, "Neuropixels PXI");
    const auto* underscored = processor.addStream (
        probeStreamSettings ("probe_ap"), 101, "Neuropixels PXI");
    processor.addStream (
        probeStreamSettings (""), 101, "Neuropixels PXI");

    const auto nested = serialiseProcessor (processor);
    ASSERT_EQ (nested["streams"].size(), 3u);
    EXPECT_EQ (
        nested["streams"][0]["uia"]["automation_id"],
        "oe.processor.100.streams.table.source_101.stream_probe_ap.index_0");
    EXPECT_EQ (
        nested["streams"][1]["uia"]["automation_id"],
        "oe.processor.100.streams.table.source_101.stream_probe_ap.index_1");
    EXPECT_EQ (
        nested["streams"][2]["uia"]["automation_id"],
        "oe.processor.100.streams.table.source_101.stream_probe_ap.index_2");
    EXPECT_EQ (
        nested["streams"][2]["uia"]["uses_display_name_fallback"],
        true);
    EXPECT_NE (
        nested["streams"][0]["uia"]["automation_id"],
        nested["streams"][1]["uia"]["automation_id"]);

    const auto direct = serialiseStream (processor, underscored, 1);
    EXPECT_EQ (direct, nested["streams"][1]);
}

TEST (StreamContractTests, ReorderingChangesLocatorButNotIdentityFacts)
{
    StreamContractProcessor processor;
    const auto* first = processor.addStream (
        probeStreamSettings ("imec.ap"), 101, "First probe");
    const auto* second = processor.addStream (
        { "Probe LFP", "Neuropixels low-frequency stream", "imec.lfp", 2500.0f, true },
        101,
        "First probe");
    const auto firstRuntimeId = first->getStreamId();
    const auto secondRuntimeId = second->getStreamId();

    const auto before = serialiseProcessor (processor);
    ASSERT_EQ (before["streams"].size(), 2u);
    EXPECT_EQ (before["streams"][0]["stream_index"], 0);
    EXPECT_EQ (before["streams"][1]["stream_index"], 1);
    EXPECT_EQ (before["streams"][0]["runtime_id"], firstRuntimeId);
    EXPECT_EQ (before["streams"][1]["runtime_id"], secondRuntimeId);
    EXPECT_NE (before["streams"][0]["stream_index"], firstRuntimeId);
    EXPECT_NE (before["streams"][1]["stream_index"], secondRuntimeId);

    processor.moveStream (0, 1);
    const auto after = serialiseProcessor (processor);
    ASSERT_EQ (after["streams"].size(), 2u);
    EXPECT_EQ (after["streams"][0]["stream_index"], 0);
    EXPECT_EQ (after["streams"][0]["runtime_id"], secondRuntimeId);
    EXPECT_EQ (after["streams"][0]["identity"]["source_id"], 101);
    EXPECT_EQ (after["streams"][0]["identity"]["identifier"], "imec.lfp");
    EXPECT_EQ (after["streams"][1]["stream_index"], 1);
    EXPECT_EQ (after["streams"][1]["runtime_id"], firstRuntimeId);
    EXPECT_EQ (after["streams"][1]["identity"]["source_id"], 101);
    EXPECT_EQ (after["streams"][1]["identity"]["identifier"], "imec.ap");
}

TEST (StreamContractTests, NestedAndDirectSerializersHaveIdenticalShape)
{
    StreamContractProcessor processor;
    const auto* stream = processor.addStream (
        probeStreamSettings(), 101, "Neuropixels PXI");

    const auto nested = serialiseProcessor (processor)["streams"][0];
    const auto direct = serialiseStream (processor, stream);

    EXPECT_EQ (nested, direct);
}

TEST (StreamContractTests, GetSerializationDoesNotMutateStreamState)
{
    StreamContractProcessor processor;
    auto* stream = processor.addStream (
        probeStreamSettings(), 101, "Neuropixels PXI");
    const auto runtimeId = stream->getStreamId();
    const auto name = stream->getName();
    const auto sourceId = stream->getSourceNodeId();
    const auto identifier = stream->getIdentifier();
    const auto parameterCount = stream->getParameters().size();

    const auto ignored = serialiseProcessor (processor);
    (void) ignored;

    EXPECT_EQ (stream->getStreamId(), runtimeId);
    EXPECT_EQ (stream->getName(), name);
    EXPECT_EQ (stream->getSourceNodeId(), sourceId);
    EXPECT_EQ (stream->getIdentifier(), identifier);
    EXPECT_EQ (stream->getParameters().size(), parameterCount);
}
