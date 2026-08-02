#include "../../Source/Utils/ControlCapability.h"
#include "../../Source/Utils/ControlCapabilityJson.h"
#include "gtest/gtest.h"

namespace
{
const StringArray expectedCapabilityIds {
    "oe.control.acquisition",
    "oe.control.recording",
    "oe.control.recording.options",
    "oe.control.recording.filename",
    "oe.control.recording.new_directory",
    "oe.control.recording.force_new_directory",
    "oe.status.cpu_usage",
    "oe.status.disk_usage",
    "oe.status.elapsed_time"
};
} // namespace

TEST (ControlCapabilityTests, DefinesStableCoreControlContracts)
{
    const auto& capabilities = getCoreControlCapabilities();

    ASSERT_EQ (capabilities.size(), (size_t) expectedCapabilityIds.size());

    for (size_t i = 0; i < capabilities.size(); ++i)
    {
        const auto& capability = capabilities[i];
        const auto& expectedId = expectedCapabilityIds[(int) i];

        EXPECT_EQ (capability.id, expectedId) << "capability order mismatch at index " << i;
        EXPECT_EQ (capability.uiaAutomationId, expectedId) << expectedId;
        EXPECT_TRUE (capability.name.isNotEmpty()) << expectedId;
        EXPECT_TRUE (capability.description.isNotEmpty()) << expectedId;
        ASSERT_FALSE (capability.operations.empty()) << expectedId;

        for (const auto& operation : capability.operations)
        {
            EXPECT_TRUE (operation.operation.isNotEmpty()) << expectedId;
            EXPECT_TRUE (operation.method.isNotEmpty()) << expectedId;
            EXPECT_TRUE (operation.path.isNotEmpty()) << expectedId;
        }

        const auto* found = findControlCapability (expectedId);
        ASSERT_NE (found, nullptr) << expectedId;
        EXPECT_EQ (found->id, expectedId);
        EXPECT_EQ (found->uiaAutomationId, expectedId);
    }
}

TEST (ControlCapabilityTests, SerialisesDiscoveryOnlyCapabilityContract)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());

    EXPECT_EQ (document["contract_version"], "0.1.0");
    EXPECT_EQ (document["surface"], "discovery_only");
    ASSERT_TRUE (document["capabilities"].is_array());
    ASSERT_EQ (document["capabilities"].size(), (size_t) expectedCapabilityIds.size());

    for (size_t i = 0; i < document["capabilities"].size(); ++i)
    {
        const auto& item = document["capabilities"][i];
        const auto expectedId = expectedCapabilityIds[(int) i].toStdString();

        EXPECT_EQ (item["id"], expectedId);
        EXPECT_EQ (item["uia"]["automation_id"], expectedId);
        ASSERT_TRUE (item["api"].is_array());
        ASSERT_FALSE (item["api"].empty()) << expectedId;

        for (const auto& operation : item["api"])
        {
            ASSERT_TRUE (operation.contains ("method")) << expectedId;
            ASSERT_TRUE (operation.contains ("path")) << expectedId;
            EXPECT_FALSE (operation["method"].get<std::string>().empty()) << expectedId;
            EXPECT_FALSE (operation["path"].get<std::string>().empty()) << expectedId;
        }
    }

    const auto acquisition = std::find_if (document["capabilities"].begin(),
                                           document["capabilities"].end(),
                                           [] (const auto& item)
                                           { return item["id"] == "oe.control.acquisition"; });
    ASSERT_NE (acquisition, document["capabilities"].end());
    EXPECT_EQ ((*acquisition)["uia"]["automation_id"], "oe.control.acquisition");
    EXPECT_EQ ((*acquisition)["api"][0]["path"], "/api/status");
    EXPECT_EQ ((*acquisition)["api"][0]["method"], "GET");
}
