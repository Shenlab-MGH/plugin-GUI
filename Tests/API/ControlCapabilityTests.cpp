#include "../../Source/CoreServices.h"
#include "../../Source/Utils/ControlCapability.h"
#include "../../Source/Utils/ControlCapabilityJson.h"
#include "gtest/gtest.h"

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
      FreezesTheStatusApiFieldsOnExactlyTwoCanonicalCapabilities)
{
    const auto& capabilities =
        getCoreControlCapabilities();
    StringArray statusCapabilityIds;

    const StringArray expectedReadFields {
        "capabilities",
        "mode",
        "acquisition_active",
        "recording_active",
        "record_node_count",
        "active_record_node_count",
        "writer_thread_running_count",
        "recording_consistent",
        "read_only"
    };
    const StringArray expectedMutationFields {
        "requested_mode",
        "mode",
        "changed",
        "acquisition_active",
        "recording_active",
        "record_node_count",
        "active_record_node_count",
        "writer_thread_running_count",
        "recording_consistent",
        "unsynchronized_confirmed"
    };

    for (const auto& capability : capabilities)
    {
        const auto usesStatusApi = std::any_of (
            capability.operations.begin(),
            capability.operations.end(),
            [] (const auto& operation)
            {
                return operation.path == "/api/status";
            });
        if (usesStatusApi)
            statusCapabilityIds.add (capability.id);
    }

    EXPECT_EQ (
        statusCapabilityIds,
        (StringArray {
            "oe.control.acquisition",
            "oe.control.recording"
        }));
    EXPECT_EQ (
        findControlCapability ("oe.status.global"),
        nullptr);

    const auto* acquisition =
        findControlCapability (
            "oe.control.acquisition");
    const auto* recording =
        findControlCapability (
            "oe.control.recording");
    ASSERT_NE (acquisition, nullptr);
    ASSERT_NE (recording, nullptr);
    ASSERT_EQ (acquisition->operations.size(), 2u);
    ASSERT_EQ (recording->operations.size(), 2u);

    EXPECT_TRUE (
        acquisition->operations[0].requestFields
            .isEmpty());
    EXPECT_EQ (
        acquisition->operations[0].responseFields,
        expectedReadFields);
    EXPECT_EQ (
        acquisition->operations[1].requestFields,
        (StringArray { "mode" }));
    EXPECT_EQ (
        acquisition->operations[1].responseFields,
        expectedMutationFields);

    EXPECT_TRUE (
        recording->operations[0].requestFields
            .isEmpty());
    EXPECT_EQ (
        recording->operations[0].responseFields,
        expectedReadFields);
    EXPECT_EQ (
        recording->operations[1].requestFields,
        (StringArray {
            "mode",
            "confirm_unsynchronized"
        }));
    EXPECT_EQ (
        recording->operations[1].responseFields,
        expectedMutationFields);
}

TEST (ControlCapabilityTests, CalculatesBoundedDiskUsage)
{
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (25, 100), 0.75f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (100, 100), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (0, 100), 1.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (1, 0), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (120, 100), 0.0f);
}
