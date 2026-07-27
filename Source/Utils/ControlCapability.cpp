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
    static const std::vector<ControlCapability> capabilities {
        { "oe.control.acquisition", "Acquisition", "Start or stop data acquisition.",
          ControlCapabilityKind::toggle, "oe.control.acquisition",
          { { "read", "GET", "/api/status", {}, { "mode" } },
            { "set", "PUT", "/api/status", { "mode" }, { "mode" } } } },
        { "oe.control.recording", "Recording", "Start or stop writing data to disk.",
          ControlCapabilityKind::toggle, "oe.control.recording",
          { { "read", "GET", "/api/status", {}, { "mode" } },
            { "set", "PUT", "/api/status", { "mode" }, { "mode" } } } },
        { "oe.control.recording.options", "Recording options", "Show or hide recording options.",
          ControlCapabilityKind::toggle, "oe.control.recording.options",
          { { "read", "GET", "/api/recording/options", {}, { "expanded" } },
            { "set", "PUT", "/api/recording/options", { "expanded" }, { "expanded" } } } },
        { "oe.control.recording.filename", "Recording filename", "Edit the recording filename.",
          ControlCapabilityKind::collection, "oe.control.recording.filename",
          { { "read", "GET", "/api/recording", {}, { "prepend_text", "base_text", "append_text" } },
            { "set", "PUT", "/api/recording", { "prepend_text", "base_text", "append_text" },
              { "prepend_text", "base_text", "append_text" } } } },
        { "oe.control.recording.directory", "Recording directory", "Choose the parent directory inherited by new Record Nodes.",
          ControlCapabilityKind::value, "oe.control.recording.directory",
          { { "read", "GET", "/api/recording", {}, { "parent_directory" } },
            { "set", "PUT", "/api/recording", { "parent_directory" }, { "parent_directory" } } } },
        { "oe.control.recording.new_directory", "New recording directory", "Start a new data directory for the next recording.",
          ControlCapabilityKind::toggle, "oe.control.recording.new_directory",
          { { "read", "GET", "/api/recording/options", {},
              { "new_directory_requested", "new_directory_request_available" } },
            { "set", "PUT", "/api/recording/options", { "new_directory_requested" },
              { "new_directory_requested", "new_directory_request_available" } } } },
        { "oe.control.recording.force_new_directory", "Force new recording directories", "Force a new data directory for each recording.",
          ControlCapabilityKind::toggle, "oe.control.recording.force_new_directory",
          { { "read", "GET", "/api/recording/options", {}, { "force_new_directory" } },
            { "set", "PUT", "/api/recording/options", { "force_new_directory" }, { "force_new_directory" } } } },
        { "oe.status.cpu_usage", "CPU usage", "Fraction of available processing time used by the signal chain.",
          ControlCapabilityKind::range, "oe.status.cpu_usage",
          { { "read", "GET", "/api/cpu", {}, { "usage" } } } },
        { "oe.status.disk_usage", "Disk usage", "Fraction of recording-volume space currently used.",
          ControlCapabilityKind::range, "oe.status.disk_usage",
          { { "read", "GET", "/api/disk", {}, { "usage" } } } },
        { "oe.status.elapsed_time", "Elapsed time", "Elapsed acquisition or recording time.",
          ControlCapabilityKind::status, "oe.status.elapsed_time",
          { { "read", "GET", "/api/time", {}, { "display" } } } }
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
