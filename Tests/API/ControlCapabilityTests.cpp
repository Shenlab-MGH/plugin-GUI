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
    EXPECT_EQ (document["contract_version"], "0.1.2");
    ASSERT_TRUE (document["capabilities"].is_array());
    ASSERT_EQ (document["capabilities"].size(), 10);

    const auto acquisition = std::find_if (document["capabilities"].begin(),
                                           document["capabilities"].end(),
                                           [] (const auto& item)
                                           { return item["id"] == "oe.control.acquisition"; });
    ASSERT_NE (acquisition, document["capabilities"].end());
    EXPECT_EQ ((*acquisition)["uia"]["automation_id"], "oe.control.acquisition");
    EXPECT_EQ ((*acquisition)["api"][0]["path"], "/api/status");
    EXPECT_EQ ((*acquisition)["api"][0]["method"], "GET");
}

TEST (ControlCapabilityTests, DefinesRecordingDirectoryAsOneExistingRouteAndField)
{
    const auto* directory = findControlCapability ("oe.control.recording.directory");
    ASSERT_NE (directory, nullptr);
    EXPECT_EQ (directory->kind, ControlCapabilityKind::value);
    ASSERT_EQ (directory->operations.size(), (size_t) 2);

    const auto& read = directory->operations[0];
    EXPECT_EQ (read.operation, "read");
    EXPECT_EQ (read.method, "GET");
    EXPECT_EQ (read.path, "/api/recording");
    EXPECT_TRUE (read.requestFields.isEmpty());
    EXPECT_EQ (read.responseFields, StringArray ({ "parent_directory" }));

    const auto& set = directory->operations[1];
    EXPECT_EQ (set.operation, "set");
    EXPECT_EQ (set.method, "PUT");
    EXPECT_EQ (set.path, "/api/recording");
    EXPECT_EQ (set.requestFields, StringArray ({ "parent_directory" }));
    EXPECT_EQ (set.responseFields, StringArray ({ "parent_directory" }));
}

TEST (ControlCapabilityTests, DefinesStatusModeSemanticsUsingActualGuiState)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());
    const auto& capabilities = document["capabilities"];
    const auto findById = [&capabilities] (const char* id)
    {
        return std::find_if (capabilities.begin(),
                             capabilities.end(),
                             [id] (const auto& item) { return item["id"] == id; });
    };

    const auto acquisitionResult = findById ("oe.control.acquisition");
    const auto recordingResult = findById ("oe.control.recording");
    ASSERT_NE (acquisitionResult, capabilities.end());
    ASSERT_NE (recordingResult, capabilities.end());

    const auto& acquisition = *acquisitionResult;
    const auto& recording = *recordingResult;
    const auto expectedModes = nlohmann::json::array ({ "IDLE", "ACQUIRE", "RECORD" });

    ASSERT_TRUE (acquisition.contains ("mode_semantics"));
    EXPECT_EQ (acquisition["mode_semantics"]["field"], "mode");
    EXPECT_EQ (acquisition["mode_semantics"]["allowed_values"], expectedModes);
    EXPECT_EQ (acquisition["mode_semantics"]["on_values"], nlohmann::json::array ({ "ACQUIRE", "RECORD" }));
    EXPECT_EQ (acquisition["mode_semantics"]["off_values"], nlohmann::json::array ({ "IDLE" }));
    EXPECT_EQ (acquisition["mode_semantics"]["commands"]["on"]["mode"], "ACQUIRE");
    EXPECT_EQ (acquisition["mode_semantics"]["commands"]["off"]["mode"], "IDLE");
    EXPECT_EQ (acquisition["mode_semantics"]["response_meaning"], "actual_gui_state");

    ASSERT_TRUE (recording.contains ("mode_semantics"));
    EXPECT_EQ (recording["mode_semantics"]["field"], "mode");
    EXPECT_EQ (recording["mode_semantics"]["allowed_values"], expectedModes);
    EXPECT_EQ (recording["mode_semantics"]["on_values"], nlohmann::json::array ({ "RECORD" }));
    EXPECT_EQ (recording["mode_semantics"]["off_values"], nlohmann::json::array ({ "IDLE", "ACQUIRE" }));
    EXPECT_EQ (recording["mode_semantics"]["commands"]["on"]["mode"], "RECORD");
    EXPECT_EQ (recording["mode_semantics"]["commands"]["off"]["mode"], "ACQUIRE");
    EXPECT_EQ (recording["mode_semantics"]["response_meaning"], "actual_gui_state");

    for (const auto& item : capabilities)
    {
        const auto id = item["id"].get<std::string>();
        if (id != "oe.control.acquisition" && id != "oe.control.recording")
            EXPECT_FALSE (item.contains ("mode_semantics")) << id;
    }
}

TEST (ControlCapabilityTests, CalculatesBoundedDiskUsage)
{
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (25, 100), 0.75f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (100, 100), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (0, 100), 1.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (1, 0), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (120, 100), 0.0f);
}
