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

#ifndef CONTROL_STATUS_JSON_H
#define CONTROL_STATUS_JSON_H

#include "../../JuceLibraryCode/JuceHeader.h"
#include "AcquisitionRecordingStatus.h"
#include "../TestableExport.h"

#include <optional>

struct StatusRequest
{
    AcquisitionRecordingMode mode = AcquisitionRecordingMode::idle;
    bool confirmUnsynchronized = false;
};

struct StatusRequestParseResult
{
    std::optional<StatusRequest> request;
    String errorCode;
    String error;
};

TESTABLE StatusRequestParseResult parseStatusRequest (StringRef requestBody);

struct RecordingOptionsUpdate
{
    std::optional<bool> expanded;
    std::optional<bool> forceNewDirectory;
    std::optional<bool> newDirectoryRequested;
};

struct RecordingOptionsUpdateParseResult
{
    std::optional<RecordingOptionsUpdate> update;
    String error;
};

TESTABLE RecordingOptionsUpdateParseResult parseRecordingOptionsUpdate (StringRef requestBody);

#endif
