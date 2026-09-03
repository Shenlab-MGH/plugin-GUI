#include "NeuropixelsPresetApiHandler.h"

#include <algorithm>
#include <set>
#include <string>

namespace
{
using json = nlohmann::json;

constexpr auto capabilityId = "oe.control.neuropixels.preset";
constexpr auto contractVersion = "0.0.5";

bool hasExactKeys (const json& value,
                   std::initializer_list<const char*> required,
                   std::initializer_list<const char*> optional = {})
{
    if (! value.is_object())
        return false;

    std::set<std::string> allowed;
    for (const auto* key : required)
    {
        allowed.insert (key);
        if (! value.contains (key))
            return false;
    }
    for (const auto* key : optional)
        allowed.insert (key);

    for (auto item = value.begin(); item != value.end(); ++item)
        if (allowed.count (item.key()) == 0)
            return false;
    return true;
}

bool isNonemptyString (const json& value)
{
    return value.is_string() && ! value.get_ref<const std::string&>().empty();
}

bool isMapHash (const json& value)
{
    if (! value.is_string())
        return false;
    const auto& text = value.get_ref<const std::string&>();
    if (text.size() != 71 || text.rfind ("sha256:", 0) != 0)
        return false;
    return std::all_of (text.begin() + 7, text.end(), [] (char character)
    {
        return (character >= '0' && character <= '9')
               || (character >= 'a' && character <= 'f');
    });
}

bool isPreset (const json& value)
{
    return hasExactKeys (value, { "preset_id", "preset_label", "electrode_map_hash" })
           && isNonemptyString (value["preset_id"])
           && isNonemptyString (value["preset_label"])
           && isMapHash (value["electrode_map_hash"]);
}

bool isSelected (const json& value)
{
    if (! hasExactKeys (value, { "selection_kind", "preset_id", "preset_label", "electrode_map_hash" })
        || ! value["selection_kind"].is_string()
        || ! isMapHash (value["electrode_map_hash"]))
        return false;

    const auto kind = value["selection_kind"].get<std::string>();
    if (kind == "preset")
        return isNonemptyString (value["preset_id"]) && isNonemptyString (value["preset_label"]);
    if (kind == "custom")
        return value["preset_id"].is_null() && value["preset_label"].is_null();
    return false;
}

bool isTarget (const json& value)
{
    if (! hasExactKeys (value,
                        { "processor_id", "probe_id", "probe_serial", "slot", "port", "dock",
                          "available_presets", "selected" })
        || ! value["processor_id"].is_number_integer()
        || ! isNonemptyString (value["probe_id"])
        || ! isNonemptyString (value["probe_serial"])
        || ! value["slot"].is_number_integer() || value["slot"].get<int>() < 0
        || ! value["port"].is_number_integer() || value["port"].get<int>() < 0
        || ! value["dock"].is_number_integer() || value["dock"].get<int>() < 0
        || ! value["available_presets"].is_array()
        || ! isSelected (value["selected"]))
        return false;

    std::set<std::string> presetIds;
    for (const auto& preset : value["available_presets"])
    {
        if (! isPreset (preset)
            || ! presetIds.insert (preset["preset_id"].get<std::string>()).second)
            return false;
    }
    return true;
}

bool isInventory (const json& value)
{
    if (! hasExactKeys (value, { "inventory_generation", "targets" })
        || ! isNonemptyString (value["inventory_generation"])
        || ! value["targets"].is_array())
        return false;

    std::set<std::string> probeIds;
    for (const auto& target : value["targets"])
    {
        if (! isTarget (target))
            return false;
        if (! probeIds.insert (target["probe_id"].get<std::string>()).second)
            return false;
    }
    return true;
}

bool isSetRequest (const json& value)
{
    return hasExactKeys (value,
                         { "processor_id", "probe_id", "expected_probe_serial",
                           "expected_inventory_generation", "preset_id",
                           "expected_electrode_map_hash" })
           && value["processor_id"].is_number_integer()
           && isNonemptyString (value["probe_id"])
           && isNonemptyString (value["expected_probe_serial"])
           && isNonemptyString (value["expected_inventory_generation"])
           && isNonemptyString (value["preset_id"])
           && isMapHash (value["expected_electrode_map_hash"]);
}

json envelope (const char* operation, const json& payload = json::object())
{
    return { { "agent_api", capabilityId },
             { "version", contractVersion },
             { "operation", operation },
             { "payload", payload } };
}

bool isExactCapabilityReply (const std::optional<json>& response)
{
    if (! response.has_value()
        || ! hasExactKeys (*response, { "ok", "capability" })
        || (*response)["ok"] != true)
        return false;
    const auto& capability = (*response)["capability"];
    return hasExactKeys (capability, { "id", "version", "operations" })
           && capability["id"] == capabilityId
           && capability["version"] == contractVersion
           && capability["operations"] == json::array ({ "inventory", "set" });
}

std::vector<int> compatibleProcessors (const PresetApiBackend& backend)
{
    std::vector<int> compatible;
    for (const auto processorId : backend.processorIds())
    {
        if (isExactCapabilityReply (backend.dispatch (processorId, envelope ("capability"))))
            compatible.push_back (processorId);
    }
    return compatible;
}

PresetApiResult errorResult (int status, const std::string& code, const std::string& message)
{
    return { status, { { "error", { { "code", code }, { "message", message } } } } };
}

int errorStatus (const std::string& code)
{
    if (code == "invalid_arguments") return 400;
    if (code == "processor_not_found" || code == "probe_not_found" || code == "preset_not_found") return 404;
    if (code == "preset_not_supported") return 422;
    if (code == "inventory_generation_mismatch" || code == "electrode_map_expectation_mismatch"
        || code == "probe_identity_mismatch" || code == "ambiguous_target"
        || code == "preset_change_requires_idle" || code == "preset_apply_in_progress"
        || code == "postcondition_failed") return 409;
    if (code == "capability_unavailable" || code == "mutation_outcome_unknown") return 503;
    if (code == "preset_apply_failed") return 500;
    return 502;
}

std::optional<PresetApiResult> pluginError (const std::optional<json>& response)
{
    if (! response.has_value()
        || ! hasExactKeys (*response, { "ok", "error" })
        || (*response)["ok"] != false
        || ! (*response)["error"].is_object()
        || ! (*response)["error"].contains ("code")
        || ! (*response)["error"].contains ("message")
        || ! isNonemptyString ((*response)["error"]["code"])
        || ! isNonemptyString ((*response)["error"]["message"]))
        return std::nullopt;

    const auto code = (*response)["error"]["code"].get<std::string>();
    static const std::set<std::string> contractedCodes {
        "invalid_arguments", "capability_unavailable", "processor_not_found", "probe_not_found",
        "ambiguous_target", "probe_identity_mismatch", "inventory_generation_mismatch",
        "electrode_map_expectation_mismatch", "preset_not_found", "preset_not_supported",
        "preset_change_requires_idle", "preset_apply_in_progress", "preset_apply_failed",
        "mutation_outcome_unknown", "postcondition_failed", "response_schema_mismatch",
        "open_ephys_unreachable", "api_http_error"
    };
    if (contractedCodes.count (code) == 0)
        return errorResult (502, "response_schema_mismatch", "Plugin returned an uncontracted error code.");
    return PresetApiResult { errorStatus (code), { { "error", (*response)["error"] } } };
}

std::optional<json> inventoryFromResponse (const std::optional<json>& response)
{
    if (! response.has_value()
        || ! hasExactKeys (*response, { "ok", "result" })
        || (*response)["ok"] != true
        || ! isInventory ((*response)["result"]))
        return std::nullopt;
    return (*response)["result"];
}

PresetApiResult readCompatibleInventory (const PresetApiBackend& backend,
                                         const std::vector<int>& processors)
{
    json combined { { "inventory_generation", nullptr }, { "targets", json::array() } };
    for (const auto processorId : processors)
    {
        const auto response = backend.dispatch (processorId, envelope ("inventory"));
        if (const auto pluginFailure = pluginError (response); pluginFailure.has_value())
            return *pluginFailure;
        const auto inventory = inventoryFromResponse (response);
        if (! inventory.has_value())
            return errorResult (502, "response_schema_mismatch", "Plugin inventory response did not match contract 0.0.5.");

        if (combined["inventory_generation"].is_null())
            combined["inventory_generation"] = (*inventory)["inventory_generation"];
        else if (combined["inventory_generation"] != (*inventory)["inventory_generation"])
            return errorResult (502, "response_schema_mismatch", "Compatible processors reported different inventory generations.");

        for (const auto& target : (*inventory)["targets"])
        {
            if (target["processor_id"] != processorId)
                return errorResult (502, "response_schema_mismatch", "Plugin target processor_id did not match the dispatched processor.");
            combined["targets"].push_back (target);
        }
    }
    return { 200, std::move (combined) };
}

json targetReadback (const json& target)
{
    return { { "processor_id", target["processor_id"] },
             { "probe_id", target["probe_id"] },
             { "probe_serial", target["probe_serial"] },
             { "preset_id", target["selected"]["preset_id"] },
             { "preset_label", target["selected"]["preset_label"] },
             { "electrode_map_hash", target["selected"]["electrode_map_hash"] } };
}

std::vector<json> matchingTargets (const json& inventory, const json& request)
{
    std::vector<json> matches;
    for (const auto& target : inventory["targets"])
        if (target["processor_id"] == request["processor_id"]
            && target["probe_id"] == request["probe_id"])
            matches.push_back (target);
    return matches;
}
} // namespace

void appendNeuropixelsPresetCapabilityIfAvailable (json& document,
                                                     const PresetApiBackend& backend)
{
    if (compatibleProcessors (backend).empty())
        return;
    document["contract_version"] = contractVersion;
    document["capabilities"].push_back (
        { { "id", capabilityId },
          { "version", contractVersion },
          { "operations", { "inventory", "set" } } });
}

PresetApiResult getNeuropixelsPresets (const PresetApiBackend& backend)
{
    const auto processors = compatibleProcessors (backend);
    if (processors.empty())
        return errorResult (503, "capability_unavailable", "No loaded processor implements the exact Neuropixels preset 0.0.5 contract.");
    if (processors.size() != 1)
        return errorResult (409, "ambiguous_target", "More than one processor implements the preset contract; core cannot safely compose their generations.");
    return readCompatibleInventory (backend, processors);
}

PresetApiResult setNeuropixelsPreset (const json& request, const PresetApiBackend& backend)
{
    if (! isSetRequest (request))
        return errorResult (400, "invalid_arguments", "Request did not match the closed preset mutation schema.");
    if (backend.readMode() != PresetControlMode::Idle)
        return errorResult (409, "preset_change_requires_idle", "Preset changes require Open Ephys IDLE mode.");

    const auto processors = compatibleProcessors (backend);
    if (processors.empty())
        return errorResult (503, "capability_unavailable", "No loaded processor implements the exact Neuropixels preset 0.0.5 contract.");
    if (processors.size() > 1)
        return errorResult (409, "ambiguous_target", "More than one processor implements the preset contract; mutation is blocked.");
    const auto processorId = request["processor_id"].get<int>();
    if (std::find (processors.begin(), processors.end(), processorId) == processors.end())
        return errorResult (404, "processor_not_found", "The requested compatible processor was not found.");

    const auto beforeInventory = readCompatibleInventory (backend, processors);
    if (beforeInventory.httpStatus != 200)
        return beforeInventory;
    const auto matches = matchingTargets (beforeInventory.body, request);
    if (matches.empty())
        return errorResult (404, "probe_not_found", "The requested probe was not found on the processor.");
    if (matches.size() != 1)
        return errorResult (409, "ambiguous_target", "The processor and probe identity did not resolve uniquely.");

    const auto& target = matches.front();
    if (target["probe_serial"] != request["expected_probe_serial"])
        return errorResult (409, "probe_identity_mismatch", "Probe serial did not match the fresh inventory.");
    if (beforeInventory.body["inventory_generation"] != request["expected_inventory_generation"])
        return errorResult (409, "inventory_generation_mismatch", "Inventory generation did not match the fresh inventory.");
    const auto desiredPreset = std::find_if (target["available_presets"].begin(),
                                             target["available_presets"].end(),
                                             [&request] (const auto& preset)
                                             { return preset["preset_id"] == request["preset_id"]; });
    if (desiredPreset == target["available_presets"].end())
        return errorResult (404, "preset_not_found", "Preset was absent from the fresh target catalog.");
    if ((*desiredPreset)["electrode_map_hash"] != request["expected_electrode_map_hash"])
        return errorResult (409, "electrode_map_expectation_mismatch", "Requested preset row did not match the expected electrode map hash.");

    const auto before = targetReadback (target);
    if (target["selected"]["selection_kind"] == "preset"
        && target["selected"]["preset_id"] == request["preset_id"]
        && target["selected"]["preset_label"] == (*desiredPreset)["preset_label"]
        && target["selected"]["electrode_map_hash"] == (*desiredPreset)["electrode_map_hash"])
        return { 200, { { "changed", false }, { "before", before },
                        { "requested", request }, { "after", before } } };

    const auto writeResponse = backend.dispatch (processorId, envelope ("set", request));
    if (const auto pluginFailure = pluginError (writeResponse); pluginFailure.has_value())
        return *pluginFailure;
    if (! writeResponse.has_value()
        || ! hasExactKeys (*writeResponse, { "ok", "accepted", "operation_id" })
        || (*writeResponse)["ok"] != true
        || (*writeResponse)["accepted"] != true
        || ! isNonemptyString ((*writeResponse)["operation_id"]))
        return errorResult (503, "mutation_outcome_unknown", "Plugin dispatch outcome was not a valid acceptance response; use a fresh getter and do not retry blindly.");

    bool obtainedReadback = false;
    bool lastReadbackWasAuthoritative = false;
    const auto convergenceDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (3000);
    for (int attempt = 0;
         attempt < 30 && std::chrono::steady_clock::now() < convergenceDeadline;
         ++attempt)
    {
        if (attempt != 0)
            backend.wait (std::chrono::milliseconds (100));
        const auto afterInventory = readCompatibleInventory (backend, processors);
        if (afterInventory.httpStatus != 200)
        {
            lastReadbackWasAuthoritative = false;
            continue;
        }
        obtainedReadback = true;
        lastReadbackWasAuthoritative = true;
        if (afterInventory.body["inventory_generation"] != request["expected_inventory_generation"])
            return errorResult (409, "postcondition_failed", "Inventory generation changed during authoritative readback.");
        const auto afterMatches = matchingTargets (afterInventory.body, request);
        if (afterMatches.size() != 1)
            return errorResult (409, "postcondition_failed", "Probe identity was not unique during authoritative readback.");
        const auto& afterTarget = afterMatches.front();
        if (afterTarget["probe_serial"] != request["expected_probe_serial"])
            return errorResult (409, "postcondition_failed", "Probe identity changed during authoritative readback.");
        if (afterTarget["selected"]["selection_kind"] == "preset"
            && afterTarget["selected"]["preset_id"] == request["preset_id"]
            && afterTarget["selected"]["preset_label"] == (*desiredPreset)["preset_label"]
            && afterTarget["selected"]["electrode_map_hash"] == (*desiredPreset)["electrode_map_hash"])
            return { 200, { { "changed", true }, { "before", before },
                            { "requested", request }, { "after", targetReadback (afterTarget) } } };
    }

    if (! obtainedReadback || ! lastReadbackWasAuthoritative)
        return errorResult (503, "mutation_outcome_unknown", "No authoritative readback was available after dispatch; use a fresh getter and do not retry blindly.");
    return errorResult (409, "postcondition_failed", "Authoritative readback did not converge to the requested preset.");
}
