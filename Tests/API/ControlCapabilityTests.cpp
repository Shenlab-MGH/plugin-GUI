#include "../../Source/Utils/ControlCapability.h"
#include "../../Source/Utils/ControlCapabilityJson.h"
#include "../../Source/Utils/OpenEphysHttpApiRoutes.h"
#include "gtest/gtest.h"

#include <cstring>
#include <vector>

namespace
{
struct ExpectedApiOperation
{
    const char* operation;
    OpenEphysHttpApi::Route route;
};

struct ExpectedCapability
{
    const char* id;
    const char* uiaAutomationId;
    std::vector<ExpectedApiOperation> operations;
};

// Full 0.0.3 capability set. Order is stable.
// Operations reference shared route descriptors.
const std::vector<ExpectedCapability> expectedCapabilities {
    { "oe.control.acquisition", "oe.control.acquisition",
      { { "read", OpenEphysHttpApi::kStatusGet },
        { "set", OpenEphysHttpApi::kStatusPut } } },
    { "oe.control.recording", "oe.control.recording",
      { { "read", OpenEphysHttpApi::kStatusGet },
        { "set", OpenEphysHttpApi::kStatusPut } } },
    { "oe.control.recording.options", "oe.control.recording.options",
      { { "read", OpenEphysHttpApi::kRecordingOptionsGet },
        { "set", OpenEphysHttpApi::kRecordingOptionsPut } } },
    { "oe.control.recording.filename", "oe.control.recording.filename",
      { { "read", OpenEphysHttpApi::kRecordingGet },
        { "set", OpenEphysHttpApi::kRecordingPut } } },
    { "oe.control.recording.directory", "oe.control.recording.directory",
      { { "read", OpenEphysHttpApi::kRecordingGet },
        { "set", OpenEphysHttpApi::kRecordingPut } } },
    { "oe.control.recording.new_directory", "oe.control.recording.new_directory",
      { { "read", OpenEphysHttpApi::kRecordingOptionsGet },
        { "set", OpenEphysHttpApi::kRecordingOptionsPut } } },
    { "oe.control.recording.force_new_directory", "oe.control.recording.force_new_directory",
      { { "read", OpenEphysHttpApi::kRecordingOptionsGet },
        { "set", OpenEphysHttpApi::kRecordingOptionsPut } } },
    { "oe.control.signal_chain.configuration", "",
      { { "read", OpenEphysHttpApi::kConfigGet } } },
    { "oe.status.cpu_usage", "oe.status.cpu_usage",
      { { "read", OpenEphysHttpApi::kCpuGet } } },
    { "oe.status.disk_usage", "oe.status.disk_usage",
      { { "read", OpenEphysHttpApi::kDiskGet } } },
    { "oe.status.elapsed_time", "oe.status.elapsed_time",
      { { "read", OpenEphysHttpApi::kTimeGet } } },
};

const OpenEphysHttpApi::Route coreR0RouteDescriptors[] = {
    OpenEphysHttpApi::kStatusGet,
    OpenEphysHttpApi::kStatusPut,
    OpenEphysHttpApi::kRecordingGet,
    OpenEphysHttpApi::kRecordingPut,
    OpenEphysHttpApi::kRecordingOptionsGet,
    OpenEphysHttpApi::kRecordingOptionsPut,
    OpenEphysHttpApi::kConfigGet,
    OpenEphysHttpApi::kCpuGet,
    OpenEphysHttpApi::kDiskGet,
    OpenEphysHttpApi::kTimeGet,
};

const OpenEphysHttpApi::Route sharedRegisteredRouteDescriptors[] = {
    OpenEphysHttpApi::kCapabilitiesGet,
    OpenEphysHttpApi::kStatusGet,
    OpenEphysHttpApi::kStatusPut,
    OpenEphysHttpApi::kRecordingGet,
    OpenEphysHttpApi::kRecordingPut,
    OpenEphysHttpApi::kRecordingOptionsGet,
    OpenEphysHttpApi::kRecordingOptionsPut,
    OpenEphysHttpApi::kConfigGet,
    OpenEphysHttpApi::kCpuGet,
    OpenEphysHttpApi::kDiskGet,
    OpenEphysHttpApi::kTimeGet,
};

bool routesEqual (const OpenEphysHttpApi::Route& a, const OpenEphysHttpApi::Route& b)
{
    return a.method == b.method
           && a.path != nullptr
           && b.path != nullptr
           && std::strcmp (a.path, b.path) == 0;
}

bool isRegisteredCoreR0Route (const OpenEphysHttpApi::Route& route)
{
    for (const auto& candidate : coreR0RouteDescriptors)
    {
        if (routesEqual (route, candidate))
            return true;
    }

    return false;
}
} // namespace

TEST (ControlCapabilityTests, SharedRouteDescriptorsHaveExactMethodPathPairs)
{
    EXPECT_EQ (OpenEphysHttpApi::kCapabilitiesGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kCapabilitiesGet.path, "/api/capabilities");
    EXPECT_STREQ (OpenEphysHttpApi::kCapabilitiesGet.methodString(), "GET");

    EXPECT_EQ (OpenEphysHttpApi::kRecordingOptionsGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingOptionsGet.path, "/api/recording/options");
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingOptionsGet.methodString(), "GET");

    EXPECT_EQ (OpenEphysHttpApi::kRecordingOptionsPut.method, OpenEphysHttpApi::Method::Put);
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingOptionsPut.path, "/api/recording/options");
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingOptionsPut.methodString(), "PUT");

    EXPECT_EQ (OpenEphysHttpApi::kConfigGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kConfigGet.path, "/api/config");
    EXPECT_STREQ (OpenEphysHttpApi::kConfigGet.methodString(), "GET");

    EXPECT_EQ (OpenEphysHttpApi::kDiskGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kDiskGet.path, "/api/disk");
    EXPECT_STREQ (OpenEphysHttpApi::kDiskGet.methodString(), "GET");

    EXPECT_EQ (OpenEphysHttpApi::kTimeGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kTimeGet.path, "/api/time");
    EXPECT_STREQ (OpenEphysHttpApi::kTimeGet.methodString(), "GET");

    ASSERT_EQ (sizeof (sharedRegisteredRouteDescriptors) / sizeof (sharedRegisteredRouteDescriptors[0]), (size_t) 11);
    ASSERT_EQ (sizeof (coreR0RouteDescriptors) / sizeof (coreR0RouteDescriptors[0]), (size_t) 10);
}

TEST (ControlCapabilityTests, DefinesStableCoreControlContracts)
{
    const auto& capabilities = getCoreControlCapabilities();

    ASSERT_EQ (capabilities.size(), expectedCapabilities.size());
    ASSERT_EQ (expectedCapabilities.size(), (size_t) 11);

    for (size_t i = 0; i < capabilities.size(); ++i)
    {
        const auto& capability = capabilities[i];
        const auto& expected = expectedCapabilities[i];
        const String expectedId (expected.id);

        EXPECT_EQ (capability.id, expectedId) << "capability order mismatch at index " << i;
        EXPECT_EQ (capability.uiaAutomationId, String (expected.uiaAutomationId)) << expectedId;
        EXPECT_TRUE (capability.name.isNotEmpty()) << expectedId;
        EXPECT_TRUE (capability.description.isNotEmpty()) << expectedId;

        ASSERT_EQ (capability.operations.size(), expected.operations.size()) << expectedId;

        for (size_t j = 0; j < capability.operations.size(); ++j)
        {
            const auto& operation = capability.operations[j];
            const auto& expectedOp = expected.operations[j];

            EXPECT_EQ (operation.operation, String (expectedOp.operation)) << expectedId << " op " << j;
            EXPECT_EQ (operation.route.method, expectedOp.route.method) << expectedId << " op " << j;
            EXPECT_STREQ (operation.route.path, expectedOp.route.path) << expectedId << " op " << j;
            EXPECT_TRUE (routesEqual (operation.route, expectedOp.route)) << expectedId << " op " << j;
        }

        const auto* found = findControlCapability (expectedId);
        ASSERT_NE (found, nullptr) << expectedId;
        EXPECT_EQ (found->id, expectedId);
    }
}

TEST (ControlCapabilityTests, ManifestOperationsMatchRegisteredCoreRoutes)
{
    const auto& capabilities = getCoreControlCapabilities();

    ASSERT_EQ (capabilities.size(), (size_t) 11);

    for (const auto& capability : capabilities)
    {
        ASSERT_FALSE (capability.operations.empty()) << capability.id;

        for (const auto& operation : capability.operations)
        {
            EXPECT_TRUE (isRegisteredCoreR0Route (operation.route))
                << capability.id << " advertises unregistered "
                << operation.route.methodString() << " " << operation.route.path;
        }
    }

    const auto* options = findControlCapability ("oe.control.recording.options");
    const auto* configuration = findControlCapability ("oe.control.signal_chain.configuration");
    const auto* disk = findControlCapability ("oe.status.disk_usage");
    const auto* time = findControlCapability ("oe.status.elapsed_time");
    ASSERT_NE (options, nullptr);
    ASSERT_NE (configuration, nullptr);
    ASSERT_NE (disk, nullptr);
    ASSERT_NE (time, nullptr);
    EXPECT_TRUE (routesEqual (options->operations[0].route, OpenEphysHttpApi::kRecordingOptionsGet));
    EXPECT_TRUE (routesEqual (configuration->operations[0].route, OpenEphysHttpApi::kConfigGet));
    EXPECT_TRUE (routesEqual (disk->operations[0].route, OpenEphysHttpApi::kDiskGet));
    EXPECT_TRUE (routesEqual (time->operations[0].route, OpenEphysHttpApi::kTimeGet));
}

TEST (ControlCapabilityTests, SerialisesFullCapabilityContractWithUia)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());

    EXPECT_EQ (document["contract_version"], "0.0.3");
    EXPECT_FALSE (document.contains ("surface"));
    ASSERT_TRUE (document["capabilities"].is_array());
    ASSERT_EQ (document["capabilities"].size(), expectedCapabilities.size());
    ASSERT_EQ (document["capabilities"].size(), (size_t) 11);

    for (size_t i = 0; i < document["capabilities"].size(); ++i)
    {
        const auto& item = document["capabilities"][i];
        const auto& expected = expectedCapabilities[i];
        const auto expectedId = std::string (expected.id);

        EXPECT_EQ (item["id"], expectedId);
        if (std::strlen (expected.uiaAutomationId) == 0)
        {
            EXPECT_FALSE (item.contains ("uia")) << expectedId;
        }
        else
        {
            ASSERT_TRUE (item.contains ("uia")) << expectedId;
            EXPECT_EQ (item["uia"]["automation_id"], expected.uiaAutomationId) << expectedId;
        }
        ASSERT_TRUE (item["api"].is_array());
        ASSERT_EQ (item["api"].size(), expected.operations.size()) << expectedId;

        for (size_t j = 0; j < item["api"].size(); ++j)
        {
            const auto& operation = item["api"][j];
            const auto& expectedOp = expected.operations[j];

            ASSERT_TRUE (operation.contains ("method")) << expectedId;
            ASSERT_TRUE (operation.contains ("path")) << expectedId;
            EXPECT_EQ (operation["operation"].get<std::string>(), expectedOp.operation) << expectedId;
            EXPECT_EQ (operation["method"].get<std::string>(), expectedOp.route.methodString()) << expectedId;
            EXPECT_EQ (operation["path"].get<std::string>(), expectedOp.route.path) << expectedId;
        }
    }
}

TEST (ControlCapabilityTests, RecordingDirectoryUsesSharedRouteAndParentDirectoryOnly)
{
    const auto* directory = findControlCapability ("oe.control.recording.directory");
    ASSERT_NE (directory, nullptr);
    EXPECT_EQ (directory->kind, ControlCapabilityKind::value);
    EXPECT_EQ (directory->uiaAutomationId, "oe.control.recording.directory");
    ASSERT_EQ (directory->operations.size(), (size_t) 2);

    const auto& read = directory->operations[0];
    EXPECT_EQ (read.operation, "read");
    EXPECT_TRUE (routesEqual (read.route, OpenEphysHttpApi::kRecordingGet));
    EXPECT_TRUE (read.requestFields.isEmpty());
    EXPECT_EQ (read.responseFields, StringArray ({ "parent_directory" }));

    const auto& set = directory->operations[1];
    EXPECT_EQ (set.operation, "set");
    EXPECT_TRUE (routesEqual (set.route, OpenEphysHttpApi::kRecordingPut));
    EXPECT_EQ (set.requestFields, StringArray ({ "parent_directory" }));
    EXPECT_EQ (set.responseFields, StringArray ({ "parent_directory" }));
}

TEST (ControlCapabilityTests, DefinesStatusModeSemanticsUsingActualGuiState)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());
    const auto& capabilities = document["capabilities"];
    const auto findById = [&capabilities] (const char* id)
    {
        return std::find_if (
            capabilities.begin(),
            capabilities.end(),
            [id] (const auto& item) { return item["id"] == id; });
    };

    const auto acquisitionResult = findById ("oe.control.acquisition");
    const auto recordingResult = findById ("oe.control.recording");
    const auto filenameResult = findById ("oe.control.recording.filename");
    const auto directoryResult = findById ("oe.control.recording.directory");
    const auto cpuResult = findById ("oe.status.cpu_usage");
    const auto optionsResult = findById ("oe.control.recording.options");

    ASSERT_NE (acquisitionResult, capabilities.end());
    ASSERT_NE (recordingResult, capabilities.end());
    ASSERT_NE (filenameResult, capabilities.end());
    ASSERT_NE (directoryResult, capabilities.end());
    ASSERT_NE (cpuResult, capabilities.end());
    ASSERT_NE (optionsResult, capabilities.end());

    const auto& acquisition = *acquisitionResult;
    const auto& recording = *recordingResult;
    const auto& filename = *filenameResult;
    const auto& directory = *directoryResult;
    const auto& cpu = *cpuResult;
    const auto& options = *optionsResult;

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

    EXPECT_FALSE (filename.contains ("mode_semantics"));
    EXPECT_FALSE (directory.contains ("mode_semantics"));
    EXPECT_FALSE (cpu.contains ("mode_semantics"));
    EXPECT_FALSE (options.contains ("mode_semantics"));
}
