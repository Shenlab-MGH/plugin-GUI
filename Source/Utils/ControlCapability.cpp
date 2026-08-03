/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "ControlCapability.h"
#include <algorithm>

const std::vector<ControlCapability>& getCoreControlCapabilities()
{
    // Agent Core R0: advertise only capabilities backed by official v1.1.0 routes.
    // Order is stable. Discovery-only: no UIA AutomationId claims (UI does not implement them).
    // Operations use shared route descriptors (same Method+path as server registration).
    const ControlModeSemantics acquisitionModeSemantics {
        "mode",
        { "IDLE", "ACQUIRE", "RECORD" },
        { "ACQUIRE", "RECORD" },
        { "IDLE" },
        "ACQUIRE",
        "IDLE",
        "actual_gui_state"
    };
    const ControlModeSemantics recordingModeSemantics {
        "mode",
        { "IDLE", "ACQUIRE", "RECORD" },
        { "RECORD" },
        { "IDLE", "ACQUIRE" },
        "RECORD",
        "ACQUIRE",
        "actual_gui_state"
    };

    static const std::vector<ControlCapability> capabilities {
        { "oe.control.acquisition", "Acquisition", "Start or stop data acquisition.",
          ControlCapabilityKind::toggle,
          { { "read", OpenEphysHttpApi::kStatusGet, {}, { "mode" } },
            { "set", OpenEphysHttpApi::kStatusPut, { "mode" }, { "mode" } } },
          acquisitionModeSemantics },
        { "oe.control.recording", "Recording", "Start or stop writing data to disk.",
          ControlCapabilityKind::toggle,
          { { "read", OpenEphysHttpApi::kStatusGet, {}, { "mode" } },
            { "set", OpenEphysHttpApi::kStatusPut, { "mode" }, { "mode" } } },
          recordingModeSemantics },
        { "oe.control.recording.filename", "Recording filename", "Edit the recording filename.",
          ControlCapabilityKind::collection,
          { { "read", OpenEphysHttpApi::kRecordingGet, {}, { "prepend_text", "base_text", "append_text" } },
            { "set", OpenEphysHttpApi::kRecordingPut, { "prepend_text", "base_text", "append_text" },
              { "prepend_text", "base_text", "append_text" } } } },
        { "oe.status.cpu_usage", "CPU usage", "Fraction of available processing time used by the signal chain.",
          ControlCapabilityKind::range,
          { { "read", OpenEphysHttpApi::kCpuGet, {}, { "usage" } } } }
    };

    return capabilities;
}

const ControlCapability* findControlCapability (StringRef id)
{
    const String target (id);
    const auto& capabilities = getCoreControlCapabilities();
    const auto result = std::find_if (capabilities.begin(),
                                      capabilities.end(),
                                      [&] (const auto& capability)
                                      { return capability.id == target; });
    return result == capabilities.end() ? nullptr : &*result;
}
