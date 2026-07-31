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

#include "StatusControl.h"

#include "ControlStatusJson.h"
#include "MessageThreadCall.h"

#include <utility>

namespace
{
using Error = AcquisitionRecordingControlError;
using Mode = AcquisitionRecordingMode;
using Snapshot = AcquisitionRecordingControlSnapshot;

StatusControlResult makeError (
    int httpStatus,
    StringRef code,
    const String& message,
    std::optional<Mode> requested = std::nullopt,
    std::optional<Snapshot> achieved = std::nullopt)
{
    StatusControlResult result;
    result.httpStatus = httpStatus;
    result.requestedMode = requested;
    result.achieved = std::move (achieved);
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}

String defaultTransportMessage (
    MessageThreadCallStatus status)
{
    switch (status)
    {
        case MessageThreadCallStatus::dispatchFailed:
            return "The status operation could not be queued on the message thread.";
        case MessageThreadCallStatus::timedOut:
            return "The status operation did not begin before the queue deadline.";
        case MessageThreadCallStatus::failed:
            return "The status operation failed on the message thread.";
        case MessageThreadCallStatus::completed:
            break;
    }

    return "The status operation did not return a result.";
}

template <typename Value>
std::optional<StatusControlResult> mapCallFailure (
    const MessageThreadCallResult<Value>& call,
    std::optional<Mode> requested = std::nullopt)
{
    if (call.status == MessageThreadCallStatus::completed
        && call.value.has_value())
        return std::nullopt;

    auto message = call.error;
    if (message.isEmpty())
        message = defaultTransportMessage (call.status);

    switch (call.status)
    {
        case MessageThreadCallStatus::dispatchFailed:
            return makeError (
                503,
                "operation_unavailable",
                message,
                requested);
        case MessageThreadCallStatus::timedOut:
            return makeError (
                504,
                "operation_timeout",
                message,
                requested);
        case MessageThreadCallStatus::failed:
            return makeError (
                500,
                "operation_failed",
                message,
                requested);
        case MessageThreadCallStatus::completed:
            return makeError (
                500,
                "operation_failed",
                message,
                requested);
    }

    return makeError (
        500,
        "operation_failed",
        message,
        requested);
}

int controllerErrorHttpStatus (Error error)
{
    switch (error)
    {
        case Error::recordNodeRequired:
        case Error::invalidRecordingPath:
        case Error::unsynchronizedConfirmationRequired:
        case Error::audioDeviceUnavailable:
        case Error::audioSampleRateTooLow:
        case Error::processorGraphNotReady:
        case Error::stateTransitionRejected:
            return 409;
        case Error::recordingDirectoryCreateFailed:
        case Error::recordingStartFailed:
        case Error::rollbackFailed:
        case Error::inconsistentState:
        case Error::operationFailed:
            return 500;
    }

    return 500;
}

String controllerErrorMessage (Error error)
{
    return "The status transition failed: "
           + String (acquisitionRecordingControlErrorCode (error))
           + ".";
}

bool isCanonicalSnapshot (const Snapshot& snapshot)
{
    return snapshot.status.mode.has_value()
           && snapshot.status.recordingConsistent
           && isValidAcquisitionRecordingControlSnapshot (
               snapshot);
}
} // namespace

StatusControlResult handleStatusGet (
    StatusControlDispatcher dispatcher,
    StatusControlReadback readback,
    std::chrono::milliseconds queueStartTimeout)
{
    auto call = runDispatchedCall (
        std::move (readback),
        std::move (dispatcher),
        queueStartTimeout);

    if (const auto failure = mapCallFailure (call))
        return *failure;

    auto achieved = std::move (*call.value);
    if (! isCanonicalSnapshot (achieved))
    {
        return makeError (
            500,
            "inconsistent_state",
            "Open Ephys reported an inconsistent acquisition or recording state.",
            std::nullopt,
            std::move (achieved));
    }

    StatusControlResult result;
    result.httpStatus = 200;
    result.achieved = std::move (achieved);
    return result;
}

StatusControlResult handleStatusPut (
    StringRef requestBody,
    StatusControlDispatcher dispatcher,
    StatusControlApply apply,
    std::chrono::milliseconds queueStartTimeout)
{
    const auto parsed = parseStatusRequest (requestBody);
    if (! parsed.request.has_value())
    {
        return makeError (
            400,
            parsed.errorCode,
            parsed.error);
    }

    const auto request = *parsed.request;
    auto call = runDispatchedCall (
        [request, apply = std::move (apply)]
        {
            return apply (request);
        },
        std::move (dispatcher),
        queueStartTimeout);

    if (const auto failure = mapCallFailure (
            call,
            request.mode))
        return *failure;

    auto control = std::move (*call.value);
    if (control.requestedMode != request.mode)
    {
        return makeError (
            500,
            "operation_failed",
            "The status controller returned a result for a different request.",
            request.mode,
            std::move (control.achieved));
    }

    if (control.error.has_value())
    {
        const auto error = *control.error;
        return makeError (
            controllerErrorHttpStatus (error),
            acquisitionRecordingControlErrorCode (error),
            controllerErrorMessage (error),
            request.mode,
            std::move (control.achieved));
    }

    if (! control.achieved.has_value())
    {
        return makeError (
            500,
            "operation_failed",
            "The status controller did not return achieved state.",
            request.mode);
    }

    if (! isCanonicalSnapshot (*control.achieved))
    {
        return makeError (
            500,
            "inconsistent_state",
            "The status controller returned inconsistent achieved state.",
            request.mode,
            std::move (control.achieved));
    }

    if (control.achieved->status.mode
        != std::optional<Mode> (request.mode))
    {
        return makeError (
            500,
            "operation_failed",
            "The achieved Open Ephys mode does not match the requested mode.",
            request.mode,
            std::move (control.achieved));
    }

    if (control.unsynchronizedConfirmed
        && (! request.confirmUnsynchronized
            || request.mode != Mode::record))
    {
        return makeError (
            500,
            "operation_failed",
            "The controller reported an unsynchronized confirmation that the request did not grant.",
            request.mode,
            std::move (control.achieved));
    }

    StatusControlResult result;
    result.httpStatus = 200;
    result.requestedMode = request.mode;
    result.achieved = std::move (control.achieved);
    result.changed = control.changed;
    result.unsynchronizedConfirmed =
        control.unsynchronizedConfirmed;
    return result;
}
