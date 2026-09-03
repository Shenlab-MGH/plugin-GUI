#include "../../Source/Utils/NeuropixelsPresetApiHandler.h"
#include "gtest/gtest.h"

#include <memory>

namespace
{
using json = nlohmann::json;

const auto hashA = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const auto hashB = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

json capabilityReply (const char* version = "0.0.5")
{
    return { { "ok", true },
             { "capability",
               { { "id", "oe.control.neuropixels.preset" },
                 { "version", version },
                 { "operations", { "inventory", "set" } } } } };
}

json inventoryResult()
{
    return {
        { "inventory_generation", "generation-7" },
        { "targets",
          { { { "processor_id", 100 },
              { "probe_id", "probe-1" },
              { "probe_serial", "1234" },
              { "slot", 2 },
              { "port", 1 },
              { "dock", 1 },
              { "available_presets",
                { { { "preset_id", "bank-a" }, { "preset_label", "Bank A" }, { "electrode_map_hash", hashA } },
                  { { "preset_id", "bank-b" }, { "preset_label", "Bank B" }, { "electrode_map_hash", hashB } } } },
              { "selected",
                { { "selection_kind", "preset" },
                  { "preset_id", "bank-a" },
                  { "preset_label", "Bank A" },
                  { "electrode_map_hash", hashA } } } } } } };
}

json setRequest()
{
    return { { "processor_id", 100 },
             { "probe_id", "probe-1" },
             { "expected_probe_serial", "1234" },
             { "expected_inventory_generation", "generation-7" },
             { "preset_id", "bank-b" },
             { "expected_electrode_map_hash", hashB } };
}

json readback (const char* presetId, const char* label, const char* hash)
{
    return { { "processor_id", 100 },
             { "probe_id", "probe-1" },
             { "probe_serial", "1234" },
             { "preset_id", presetId },
             { "preset_label", label },
             { "electrode_map_hash", hash } };
}

json setResult()
{
    return { { "changed", true },
             { "before", readback ("bank-a", "Bank A", hashA) },
             { "requested", setRequest() },
             { "after", readback ("bank-b", "Bank B", hashB) } };
}

PresetApiBackend backendWithExactPlugin()
{
    auto applied = std::make_shared<bool> (false);
    return {
        [] { return std::vector<int> { 100 }; },
        [applied] (int processorId, const json& request) -> std::optional<json>
        {
            if (processorId != 100)
                return std::nullopt;
            const auto operation = request.at ("operation").get<std::string>();
            if (operation == "capability")
                return capabilityReply();
            if (operation == "inventory")
            {
                auto inventory = inventoryResult();
                if (*applied)
                    inventory["targets"][0]["selected"] = {
                        { "selection_kind", "preset" },
                        { "preset_id", "bank-b" },
                        { "preset_label", "Bank B" },
                        { "electrode_map_hash", hashB }
                    };
                return json { { "ok", true }, { "result", inventory } };
            }
            if (operation == "set")
            {
                *applied = true;
                return json { { "ok", true }, { "accepted", true }, { "operation_id", "preset-op-1" } };
            }
            return std::nullopt;
        },
        [] { return PresetControlMode::Idle; },
        [] (std::chrono::milliseconds) {}
    };
}
} // namespace

TEST (NeuropixelsPresetApiHandlerTests, DefinesExactPublicRoutes)
{
    EXPECT_EQ (OpenEphysHttpApi::kNeuropixelsPresetsGet.method, OpenEphysHttpApi::Method::Get);
    EXPECT_STREQ (OpenEphysHttpApi::kNeuropixelsPresetsGet.path, "/api/plugins/neuropixels/presets");
    EXPECT_EQ (OpenEphysHttpApi::kNeuropixelsPresetSelectedPut.method, OpenEphysHttpApi::Method::Put);
    EXPECT_STREQ (OpenEphysHttpApi::kNeuropixelsPresetSelectedPut.path, "/api/plugins/neuropixels/presets/selected");
}

TEST (NeuropixelsPresetApiHandlerTests, EntireHttpServerUsesLoopbackSecurityBoundary)
{
    EXPECT_STREQ (OpenEphysHttpApi::kListenAddress, "127.0.0.1");
}

TEST (NeuropixelsPresetApiHandlerTests, AdvertisesOnlyAnExactLivePluginCapability)
{
    auto backend = backendWithExactPlugin();
    auto document = nlohmann::json { { "contract_version", "0.0.5" }, { "capabilities", json::array() } };
    appendNeuropixelsPresetCapabilityIfAvailable (document, backend);
    ASSERT_EQ (document["capabilities"].size(), 1u);
    EXPECT_EQ (document["capabilities"][0].size(), 3u);
    EXPECT_EQ (document["capabilities"][0]["id"], "oe.control.neuropixels.preset");
    EXPECT_EQ (document["capabilities"][0]["version"], "0.0.5");
    EXPECT_EQ (document["capabilities"][0]["operations"], json::array ({ "inventory", "set" }));
    EXPECT_EQ (document["contract_version"], "0.0.5");

    backend.dispatch = [] (int, const json&) -> std::optional<json> { return capabilityReply ("0.0.4"); };
    document["capabilities"] = json::array();
    appendNeuropixelsPresetCapabilityIfAvailable (document, backend);
    EXPECT_TRUE (document["capabilities"].empty());

    backend.dispatch = [] (int, const json&) -> std::optional<json> { return std::nullopt; };
    appendNeuropixelsPresetCapabilityIfAvailable (document, backend);
    EXPECT_TRUE (document["capabilities"].empty());
}

TEST (NeuropixelsPresetApiHandlerTests, MultipleCompatibleProcessorsFailClosed)
{
    auto backend = backendWithExactPlugin();
    auto setDispatches = std::make_shared<int> (0);
    backend.processorIds = [] { return std::vector<int> { 100, 101 }; };
    const auto originalDispatch = backend.dispatch;
    backend.dispatch = [originalDispatch, setDispatches] (int processorId, const json& request) -> std::optional<json>
    {
        if (request.at ("operation") == "capability")
            return capabilityReply();
        if (request.at ("operation") == "set")
            ++*setDispatches;
        return originalDispatch (processorId, request);
    };
    auto document = json { { "contract_version", "0.0.5" }, { "capabilities", json::array() } };
    appendNeuropixelsPresetCapabilityIfAvailable (document, backend);
    ASSERT_EQ (document["capabilities"].size(), 1u);
    EXPECT_EQ (document["capabilities"][0]["id"], "oe.control.neuropixels.preset");

    const auto getResult = getNeuropixelsPresets (backend);
    EXPECT_EQ (getResult.httpStatus, 409);
    EXPECT_EQ (getResult.body["error"]["code"], "ambiguous_target");

    const auto putResult = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (putResult.httpStatus, 409);
    EXPECT_EQ (putResult.body["error"]["code"], "ambiguous_target");
    EXPECT_EQ (*setDispatches, 0);
}

TEST (NeuropixelsPresetApiHandlerTests, InventoryUnwrapsOnlyClosedContractOutput)
{
    const auto result = getNeuropixelsPresets (backendWithExactPlugin());
    EXPECT_EQ (result.httpStatus, 200);
    EXPECT_EQ (result.body, inventoryResult());

    auto backend = backendWithExactPlugin();
    backend.dispatch = [] (int, const json& request) -> std::optional<json>
    {
        if (request.at ("operation") == "capability")
            return capabilityReply();
        auto malformed = inventoryResult();
        malformed["unexpected"] = true;
        return json { { "ok", true }, { "result", malformed } };
    };
    const auto malformed = getNeuropixelsPresets (backend);
    EXPECT_EQ (malformed.httpStatus, 502);
    EXPECT_EQ (malformed.body["error"]["code"], "response_schema_mismatch");

    backend = backendWithExactPlugin();
    const auto originalDispatch = backend.dispatch;
    backend.dispatch = [originalDispatch] (int id, const json& request) -> std::optional<json>
    {
        auto response = originalDispatch (id, request);
        if (request.at ("operation") == "inventory")
            (*response)["result"]["targets"].push_back ((*response)["result"]["targets"][0]);
        return response;
    };
    const auto duplicateProbe = getNeuropixelsPresets (backend);
    EXPECT_EQ (duplicateProbe.httpStatus, 502);
    EXPECT_EQ (duplicateProbe.body["error"]["code"], "response_schema_mismatch");
}

TEST (NeuropixelsPresetApiHandlerTests, SetRejectsNonIdleAndClosedSchemaBeforeDispatch)
{
    auto backend = backendWithExactPlugin();
    int setDispatches = 0;
    const auto originalDispatch = backend.dispatch;
    backend.dispatch = [&] (int id, const json& request) -> std::optional<json>
    {
        if (request.at ("operation") == "set")
            ++setDispatches;
        return originalDispatch (id, request);
    };
    backend.readMode = [] { return PresetControlMode::Acquire; };

    auto active = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (active.httpStatus, 409);
    EXPECT_EQ (active.body["error"]["code"], "preset_change_requires_idle");
    EXPECT_EQ (setDispatches, 0);

    backend.readMode = [] { return PresetControlMode::Idle; };
    auto request = setRequest();
    request["unexpected"] = true;
    auto invalid = setNeuropixelsPreset (request, backend);
    EXPECT_EQ (invalid.httpStatus, 400);
    EXPECT_EQ (invalid.body["error"]["code"], "invalid_arguments");
    EXPECT_EQ (setDispatches, 0);
}

TEST (NeuropixelsPresetApiHandlerTests, SetRejectsStaleRequestedPresetHashBeforeDispatch)
{
    auto backend = backendWithExactPlugin();
    int setDispatches = 0;
    const auto originalDispatch = backend.dispatch;
    backend.dispatch = [&] (int id, const json& request) -> std::optional<json>
    {
        if (request.at ("operation") == "set")
            ++setDispatches;
        return originalDispatch (id, request);
    };
    auto request = setRequest();
    request["expected_electrode_map_hash"] = hashA;
    const auto result = setNeuropixelsPreset (request, backend);
    EXPECT_EQ (result.httpStatus, 409);
    EXPECT_EQ (result.body["error"]["code"], "electrode_map_expectation_mismatch");
    EXPECT_EQ (setDispatches, 0);
}

TEST (NeuropixelsPresetApiHandlerTests, MissingPresetIdInFreshCatalogIsNotFound)
{
    auto backend = backendWithExactPlugin();
    auto request = setRequest();
    request["preset_id"] = "not-in-fresh-catalog";
    const auto result = setNeuropixelsPreset (request, backend);
    EXPECT_EQ (result.httpStatus, 404);
    EXPECT_EQ (result.body["error"]["code"], "preset_not_found");
}

TEST (NeuropixelsPresetApiHandlerTests, SetTargetsProcessorAndUnwrapsTypedResponse)
{
    auto backend = backendWithExactPlugin();
    const auto result = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (result.httpStatus, 200);
    EXPECT_EQ (result.body, setResult());

    auto missing = setRequest();
    missing["processor_id"] = 999;
    const auto notFound = setNeuropixelsPreset (missing, backend);
    EXPECT_EQ (notFound.httpStatus, 404);
    EXPECT_EQ (notFound.body["error"]["code"], "processor_not_found");

    backend.processorIds = [] { return std::vector<int>(); };
    const auto unavailable = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (unavailable.httpStatus, 503);
    EXPECT_EQ (unavailable.body["error"]["code"], "capability_unavailable");
}

TEST (NeuropixelsPresetApiHandlerTests, IdempotentNoopRequiresIdLabelAndHashToMatch)
{
    auto backend = backendWithExactPlugin();
    const auto originalDispatch = backend.dispatch;
    auto setSeen = std::make_shared<bool> (false);
    int setDispatches = 0;
    backend.dispatch = [originalDispatch, setSeen, &setDispatches] (int id, const json& request) -> std::optional<json>
    {
        const auto operation = request.at ("operation").get<std::string>();
        if (operation == "set")
        {
            *setSeen = true;
            ++setDispatches;
        }
        auto response = originalDispatch (id, request);
        if (operation == "inventory" && ! *setSeen)
        {
            (*response)["result"]["targets"][0]["selected"] = {
                { "selection_kind", "preset" },
                { "preset_id", "bank-b" },
                { "preset_label", "stale label" },
                { "electrode_map_hash", hashB }
            };
        }
        return response;
    };
    const auto result = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (result.httpStatus, 200);
    EXPECT_TRUE (result.body["changed"]);
    EXPECT_EQ (setDispatches, 1);
}

TEST (NeuropixelsPresetApiHandlerTests, GenerationDriftAfterAcceptedWriteFailsPostcondition)
{
    auto backend = backendWithExactPlugin();
    const auto originalDispatch = backend.dispatch;
    auto setSeen = std::make_shared<bool> (false);
    backend.dispatch = [originalDispatch, setSeen] (int id, const json& request) -> std::optional<json>
    {
        const auto operation = request.at ("operation").get<std::string>();
        if (operation == "set")
            *setSeen = true;
        auto response = originalDispatch (id, request);
        if (operation == "inventory" && *setSeen)
            (*response)["result"]["inventory_generation"] = "generation-8";
        return response;
    };
    const auto result = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (result.httpStatus, 409);
    EXPECT_EQ (result.body["error"]["code"], "postcondition_failed");
}

TEST (NeuropixelsPresetApiHandlerTests, ReadbackLossAfterOldObservationHasUnknownOutcome)
{
    auto backend = backendWithExactPlugin();
    const auto originalDispatch = backend.dispatch;
    auto setSeen = std::make_shared<bool> (false);
    auto postSetInventoryReads = std::make_shared<int> (0);
    backend.dispatch = [originalDispatch, setSeen, postSetInventoryReads]
        (int id, const json& request) -> std::optional<json>
    {
        const auto operation = request.at ("operation").get<std::string>();
        if (operation == "set")
            *setSeen = true;
        if (operation == "inventory" && *setSeen)
        {
            ++*postSetInventoryReads;
            if (*postSetInventoryReads > 1)
                return std::nullopt;
            auto oldState = inventoryResult();
            return json { { "ok", true }, { "result", oldState } };
        }
        return originalDispatch (id, request);
    };

    const auto result = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (result.httpStatus, 503);
    EXPECT_EQ (result.body["error"]["code"], "mutation_outcome_unknown");
}

TEST (NeuropixelsPresetApiHandlerTests, ContinuouslyAuthoritativeOldReadbackFailsPostcondition)
{
    auto backend = backendWithExactPlugin();
    const auto originalDispatch = backend.dispatch;
    auto setSeen = std::make_shared<bool> (false);
    backend.dispatch = [originalDispatch, setSeen] (int id, const json& request) -> std::optional<json>
    {
        const auto operation = request.at ("operation").get<std::string>();
        if (operation == "set")
            *setSeen = true;
        if (operation == "inventory" && *setSeen)
            return json { { "ok", true }, { "result", inventoryResult() } };
        return originalDispatch (id, request);
    };

    const auto result = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (result.httpStatus, 409);
    EXPECT_EQ (result.body["error"]["code"], "postcondition_failed");
}

TEST (NeuropixelsPresetApiHandlerTests, TypedPluginErrorsMapToHttpAndRemainTyped)
{
    auto backend = backendWithExactPlugin();
    backend.dispatch = [] (int, const json& request) -> std::optional<json>
    {
        if (request.at ("operation") == "capability")
            return capabilityReply();
        return json { { "ok", false },
                      { "error", { { "code", "inventory_generation_mismatch" }, { "message", "stale inventory" } } } };
    };
    const auto result = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (result.httpStatus, 409);
    EXPECT_EQ (result.body["error"]["code"], "inventory_generation_mismatch");
    EXPECT_EQ (result.body["error"]["message"], "stale inventory");

    backend.dispatch = [] (int, const json& request) -> std::optional<json>
    {
        if (request.at ("operation") == "capability")
            return capabilityReply();
        return json { { "ok", false },
                      { "error", { { "code", "invented_plugin_error" }, { "message", "not contracted" } } } };
    };
    const auto unknown = setNeuropixelsPreset (setRequest(), backend);
    EXPECT_EQ (unknown.httpStatus, 502);
    EXPECT_EQ (unknown.body["error"]["code"], "response_schema_mismatch");
}
