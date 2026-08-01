#ifndef SETTINGS_DISPATCH_H
#define SETTINGS_DISPATCH_H

#include "json.hpp"
#include "../../JuceLibraryCode/JuceHeader.h"

#include <functional>
#include <optional>
#include <string>

using json = nlohmann::json;

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
