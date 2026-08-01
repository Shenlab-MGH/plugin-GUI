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

    ------------------------------------------------------------------
*/

#ifndef STATUS_CONTROL_H
#define STATUS_CONTROL_H

#include "AcquisitionRecordingControl.h"
#include "../TestableExport.h"

#include "../../JuceLibraryCode/JuceHeader.h"

#include <chrono>
#include <functional>
#include <optional>

class MessageThreadCallGeneration;

/**
    Maximum time allowed for a queued status operation to begin on the JUCE
    message thread. Once the operation begins, the caller waits for its
    determinate result instead of reporting a timeout while a mutation may be
    in flight.
*/
inline constexpr auto statusControlQueueStartTimeout =
    std::chrono::seconds (2);

using StatusControlDispatcher =
    std::function<bool (std::function<void()>)>;
using StatusControlReadback =
    std::function<AcquisitionRecordingControlSnapshot()>;
using StatusControlApply =
    std::function<AcquisitionRecordingControlResult (
        const StatusRequest&)>;

/** A transport-neutral result for the shared /api/status contract. */
struct StatusControlResult
{
    int httpStatus = 500;
    std::optional<AcquisitionRecordingMode> requestedMode;
    std::optional<AcquisitionRecordingControlSnapshot> achieved;
    bool changed = false;
    bool unsynchronizedConfirmed = false;
    String errorCode;
    String errorMessage;
};

/**
    Reads one fresh copied snapshot on the JUCE message thread.

    Must be called off the message thread. The dispatcher must enqueue the
    operation and return promptly; the queue-start deadline begins after the
    dispatcher accepts it.
*/
TESTABLE StatusControlResult handleStatusGet (
    StatusControlDispatcher dispatcher,
    StatusControlReadback readback,
    std::chrono::milliseconds queueStartTimeout =
        statusControlQueueStartTimeout);

TESTABLE StatusControlResult handleStatusGet (
    StatusControlDispatcher dispatcher,
    StatusControlReadback readback,
    std::chrono::milliseconds queueStartTimeout,
    MessageThreadCallGeneration& generation);

/**
    Strictly parses a status request before dispatching one controller call to
    the JUCE message thread.

    Must be called off the message thread. The dispatcher must enqueue the
    operation and return promptly; the queue-start deadline begins after the
    dispatcher accepts it.
*/
TESTABLE StatusControlResult handleStatusPut (
    StringRef requestBody,
    StatusControlDispatcher dispatcher,
    StatusControlApply apply,
    std::chrono::milliseconds queueStartTimeout =
        statusControlQueueStartTimeout);

TESTABLE StatusControlResult handleStatusPut (
    StringRef requestBody,
    StatusControlDispatcher dispatcher,
    StatusControlApply apply,
    std::chrono::milliseconds queueStartTimeout,
    MessageThreadCallGeneration& generation);

#endif
