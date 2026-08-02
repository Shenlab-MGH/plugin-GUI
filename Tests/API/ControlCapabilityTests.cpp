#include "../../Source/Utils/ControlCapability.h"
#include "../../Source/Utils/ControlCapabilityJson.h"
#include "../../Source/Utils/OpenEphysHttpApiRoutes.h"
#include "gtest/gtest.h"

#include <utility>
#include <vector>

namespace
{
struct ExpectedApiOperation
{
    const char* operation;
    const char* method;
    const char* path;
};

struct ExpectedCapability
{
    const char* id;
    std::vector<ExpectedApiOperation> operations;
};

// Official v1.1.0 only backs status / recording / cpu for Core R0 controls.
// Order is stable and intentional.
const std::vector<ExpectedCapability> expectedCapabilities {
    { "oe.control.acquisition",
      { { "read", OpenEphysHttpApi::kMethodGet, OpenEphysHttpApi::kPathStatus },
        { "set", OpenEphysHttpApi::kMethodPut, OpenEphysHttpApi::kPathStatus } } },
    { "oe.control.recording",
      { { "read", OpenEphysHttpApi::kMethodGet, OpenEphysHttpApi::kPathStatus },
        { "set", OpenEphysHttpApi::kMethodPut, OpenEphysHttpApi::kPathStatus } } },
    { "oe.control.recording.filename",
      { { "read", OpenEphysHttpApi::kMethodGet, OpenEphysHttpApi::kPathRecording },
        { "set", OpenEphysHttpApi::kMethodPut, OpenEphysHttpApi::kPathRecording } } },
    { "oe.status.cpu_usage",
      { { "read", OpenEphysHttpApi::kMethodGet, OpenEphysHttpApi::kPathCpu } } },
};

// Exact method/path pairs registered for routes the Core R0 manifest may advertise.
const std::vector<std::pair<const char*, const char*>> registeredCoreR0Routes {
    { OpenEphysHttpApi::kMethodGet, OpenEphysHttpApi::kPathStatus },
    { OpenEphysHttpApi::kMethodPut, OpenEphysHttpApi::kPathStatus },
    { OpenEphysHttpApi::kMethodGet, OpenEphysHttpApi::kPathRecording },
    { OpenEphysHttpApi::kMethodPut, OpenEphysHttpApi::kPathRecording },
    { OpenEphysHttpApi::kMethodGet, OpenEphysHttpApi::kPathCpu },
};

bool matchesMethodPath (const String& method, const String& path, const char* expectedMethod, const char* expectedPath)
{
    return method == expectedMethod && path == expectedPath;
}

bool isRegisteredCoreR0Route (const String& method, const String& path)
{
    for (const auto& route : registeredCoreR0Routes)
    {
        if (matchesMethodPath (method, path, route.first, route.second))
            return true;
    }

    return false;
}
} // namespace

TEST (ControlCapabilityTests, DefinesStableCoreControlContracts)
{
    const auto& capabilities = getCoreControlCapabilities();

    ASSERT_EQ (capabilities.size(), expectedCapabilities.size());
    ASSERT_EQ (expectedCapabilities.size(), (size_t) 4);

    for (size_t i = 0; i < capabilities.size(); ++i)
    {
        const auto& capability = capabilities[i];
        const auto& expected = expectedCapabilities[i];
        const String expectedId (expected.id);

        EXPECT_EQ (capability.id, expectedId) << "capability order mismatch at index " << i;
        EXPECT_TRUE (capability.name.isNotEmpty()) << expectedId;
        EXPECT_TRUE (capability.description.isNotEmpty()) << expectedId;

        ASSERT_EQ (capability.operations.size(), expected.operations.size()) << expectedId;

        for (size_t j = 0; j < capability.operations.size(); ++j)
        {
            const auto& operation = capability.operations[j];
            const auto& expectedOp = expected.operations[j];

            EXPECT_EQ (operation.operation, String (expectedOp.operation)) << expectedId << " op " << j;
            EXPECT_EQ (operation.method, String (expectedOp.method)) << expectedId << " op " << j;
            EXPECT_EQ (operation.path, String (expectedOp.path)) << expectedId << " op " << j;
        }

        const auto* found = findControlCapability (expectedId);
        ASSERT_NE (found, nullptr) << expectedId;
        EXPECT_EQ (found->id, expectedId);
    }

    // Capabilities that would require unregistered endpoints must not appear.
    EXPECT_EQ (findControlCapability ("oe.control.recording.options"), nullptr);
    EXPECT_EQ (findControlCapability ("oe.control.recording.new_directory"), nullptr);
    EXPECT_EQ (findControlCapability ("oe.control.recording.force_new_directory"), nullptr);
    EXPECT_EQ (findControlCapability ("oe.status.disk_usage"), nullptr);
    EXPECT_EQ (findControlCapability ("oe.status.elapsed_time"), nullptr);
}

TEST (ControlCapabilityTests, ManifestOperationsMatchRegisteredCoreRoutes)
{
    const auto& capabilities = getCoreControlCapabilities();

    ASSERT_EQ (capabilities.size(), (size_t) 4);

    for (const auto& capability : capabilities)
    {
        ASSERT_FALSE (capability.operations.empty()) << capability.id;

        for (const auto& operation : capability.operations)
        {
            EXPECT_TRUE (isRegisteredCoreR0Route (operation.method, operation.path))
                << capability.id << " advertises unregistered "
                << operation.method << " " << operation.path;
        }
    }

    // Exact pairs for each canonical capability (not merely nonempty strings).
    const auto* acquisition = findControlCapability ("oe.control.acquisition");
    const auto* recording = findControlCapability ("oe.control.recording");
    const auto* filename = findControlCapability ("oe.control.recording.filename");
    const auto* cpu = findControlCapability ("oe.status.cpu_usage");

    ASSERT_NE (acquisition, nullptr);
    ASSERT_NE (recording, nullptr);
    ASSERT_NE (filename, nullptr);
    ASSERT_NE (cpu, nullptr);

    ASSERT_EQ (acquisition->operations.size(), (size_t) 2);
    EXPECT_TRUE (matchesMethodPath (acquisition->operations[0].method,
                                    acquisition->operations[0].path,
                                    OpenEphysHttpApi::kMethodGet,
                                    OpenEphysHttpApi::kPathStatus));
    EXPECT_TRUE (matchesMethodPath (acquisition->operations[1].method,
                                    acquisition->operations[1].path,
                                    OpenEphysHttpApi::kMethodPut,
                                    OpenEphysHttpApi::kPathStatus));

    ASSERT_EQ (recording->operations.size(), (size_t) 2);
    EXPECT_TRUE (matchesMethodPath (recording->operations[0].method,
                                    recording->operations[0].path,
                                    OpenEphysHttpApi::kMethodGet,
                                    OpenEphysHttpApi::kPathStatus));
    EXPECT_TRUE (matchesMethodPath (recording->operations[1].method,
                                    recording->operations[1].path,
                                    OpenEphysHttpApi::kMethodPut,
                                    OpenEphysHttpApi::kPathStatus));

    ASSERT_EQ (filename->operations.size(), (size_t) 2);
    EXPECT_TRUE (matchesMethodPath (filename->operations[0].method,
                                    filename->operations[0].path,
                                    OpenEphysHttpApi::kMethodGet,
                                    OpenEphysHttpApi::kPathRecording));
    EXPECT_TRUE (matchesMethodPath (filename->operations[1].method,
                                    filename->operations[1].path,
                                    OpenEphysHttpApi::kMethodPut,
                                    OpenEphysHttpApi::kPathRecording));

    ASSERT_EQ (cpu->operations.size(), (size_t) 1);
    EXPECT_TRUE (matchesMethodPath (cpu->operations[0].method,
                                    cpu->operations[0].path,
                                    OpenEphysHttpApi::kMethodGet,
                                    OpenEphysHttpApi::kPathCpu));
}

TEST (ControlCapabilityTests, SerialisesDiscoveryOnlyCapabilityContract)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());

    EXPECT_EQ (document["contract_version"], "0.1.0");
    EXPECT_EQ (document["surface"], "discovery_only");
    ASSERT_TRUE (document["capabilities"].is_array());
    ASSERT_EQ (document["capabilities"].size(), expectedCapabilities.size());
    ASSERT_EQ (document["capabilities"].size(), (size_t) 4);

    for (size_t i = 0; i < document["capabilities"].size(); ++i)
    {
        const auto& item = document["capabilities"][i];
        const auto& expected = expectedCapabilities[i];
        const auto expectedId = std::string (expected.id);

        EXPECT_EQ (item["id"], expectedId);
        // R0 is API-discovery-only: official v1.1.0 UI does not implement AutomationIds.
        EXPECT_FALSE (item.contains ("uia")) << expectedId;
        ASSERT_TRUE (item["api"].is_array());
        ASSERT_EQ (item["api"].size(), expected.operations.size()) << expectedId;

        for (size_t j = 0; j < item["api"].size(); ++j)
        {
            const auto& operation = item["api"][j];
            const auto& expectedOp = expected.operations[j];

            ASSERT_TRUE (operation.contains ("method")) << expectedId;
            ASSERT_TRUE (operation.contains ("path")) << expectedId;
            EXPECT_EQ (operation["operation"].get<std::string>(), expectedOp.operation) << expectedId;
            EXPECT_EQ (operation["method"].get<std::string>(), expectedOp.method) << expectedId;
            EXPECT_EQ (operation["path"].get<std::string>(), expectedOp.path) << expectedId;
        }
    }

    const auto acquisition = std::find_if (document["capabilities"].begin(),
                                           document["capabilities"].end(),
                                           [] (const auto& item)
                                           { return item["id"] == "oe.control.acquisition"; });
    ASSERT_NE (acquisition, document["capabilities"].end());
    EXPECT_FALSE ((*acquisition).contains ("uia"));
    EXPECT_EQ ((*acquisition)["api"][0]["path"], OpenEphysHttpApi::kPathStatus);
    EXPECT_EQ ((*acquisition)["api"][0]["method"], OpenEphysHttpApi::kMethodGet);
}
