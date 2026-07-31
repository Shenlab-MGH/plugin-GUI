#include "../../Source/CoreServices.h"
#include "../../Source/Utils/ControlCapability.h"
#include "../../Source/Utils/ControlCapabilityJson.h"
#include "../../Source/Utils/ControlStatusJson.h"
#include "../../Source/Utils/StatusControl.h"
#include "gtest/gtest.h"

#include <set>
#include <string>

namespace
{
using json = nlohmann::json;

const json* findCapabilityJson (const json& document, const char* id)
{
    const auto result = std::find_if (
        document["capabilities"].begin(),
        document["capabilities"].end(),
        [id] (const auto& item) { return item["id"] == id; });
    return result == document["capabilities"].end() ? nullptr : &*result;
}

std::set<std::string> stringSet (const json& values)
{
    std::set<std::string> result;
    for (const auto& value : values)
        result.insert (value.template get<std::string>());
    return result;
}

std::set<std::string> keySet (const json& document)
{
    std::set<std::string> result;
    for (const auto& item : document.items())
        result.insert (item.key());
    return result;
}

AcquisitionRecordingControlSnapshot idleSnapshot()
{
    AcquisitionRecordingControlSnapshot snapshot;
    snapshot.status = deriveAcquisitionRecordingStatus (
        false,
        { { false, false } });
    snapshot.recordNodes = {
        { 1, false, false, true, true }
    };
    snapshot.audioDeviceAvailable = true;
    snapshot.audioSampleRate = 30000.0;
    snapshot.processorGraphReady = true;
    return snapshot;
}
} // namespace

TEST (ControlCapabilityTests, DefinesStableCoreControlContracts)
{
    const auto capabilities = getCoreControlCapabilities();
    const StringArray expectedIds {
        "oe.control.acquisition",
        "oe.control.recording",
        "oe.control.recording.options",
        "oe.control.recording.filename",
        "oe.control.recording.directory",
        "oe.control.recording.engine",
        "oe.control.recording.new_directory",
        "oe.control.recording.force_new_directory",
        "oe.status.cpu_usage",
        "oe.status.disk_usage",
        "oe.status.elapsed_time"
    };

    EXPECT_EQ (capabilities.size(), (size_t) expectedIds.size());

    for (const auto& id : expectedIds)
    {
        const auto* capability = findControlCapability (id);
        ASSERT_NE (capability, nullptr) << id;
        EXPECT_EQ (capability->id, id);
        EXPECT_EQ (capability->uiaAutomationId, id);
        EXPECT_TRUE (capability->name.isNotEmpty());
        EXPECT_TRUE (capability->description.isNotEmpty());
        EXPECT_FALSE (capability->operations.empty());
    }
}

TEST (ControlCapabilityTests, SerialisesApiAndUiaMetadataByCanonicalId)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());
    ASSERT_TRUE (document["capabilities"].is_array());
    ASSERT_EQ (document["capabilities"].size(), 11);

    const auto acquisition = std::find_if (document["capabilities"].begin(),
                                           document["capabilities"].end(),
                                           [] (const auto& item)
                                           { return item["id"] == "oe.control.acquisition"; });
    ASSERT_NE (acquisition, document["capabilities"].end());
    EXPECT_EQ ((*acquisition)["uia"]["automation_id"], "oe.control.acquisition");
    EXPECT_EQ ((*acquisition)["api"][0]["path"], "/api/status");
    EXPECT_EQ ((*acquisition)["api"][0]["method"], "GET");

    const auto newDirectory = std::find_if (
        document["capabilities"].begin(),
        document["capabilities"].end(),
        [] (const auto& item)
        { return item["id"] == "oe.control.recording.new_directory"; });
    ASSERT_NE (newDirectory, document["capabilities"].end());
    EXPECT_EQ ((*newDirectory)["api"][0]["response_fields"][1],
               "new_directory_request_available");

    const auto recordingDirectory = std::find_if (
        document["capabilities"].begin(),
        document["capabilities"].end(),
        [] (const auto& item)
        { return item["id"] == "oe.control.recording.directory"; });
    ASSERT_NE (recordingDirectory, document["capabilities"].end());
    EXPECT_EQ ((*recordingDirectory)["kind"], "value");
    EXPECT_EQ ((*recordingDirectory)["uia"]["automation_id"],
               "oe.control.recording.directory");
    ASSERT_EQ ((*recordingDirectory)["api"].size(), 2);
    EXPECT_EQ ((*recordingDirectory)["api"][0]["method"], "GET");
    EXPECT_EQ ((*recordingDirectory)["api"][0]["path"], "/api/recording");
    EXPECT_EQ ((*recordingDirectory)["api"][0]["response_fields"][0],
               "parent_directory");
    EXPECT_EQ ((*recordingDirectory)["api"][1]["method"], "PUT");
    EXPECT_EQ ((*recordingDirectory)["api"][1]["request_fields"][0],
               "parent_directory");

    const auto recordingEngine = std::find_if (
        document["capabilities"].begin(),
        document["capabilities"].end(),
        [] (const auto& item)
        { return item["id"] == "oe.control.recording.engine"; });
    ASSERT_NE (recordingEngine, document["capabilities"].end());
    EXPECT_EQ ((*recordingEngine)["kind"], "selection");
    EXPECT_EQ ((*recordingEngine)["uia"]["automation_id"],
               "oe.control.recording.engine");
    ASSERT_EQ ((*recordingEngine)["api"].size(), 2);
    EXPECT_EQ ((*recordingEngine)["api"][0]["method"], "GET");
    EXPECT_EQ ((*recordingEngine)["api"][0]["path"], "/api/recording");
    EXPECT_EQ ((*recordingEngine)["api"][0]["response_fields"][0],
               "default_record_engine");
    EXPECT_EQ ((*recordingEngine)["api"][1]["method"], "PUT");
    EXPECT_EQ ((*recordingEngine)["api"][1]["request_fields"][0],
               "default_record_engine");
}

TEST (ControlCapabilityTests,
      StatusCapabilitiesMatchExactSuccessfulStatusJsonContracts)
{
    StatusControlResult getResult;
    getResult.httpStatus = 200;
    getResult.achieved = idleSnapshot();
    const auto getDocument = statusGetResultToJson (getResult);

    StatusControlResult putResult;
    putResult.httpStatus = 200;
    putResult.requestedMode = AcquisitionRecordingMode::idle;
    putResult.achieved = idleSnapshot();
    const auto putDocument = statusPutResultToJson (putResult);

    const auto capabilitiesDocument =
        controlCapabilitiesToJson (getCoreControlCapabilities());
    const auto* acquisition = findCapabilityJson (
        capabilitiesDocument,
        "oe.control.acquisition");
    const auto* recording = findCapabilityJson (
        capabilitiesDocument,
        "oe.control.recording");
    ASSERT_NE (acquisition, nullptr);
    ASSERT_NE (recording, nullptr);

    const json expectedGetFields = {
        "ok", "capabilities", "mode", "acquisition_active",
        "recording_active", "record_node_count",
        "active_record_node_count", "writer_thread_running_count",
        "recording_consistent", "record_nodes",
        "audio_device_available", "audio_sample_rate",
        "processor_graph_ready"
    };
    const json expectedPutFields = {
        "ok", "capabilities", "mode", "acquisition_active",
        "recording_active", "record_node_count",
        "active_record_node_count", "writer_thread_running_count",
        "recording_consistent", "record_nodes",
        "audio_device_available", "audio_sample_rate",
        "processor_graph_ready", "requested_mode", "changed",
        "unsynchronized_confirmed"
    };

    for (const auto* capability : { acquisition, recording })
    {
        ASSERT_EQ ((*capability)["api"].size(), 2);
        EXPECT_EQ ((*capability)["api"][0]["method"], "GET");
        EXPECT_EQ ((*capability)["api"][0]["path"], "/api/status");
        EXPECT_EQ ((*capability)["api"][0]["request_fields"], json::array());
        EXPECT_EQ ((*capability)["api"][0]["response_fields"], expectedGetFields);
        EXPECT_EQ (
            stringSet ((*capability)["api"][0]["response_fields"]),
            keySet (getDocument));

        EXPECT_EQ ((*capability)["api"][1]["method"], "PUT");
        EXPECT_EQ ((*capability)["api"][1]["path"], "/api/status");
        EXPECT_EQ ((*capability)["api"][1]["response_fields"], expectedPutFields);
        EXPECT_EQ (
            stringSet ((*capability)["api"][1]["response_fields"]),
            keySet (putDocument));
    }

    EXPECT_EQ (
        (*acquisition)["api"][1]["request_fields"],
        json ({ "mode" }));
    EXPECT_EQ (
        (*recording)["api"][1]["request_fields"],
        json ({ "mode", "confirm_unsynchronized" }));

    std::size_t statusOperationCount = 0;
    for (const auto& capability : capabilitiesDocument["capabilities"])
    {
        for (const auto& operation : capability["api"])
        {
            if (operation["path"] == "/api/status")
            {
                ++statusOperationCount;
                EXPECT_TRUE (
                    capability["id"] == "oe.control.acquisition"
                    || capability["id"] == "oe.control.recording");
            }
        }
    }
    EXPECT_EQ (statusOperationCount, 4);
}

TEST (ControlCapabilityTests, CalculatesBoundedDiskUsage)
{
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (25, 100), 0.75f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (100, 100), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (0, 100), 1.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (1, 0), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (120, 100), 0.0f);
}
