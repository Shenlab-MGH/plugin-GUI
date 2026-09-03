/*
    Minimal core bridge for the versioned 0.0.5 Neuropixels preset plugin contract.
    Preset semantics remain plugin-owned; core only validates, forwards, and
    verifies typed JSON on the message thread.
*/

#ifndef NEUROPIXELS_PRESET_API_HANDLER_H
#define NEUROPIXELS_PRESET_API_HANDLER_H

#include "OpenEphysHttpApiRoutes.h"
#include "../TestableExport.h"
#include "json.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <vector>

enum class PresetControlMode
{
    Idle,
    Acquire,
    Record,
    Unknown
};

struct PresetApiBackend
{
    std::function<std::vector<int>()> processorIds;
    std::function<std::optional<nlohmann::json> (int, const nlohmann::json&)> dispatch;
    std::function<PresetControlMode()> readMode;
    std::function<void (std::chrono::milliseconds)> wait;
};

struct PresetApiResult
{
    int httpStatus;
    nlohmann::json body;
};

TESTABLE void appendNeuropixelsPresetCapabilityIfAvailable (
    nlohmann::json& capabilityDocument,
    const PresetApiBackend& backend);

TESTABLE PresetApiResult getNeuropixelsPresets (const PresetApiBackend& backend);
TESTABLE PresetApiResult setNeuropixelsPreset (
    const nlohmann::json& request,
    const PresetApiBackend& backend);

#endif
