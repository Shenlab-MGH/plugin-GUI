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

#ifndef SETTINGS_DISPATCH_H
#define SETTINGS_DISPATCH_H

#include "json.hpp"
#include "../../JuceLibraryCode/JuceHeader.h"

#include <functional>
#include <optional>
#include <string>

using json = nlohmann::json;

template <typename Value>
std::optional<Value> optionalSettingsField (const json& document,
                                            const char* key)
{
    try
    {
        return document.at (key).get<Value>();
    }
    catch (const json::exception&)
    {
        return std::nullopt;
    }
}

struct AudioSettingsUpdate
{
    std::optional<std::string> deviceType, deviceName;
    std::optional<int> sampleRate, bufferSize;
};

struct AudioSettingsOperations
{
    std::function<void (const String&)> setDeviceType, setDeviceName;
    std::function<void (int)> setSampleRate, setBufferSize;
    std::function<void()> updateGraphBufferSize;
    std::function<json()> snapshot;
};

inline json applyAudioSettingsUpdate (const AudioSettingsUpdate& update,
                                      const AudioSettingsOperations& operations)
{
    if (update.deviceType) operations.setDeviceType (*update.deviceType);
    if (update.deviceName) operations.setDeviceName (*update.deviceName);
    if (update.sampleRate) operations.setSampleRate (*update.sampleRate);
    if (update.bufferSize)
    {
        operations.setBufferSize (*update.bufferSize);
        operations.updateGraphBufferSize();
    }
    return operations.snapshot();
}

struct RecordingSettingsUpdate
{
    std::optional<std::string> parentDirectory, prependText, baseText, appendText, defaultRecordEngine, startNewDirectory;
};

struct RecordingSettingsOperations
{
    std::function<void (const String&)> setParentDirectory, setPrependText, setBaseText, setAppendText;
    std::function<bool (const String&)> setDefaultEngine;
    std::function<void()> createNewDirectory;
    std::function<json()> snapshot;
};

inline json applyRecordingSettingsUpdate (const RecordingSettingsUpdate& update,
                                          const RecordingSettingsOperations& operations)
{
    if (update.parentDirectory) operations.setParentDirectory (*update.parentDirectory);
    if (update.prependText) operations.setPrependText (*update.prependText);
    if (update.baseText) operations.setBaseText (*update.baseText);
    if (update.appendText) operations.setAppendText (*update.appendText);
    if (update.defaultRecordEngine) operations.setDefaultEngine (*update.defaultRecordEngine);
    if (update.startNewDirectory && *update.startNewDirectory == "true") operations.createNewDirectory();
    return operations.snapshot();
}

struct RecordNodeSettingsUpdate
{
    int id;
    std::optional<std::string> parentDirectory, recordEngine;
};

struct RecordNodeSettingsOperations
{
    std::function<void (const String&, int)> setDirectory, setEngine;
    std::function<json()> snapshot;
};

inline json applyRecordNodeSettingsUpdate (const RecordNodeSettingsUpdate& update,
                                           const RecordNodeSettingsOperations& operations)
{
    if (update.parentDirectory) operations.setDirectory (*update.parentDirectory, update.id);
    if (update.recordEngine) operations.setEngine (*update.recordEngine, update.id);
    return operations.snapshot();
}

#endif
