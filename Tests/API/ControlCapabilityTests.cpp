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
    std::vector<ExpectedApiOperation> operations;
};

// Official v1.1.0 only backs status / recording / cpu for Core R0 controls.
// Order is stable and intentional. Operations reference shared route descriptors.
const std::vector<ExpectedCapability> expectedCapabilities {
    { "oe.control.acquisition",
      { { "read", OpenEphysHttpApi::kStatusGet },
        { "set", OpenEphysHttpApi::kStatusPut } } },
    { "oe.control.recording",
      { { "read", OpenEphysHttpApi::kStatusGet },
        { "set", OpenEphysHttpApi::kStatusPut } } },
    { "oe.control.recording.filename",
      { { "read", OpenEphysHttpApi::kRecordingGet },
        { "set", OpenEphysHttpApi::kRecordingPut } } },
    { "oe.status.cpu_usage",
      { { "read", OpenEphysHttpApi::kCpuGet } } },
};

// Exact shared descriptors for Core R0 routes the manifest may advertise.
const OpenEphysHttpApi::Route coreR0RouteDescriptors[] = {
    OpenEphysHttpApi::kStatusGet,
    OpenEphysHttpApi::kStatusPut,
    OpenEphysHttpApi::kRecordingGet,
    OpenEphysHttpApi::kRecordingPut,
    OpenEphysHttpApi::kCpuGet,
};

// Shared descriptors also cover GET /api/capabilities (discovery endpoint).
const OpenEphysHttpApi::Route sharedRegisteredRouteDescriptors[] = {
    OpenEphysHttpApi::kCapabilitiesGet,
    OpenEphysHttpApi::kStatusGet,
    OpenEphysHttpApi::kStatusPut,
    OpenEphysHttpApi::kRecordingGet,
    OpenEphysHttpApi::kRecordingPut,
    OpenEphysHttpApi::kCpuGet,
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

    EXPECT_EQ (OpenEphysHttpApi::kStatusGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kStatusGet.path, "/api/status");
    EXPECT_STREQ (OpenEphysHttpApi::kStatusGet.methodString(), "GET");

    EXPECT_EQ (OpenEphysHttpApi::kStatusPut.method, OpenEphysHttpApi::Method::Put);
    EXPECT_STREQ (OpenEphysHttpApi::kStatusPut.path, "/api/status");
    EXPECT_STREQ (OpenEphysHttpApi::kStatusPut.methodString(), "PUT");

    EXPECT_EQ (OpenEphysHttpApi::kRecordingGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingGet.path, "/api/recording");
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingGet.methodString(), "GET");

    EXPECT_EQ (OpenEphysHttpApi::kRecordingPut.method, OpenEphysHttpApi::Method::Put);
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingPut.path, "/api/recording");
    EXPECT_STREQ (OpenEphysHttpApi::kRecordingPut.methodString(), "PUT");

    EXPECT_EQ (OpenEphysHttpApi::kCpuGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kCpuGet.path, "/api/cpu");
    EXPECT_STREQ (OpenEphysHttpApi::kCpuGet.methodString(), "GET");

    ASSERT_EQ (sizeof (sharedRegisteredRouteDescriptors) / sizeof (sharedRegisteredRouteDescriptors[0]), (size_t) 6);
    ASSERT_EQ (sizeof (coreR0RouteDescriptors) / sizeof (coreR0RouteDescriptors[0]), (size_t) 5);

    EXPECT_TRUE (routesEqual (sharedRegisteredRouteDescriptors[0], OpenEphysHttpApi::kCapabilitiesGet));
    EXPECT_TRUE (routesEqual (sharedRegisteredRouteDescriptors[1], OpenEphysHttpApi::kStatusGet));
    EXPECT_TRUE (routesEqual (sharedRegisteredRouteDescriptors[2], OpenEphysHttpApi::kStatusPut));
    EXPECT_TRUE (routesEqual (sharedRegisteredRouteDescriptors[3], OpenEphysHttpApi::kRecordingGet));
    EXPECT_TRUE (routesEqual (sharedRegisteredRouteDescriptors[4], OpenEphysHttpApi::kRecordingPut));
    EXPECT_TRUE (routesEqual (sharedRegisteredRouteDescriptors[5], OpenEphysHttpApi::kCpuGet));
}

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
            EXPECT_EQ (operation.route.method, expectedOp.route.method) << expectedId << " op " << j;
            EXPECT_STREQ (operation.route.path, expectedOp.route.path) << expectedId << " op " << j;
            EXPECT_TRUE (routesEqual (operation.route, expectedOp.route)) << expectedId << " op " << j;
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
            EXPECT_TRUE (isRegisteredCoreR0Route (operation.route))
                << capability.id << " advertises unregistered "
                << operation.route.methodString() << " " << operation.route.path;
        }
    }

    // Exact descriptors for each canonical capability (not merely nonempty strings).
    const auto* acquisition = findControlCapability ("oe.control.acquisition");
    const auto* recording = findControlCapability ("oe.control.recording");
    const auto* filename = findControlCapability ("oe.control.recording.filename");
    const auto* cpu = findControlCapability ("oe.status.cpu_usage");

    ASSERT_NE (acquisition, nullptr);
    ASSERT_NE (recording, nullptr);
    ASSERT_NE (filename, nullptr);
    ASSERT_NE (cpu, nullptr);

    ASSERT_EQ (acquisition->operations.size(), (size_t) 2);
    EXPECT_TRUE (routesEqual (acquisition->operations[0].route, OpenEphysHttpApi::kStatusGet));
    EXPECT_TRUE (routesEqual (acquisition->operations[1].route, OpenEphysHttpApi::kStatusPut));

    ASSERT_EQ (recording->operations.size(), (size_t) 2);
    EXPECT_TRUE (routesEqual (recording->operations[0].route, OpenEphysHttpApi::kStatusGet));
    EXPECT_TRUE (routesEqual (recording->operations[1].route, OpenEphysHttpApi::kStatusPut));

    ASSERT_EQ (filename->operations.size(), (size_t) 2);
    EXPECT_TRUE (routesEqual (filename->operations[0].route, OpenEphysHttpApi::kRecordingGet));
    EXPECT_TRUE (routesEqual (filename->operations[1].route, OpenEphysHttpApi::kRecordingPut));

    ASSERT_EQ (cpu->operations.size(), (size_t) 1);
    EXPECT_TRUE (routesEqual (cpu->operations[0].route, OpenEphysHttpApi::kCpuGet));
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
            EXPECT_EQ (operation["method"].get<std::string>(), expectedOp.route.methodString()) << expectedId;
            EXPECT_EQ (operation["path"].get<std::string>(), expectedOp.route.path) << expectedId;
        }
    }

    const auto acquisition = std::find_if (document["capabilities"].begin(),
                                           document["capabilities"].end(),
                                           [] (const auto& item)
                                           { return item["id"] == "oe.control.acquisition"; });
    ASSERT_NE (acquisition, document["capabilities"].end());
    EXPECT_FALSE ((*acquisition).contains ("uia"));
    EXPECT_EQ ((*acquisition)["api"][0]["path"], OpenEphysHttpApi::kStatusGet.path);
    EXPECT_EQ ((*acquisition)["api"][0]["method"], OpenEphysHttpApi::kStatusGet.methodString());
}
