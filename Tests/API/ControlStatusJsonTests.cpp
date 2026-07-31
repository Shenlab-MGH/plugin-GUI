#include "../../Source/Utils/ControlStatusJson.h"
#include "../../Source/Utils/StatusControl.h"
#include "../../Source/Utils/json.hpp"
#include "gtest/gtest.h"

#include <limits>
#include <set>
#include <string>

namespace
{
using json = nlohmann::json;
using Mode = AcquisitionRecordingMode;
using Snapshot = AcquisitionRecordingControlSnapshot;

const json statusCapabilities = json::array ({
    "oe.control.acquisition",
    "oe.control.recording"
});

std::set<std::string> keysOf (const json& document)
{
    std::set<std::string> keys;
    for (const auto& item : document.items())
        keys.insert (item.key());
    return keys;
}

void expectExactKeys (
    const json& document,
    std::initializer_list<const char*> expected)
{
    std::set<std::string> expectedKeys;
    for (const auto* key : expected)
        expectedKeys.insert (key);

    EXPECT_EQ (keysOf (document), expectedKeys);
}

Snapshot fullSnapshot (Mode mode)
{
    const auto acquiring = mode != Mode::idle;
    const auto recording = mode == Mode::record;

    Snapshot snapshot;
    snapshot.status = deriveAcquisitionRecordingStatus (
        acquiring,
        { { recording, recording },
          { recording, recording } });
    snapshot.recordNodes = {
        { 17, recording, recording, true, false },
        { 29, recording, recording, true, true }
    };
    snapshot.audioDeviceAvailable = true;
    snapshot.audioSampleRate = 30000.0;
    snapshot.processorGraphReady = true;
    return snapshot;
}

StatusControlResult getSuccess (Snapshot achieved)
{
    StatusControlResult result;
    result.httpStatus = 200;
    result.achieved = std::move (achieved);
    return result;
}

StatusControlResult putSuccess (
    Mode requested,
    Snapshot achieved,
    bool changed = true,
    bool confirmationConsumed = false)
{
    StatusControlResult result;
    result.httpStatus = 200;
    result.requestedMode = requested;
    result.achieved = std::move (achieved);
    result.changed = changed;
    result.unsynchronizedConfirmed = confirmationConsumed;
    return result;
}
} // namespace

TEST (ControlStatusJsonTests,
      SerialisesGetSuccessWithExactCanonicalFieldsAndFullNodeFacts)
{
    const auto document = statusGetResultToJson (
        getSuccess (fullSnapshot (Mode::record)));

    expectExactKeys (
        document,
        { "ok", "capabilities", "mode",
          "acquisition_active", "recording_active",
          "record_node_count", "active_record_node_count",
          "writer_thread_running_count", "recording_consistent",
          "record_nodes", "audio_device_available",
          "audio_sample_rate", "processor_graph_ready" });
    EXPECT_EQ (document["ok"], true);
    EXPECT_EQ (document["capabilities"], statusCapabilities);
    EXPECT_FALSE (document.contains ("capability"));
    EXPECT_EQ (document["mode"], "RECORD");
    EXPECT_EQ (document["acquisition_active"], true);
    EXPECT_EQ (document["recording_active"], true);
    EXPECT_EQ (document["record_node_count"], 2);
    EXPECT_EQ (document["active_record_node_count"], 2);
    EXPECT_EQ (document["writer_thread_running_count"], 2);
    EXPECT_EQ (document["recording_consistent"], true);
    EXPECT_EQ (document["audio_device_available"], true);
    EXPECT_EQ (document["audio_sample_rate"], 30000.0);
    EXPECT_EQ (document["processor_graph_ready"], true);
    ASSERT_EQ (document["record_nodes"].size(), 2);
    EXPECT_EQ (
        document["record_nodes"][0],
        json ({ { "generation", 17 },
                { "recording_active", true },
                { "writer_thread_running", true },
                { "recording_path_valid", true },
                { "synchronized", false } }));
    EXPECT_EQ (
        document["record_nodes"][1],
        json ({ { "generation", 29 },
                { "recording_active", true },
                { "writer_thread_running", true },
                { "recording_path_valid", true },
                { "synchronized", true } }));
}

TEST (ControlStatusJsonTests,
      SerialisesInconsistentGetWithNullModeRawFactsAndNestedError)
{
    auto achieved = fullSnapshot (Mode::idle);
    achieved.status.mode.reset();
    achieved.status.recordingActive = false;
    achieved.status.activeRecordNodeCount = 1;
    achieved.status.writerThreadRunningCount = 1;
    achieved.status.recordingConsistent = false;
    achieved.recordNodes[0].recordingActive = true;
    achieved.recordNodes[0].writerThreadRunning = true;

    StatusControlResult result;
    result.httpStatus = 500;
    result.achieved = achieved;
    result.errorCode = "inconsistent_state";
    result.errorMessage = "Copied status is inconsistent.";

    const auto document = statusGetResultToJson (result);

    expectExactKeys (
        document,
        { "ok", "capabilities", "mode",
          "acquisition_active", "recording_active",
          "record_node_count", "active_record_node_count",
          "writer_thread_running_count", "recording_consistent",
          "record_nodes", "audio_device_available",
          "audio_sample_rate", "processor_graph_ready", "error" });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (document["capabilities"], statusCapabilities);
    EXPECT_TRUE (document["mode"].is_null());
    EXPECT_EQ (document["active_record_node_count"], 1);
    EXPECT_EQ (document["writer_thread_running_count"], 1);
    EXPECT_EQ (document["recording_consistent"], false);
    EXPECT_EQ (document["record_nodes"][0]["recording_active"], true);
    EXPECT_EQ (document["record_nodes"][0]["writer_thread_running"], true);
    EXPECT_EQ (
        document["error"],
        json ({ { "code", "inconsistent_state" },
                { "message", "Copied status is inconsistent." } }));
}

TEST (ControlStatusJsonTests,
      SerialisesGetTransportErrorWithoutFabricatingState)
{
    StatusControlResult result;
    result.httpStatus = 504;
    result.errorCode = "operation_timeout";
    result.errorMessage = "The operation did not start in time.";

    const auto document = statusGetResultToJson (result);

    expectExactKeys (document, { "ok", "capabilities", "error" });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (document["capabilities"], statusCapabilities);
    EXPECT_EQ (
        document["error"],
        json ({ { "code", "operation_timeout" },
                { "message", "The operation did not start in time." } }));
}

TEST (ControlStatusJsonTests,
      SerialisesPutSuccessForIdleAcquireAndRecordWithPluralCapabilities)
{
    struct Case
    {
        Mode mode;
        const char* name;
        bool confirmationConsumed;
    };

    for (const auto& testCase : {
             Case { Mode::idle, "IDLE", false },
             Case { Mode::acquire, "ACQUIRE", false },
             Case { Mode::record, "RECORD", true } })
    {
        const auto document = statusPutResultToJson (
            putSuccess (
                testCase.mode,
                fullSnapshot (testCase.mode),
                true,
                testCase.confirmationConsumed));

        expectExactKeys (
            document,
            { "ok", "capabilities", "mode",
              "acquisition_active", "recording_active",
              "record_node_count", "active_record_node_count",
              "writer_thread_running_count", "recording_consistent",
              "record_nodes", "audio_device_available",
              "audio_sample_rate", "processor_graph_ready",
              "requested_mode", "changed",
              "unsynchronized_confirmed" });
        EXPECT_EQ (document["ok"], true);
        EXPECT_EQ (document["capabilities"], statusCapabilities);
        EXPECT_FALSE (document.contains ("capability"));
        EXPECT_EQ (document["mode"], testCase.name);
        EXPECT_EQ (document["requested_mode"], testCase.name);
        EXPECT_EQ (document["changed"], true);
        EXPECT_EQ (
            document["acquisition_active"],
            testCase.mode != Mode::idle);
        EXPECT_EQ (
            document["recording_active"],
            testCase.mode == Mode::record);
        EXPECT_EQ (document["record_node_count"], 2);
        EXPECT_EQ (
            document["active_record_node_count"],
            testCase.mode == Mode::record ? 2 : 0);
        EXPECT_EQ (
            document["writer_thread_running_count"],
            testCase.mode == Mode::record ? 2 : 0);
        EXPECT_EQ (document["recording_consistent"], true);
        EXPECT_EQ (document["record_nodes"].size(), 2);
        EXPECT_EQ (
            document["record_nodes"][0]["recording_active"],
            testCase.mode == Mode::record);
        EXPECT_EQ (
            document["record_nodes"][0]["writer_thread_running"],
            testCase.mode == Mode::record);
        EXPECT_EQ (
            document["record_nodes"][0]["generation"],
            17);
        EXPECT_EQ (
            document["record_nodes"][0]["recording_path_valid"],
            true);
        EXPECT_EQ (
            document["record_nodes"][0]["synchronized"],
            false);
        EXPECT_EQ (document["audio_device_available"], true);
        EXPECT_EQ (document["audio_sample_rate"], 30000.0);
        EXPECT_EQ (document["processor_graph_ready"], true);
        EXPECT_EQ (
            document["unsynchronized_confirmed"],
            testCase.confirmationConsumed);
    }
}

TEST (ControlStatusJsonTests,
      SerialisesPutControllerErrorWithRequestedModeAndAchievedState)
{
    StatusControlResult result;
    result.httpStatus = 409;
    result.requestedMode = Mode::record;
    result.achieved = fullSnapshot (Mode::acquire);
    result.errorCode = "unsynchronized_confirmation_required";
    result.errorMessage = "Confirmation is required.";

    const auto document = statusPutResultToJson (result);

    expectExactKeys (
        document,
        { "ok", "capabilities", "mode",
          "acquisition_active", "recording_active",
          "record_node_count", "active_record_node_count",
          "writer_thread_running_count", "recording_consistent",
          "record_nodes", "audio_device_available",
          "audio_sample_rate", "processor_graph_ready",
          "requested_mode", "changed", "unsynchronized_confirmed",
          "error" });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (document["capabilities"], statusCapabilities);
    EXPECT_EQ (document["mode"], "ACQUIRE");
    EXPECT_EQ (document["requested_mode"], "RECORD");
    EXPECT_EQ (document["changed"], false);
    EXPECT_EQ (document["unsynchronized_confirmed"], false);
    EXPECT_EQ (
        document["error"],
        json ({ { "code", "unsynchronized_confirmation_required" },
                { "message", "Confirmation is required." } }));
}

TEST (ControlStatusJsonTests,
      SerialisesPutTransportAndParseErrorsWithoutFabricatingState)
{
    StatusControlResult transportError;
    transportError.httpStatus = 503;
    transportError.requestedMode = Mode::acquire;
    transportError.errorCode = "operation_unavailable";
    transportError.errorMessage = "The operation is unavailable.";

    auto document = statusPutResultToJson (transportError);
    expectExactKeys (
        document,
        { "ok", "capabilities", "requested_mode", "changed",
          "unsynchronized_confirmed", "error" });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (document["capabilities"], statusCapabilities);
    EXPECT_EQ (document["requested_mode"], "ACQUIRE");
    EXPECT_EQ (document["changed"], false);
    EXPECT_EQ (document["unsynchronized_confirmed"], false);
    EXPECT_EQ (
        document["error"],
        json ({ { "code", "operation_unavailable" },
                { "message", "The operation is unavailable." } }));
    EXPECT_FALSE (document.contains ("mode"));
    EXPECT_FALSE (document.contains ("record_nodes"));

    StatusControlResult parseError;
    parseError.httpStatus = 400;
    parseError.errorCode = "invalid_request";
    parseError.errorMessage = "Field 'mode' is required.";

    document = statusPutResultToJson (parseError);
    expectExactKeys (document, { "ok", "capabilities", "error" });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (document["capabilities"], statusCapabilities);
    EXPECT_EQ (
        document["error"],
        json ({ { "code", "invalid_request" },
                { "message", "Field 'mode' is required." } }));
    EXPECT_FALSE (document.contains ("requested_mode"));
    EXPECT_FALSE (document.contains ("changed"));
    EXPECT_FALSE (document.contains ("unsynchronized_confirmed"));
}

TEST (ControlStatusJsonTests,
      SerialisesNonFiniteAudioSampleRatesAsJsonNull)
{
    for (const auto sampleRate : {
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN() })
    {
        auto snapshot = fullSnapshot (Mode::idle);
        snapshot.audioSampleRate = sampleRate;

        const auto document = statusGetResultToJson (
            getSuccess (std::move (snapshot)));

        EXPECT_TRUE (document["audio_sample_rate"].is_null());
    }
}

TEST (ControlStatusJsonTests, ParsesSupportedRecordingOptionFields)
{
    const auto result = parseRecordingOptionsUpdate (
        R"({"expanded":true,"force_new_directory":false,"new_directory_requested":true})");

    ASSERT_TRUE (result.update.has_value());
    EXPECT_TRUE (result.error.isEmpty());
    ASSERT_TRUE (result.update->expanded.has_value());
    ASSERT_TRUE (result.update->forceNewDirectory.has_value());
    ASSERT_TRUE (result.update->newDirectoryRequested.has_value());
    EXPECT_TRUE (*result.update->expanded);
    EXPECT_FALSE (*result.update->forceNewDirectory);
    EXPECT_TRUE (*result.update->newDirectoryRequested);
}

TEST (ControlStatusJsonTests, RejectsMalformedOrNonObjectRequests)
{
    auto result = parseRecordingOptionsUpdate ("{");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.isNotEmpty());

    result = parseRecordingOptionsUpdate ("[]");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("object"));
}

TEST (ControlStatusJsonTests, RejectsUnknownEmptyAndNonBooleanFields)
{
    auto result = parseRecordingOptionsUpdate (R"({"expanded":"yes"})");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("boolean"));

    result = parseRecordingOptionsUpdate (R"({"expaned":true})");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("Unknown"));

    result = parseRecordingOptionsUpdate ("{}");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("at least one"));
}

TEST (ControlStatusJsonTests, RejectsContradictoryForcedDirectoryState)
{
    const auto result = parseRecordingOptionsUpdate (
        R"({"force_new_directory":true,"new_directory_requested":false})");

    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("new_directory_requested"));
}
