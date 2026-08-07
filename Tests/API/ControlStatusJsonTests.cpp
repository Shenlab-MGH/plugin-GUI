#include "../../Source/Utils/ControlStatusJson.h"
#include "../../Source/Utils/json.hpp"
#include "../../Source/Utils/StatusControl.h"
#include "gtest/gtest.h"

#include <functional>
#include <initializer_list>
#include <set>
#include <string>

namespace
{
using Error = AcquisitionRecordingControlError;
using Mode = AcquisitionRecordingMode;

AcquisitionRecordingControlSnapshot statusJsonSnapshot (
    Mode mode)
{
    const auto callbacksActive =
        mode != Mode::idle;
    const auto recordingActive =
        mode == Mode::record;

    AcquisitionRecordingControlSnapshot result;
    result.status = deriveAcquisitionRecordingStatus (
        callbacksActive,
        { { recordingActive, recordingActive } });
    result.recordNodes = {
        { 91,
          recordingActive,
          recordingActive,
          true,
          true }
    };
    result.audioDeviceAvailable = true;
    result.audioSampleRate = 44100.0;
    result.processorGraphReady = true;
    return result;
}

void expectExactKeys (
    const nlohmann::json& document,
    std::initializer_list<const char*> expected)
{
    std::set<std::string> actualKeys;
    for (const auto& item : document.items())
        actualKeys.insert (item.key());

    std::set<std::string> expectedKeys;
    for (const auto* key : expected)
        expectedKeys.insert (key);

    EXPECT_EQ (actualKeys, expectedKeys);
}

void expectExactError (
    const nlohmann::json& error,
    const char* expectedCode)
{
    expectExactKeys (error, { "code", "message" });
    EXPECT_EQ (error["code"], expectedCode);
    EXPECT_TRUE (error["message"].is_string());
    EXPECT_FALSE (
        error["message"].get<std::string>().empty());
}

auto immediateStatusDispatcher()
{
    return [] (std::function<void()> operation)
    {
        operation();
        return true;
    };
}
} // namespace

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

TEST (ControlStatusJsonTests,
      SerializesTheExactConsistentGetSchema)
{
    const auto result = handleStatusGet (
        immediateStatusDispatcher(),
        [] { return statusJsonSnapshot (Mode::idle); },
        std::chrono::milliseconds (50));
    const auto document =
        statusGetResultToJson (result);

    expectExactKeys (
        document,
        {
            "capabilities",
            "mode",
            "acquisition_active",
            "recording_active",
            "record_node_count",
            "active_record_node_count",
            "writer_thread_running_count",
            "recording_consistent",
            "read_only"
        });
    EXPECT_EQ (
        document["capabilities"],
        nlohmann::json::array ({
            "oe.control.acquisition",
            "oe.control.recording"
        }));
    EXPECT_EQ (document["mode"], "IDLE");
    EXPECT_EQ (document["acquisition_active"], false);
    EXPECT_EQ (document["recording_active"], false);
    EXPECT_EQ (document["record_node_count"], 1);
    EXPECT_EQ (document["active_record_node_count"], 0);
    EXPECT_EQ (
        document["writer_thread_running_count"],
        0);
    EXPECT_EQ (document["recording_consistent"], true);
    EXPECT_EQ (document["read_only"], true);
}

TEST (ControlStatusJsonTests,
      SerializesTheExactInconsistentGetSchemaWithRawFacts)
{
    auto raw = statusJsonSnapshot (Mode::idle);
    raw.status = deriveAcquisitionRecordingStatus (
        false,
        { { true, true } });
    raw.recordNodes[0].recordingActive = true;
    raw.recordNodes[0].writerThreadRunning = true;

    const auto result = handleStatusGet (
        immediateStatusDispatcher(),
        [raw] { return raw; },
        std::chrono::milliseconds (50));
    const auto document =
        statusGetResultToJson (result);

    expectExactKeys (
        document,
        {
            "ok",
            "capabilities",
            "mode",
            "acquisition_active",
            "recording_active",
            "record_node_count",
            "active_record_node_count",
            "writer_thread_running_count",
            "recording_consistent",
            "error"
        });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (
        document["capabilities"],
        nlohmann::json::array ({
            "oe.control.acquisition",
            "oe.control.recording"
        }));
    EXPECT_TRUE (document["mode"].is_null());
    EXPECT_EQ (document["acquisition_active"], false);
    EXPECT_EQ (document["recording_active"], false);
    EXPECT_EQ (document["record_node_count"], 1);
    EXPECT_EQ (document["active_record_node_count"], 1);
    EXPECT_EQ (
        document["writer_thread_running_count"],
        1);
    EXPECT_EQ (document["recording_consistent"], false);
    expectExactError (
        document["error"],
        "inconsistent_state");
}

TEST (ControlStatusJsonTests,
      TransportGetErrorDoesNotFabricateStateFields)
{
    const auto result = handleStatusGet (
        [] (std::function<void()>) { return false; },
        [] { return statusJsonSnapshot (Mode::idle); },
        std::chrono::milliseconds (50));
    const auto document =
        statusGetResultToJson (result);

    expectExactKeys (
        document,
        { "ok", "capabilities", "error" });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (
        document["capabilities"],
        nlohmann::json::array ({
            "oe.control.acquisition",
            "oe.control.recording"
        }));
    expectExactError (
        document["error"],
        "operation_unavailable");
}

TEST (ControlStatusJsonTests,
      PutSuccessUsesExactSchemaAndAffectedCapability)
{
    struct Case
    {
        const char* request;
        Mode mode;
        const char* modeName;
        const char* capability;
    };

    for (const auto& testCase : {
             Case {
                 R"({"mode":"IDLE"})",
                 Mode::idle,
                 "IDLE",
                 "oe.control.acquisition"
             },
             Case {
                 R"({"mode":"ACQUIRE"})",
                 Mode::acquire,
                 "ACQUIRE",
                 "oe.control.acquisition"
             },
             Case {
                 R"({"mode":"RECORD"})",
                 Mode::record,
                 "RECORD",
                 "oe.control.recording"
             } })
    {
        const auto result = handleStatusPut (
            testCase.request,
            immediateStatusDispatcher(),
            [&] (const StatusRequest& request)
            {
                return AcquisitionRecordingControlResult {
                    request.mode,
                    statusJsonSnapshot (request.mode),
                    true,
                    false,
                    std::nullopt
                };
            },
            std::chrono::milliseconds (50));
        const auto document =
            statusPutResultToJson (result);

        expectExactKeys (
            document,
            {
                "ok",
                "capability",
                "requested_mode",
                "mode",
                "changed",
                "acquisition_active",
                "recording_active",
                "record_node_count",
                "active_record_node_count",
                "writer_thread_running_count",
                "recording_consistent",
                "unsynchronized_confirmed"
            });
        EXPECT_EQ (document["ok"], true);
        EXPECT_EQ (
            document["capability"],
            testCase.capability);
        EXPECT_EQ (
            document["requested_mode"],
            testCase.modeName);
        EXPECT_EQ (document["mode"],
                   testCase.modeName);
        EXPECT_EQ (document["changed"], true);
        EXPECT_EQ (
            document["acquisition_active"],
            testCase.mode != Mode::idle);
        EXPECT_EQ (
            document["recording_active"],
            testCase.mode == Mode::record);
        EXPECT_EQ (document["record_node_count"], 1);
        EXPECT_EQ (
            document["active_record_node_count"],
            testCase.mode == Mode::record ? 1 : 0);
        EXPECT_EQ (
            document["writer_thread_running_count"],
            testCase.mode == Mode::record ? 1 : 0);
        EXPECT_EQ (
            document["recording_consistent"],
            true);
        EXPECT_EQ (
            document["unsynchronized_confirmed"],
            false);
    }
}

TEST (ControlStatusJsonTests,
      PutErrorWithAchievedStateUsesExactSchema)
{
    const auto result = handleStatusPut (
        R"({"mode":"RECORD"})",
        immediateStatusDispatcher(),
        [] (const StatusRequest& request)
        {
            return AcquisitionRecordingControlResult {
                request.mode,
                statusJsonSnapshot (Mode::acquire),
                false,
                false,
                Error::stateTransitionRejected
            };
        },
        std::chrono::milliseconds (50));
    const auto document =
        statusPutResultToJson (result);

    expectExactKeys (
        document,
        {
            "ok",
            "capability",
            "requested_mode",
            "mode",
            "changed",
            "acquisition_active",
            "recording_active",
            "record_node_count",
            "active_record_node_count",
            "writer_thread_running_count",
            "recording_consistent",
            "error"
        });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (
        document["capability"],
        "oe.control.recording");
    EXPECT_EQ (document["requested_mode"], "RECORD");
    EXPECT_EQ (document["mode"], "ACQUIRE");
    EXPECT_EQ (document["changed"], false);
    EXPECT_EQ (document["acquisition_active"], true);
    EXPECT_EQ (document["recording_active"], false);
    EXPECT_EQ (document["record_node_count"], 1);
    EXPECT_EQ (
        document["active_record_node_count"],
        0);
    EXPECT_EQ (
        document["writer_thread_running_count"],
        0);
    EXPECT_EQ (
        document["recording_consistent"],
        true);
    expectExactError (
        document["error"],
        "state_transition_rejected");
}

TEST (ControlStatusJsonTests,
      PutErrorWithoutAchievedStateOmitsEveryStateField)
{
    const auto result = handleStatusPut (
        R"({"mode":"ACQUIRE"})",
        [] (std::function<void()>) { return false; },
        [] (const StatusRequest&)
        {
            return AcquisitionRecordingControlResult {};
        },
        std::chrono::milliseconds (50));
    const auto document =
        statusPutResultToJson (result);

    expectExactKeys (
        document,
        {
            "ok",
            "capability",
            "requested_mode",
            "changed",
            "error"
        });
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (
        document["capability"],
        "oe.control.acquisition");
    EXPECT_EQ (document["requested_mode"], "ACQUIRE");
    EXPECT_EQ (document["changed"], false);
    expectExactError (
        document["error"],
        "operation_unavailable");
}

TEST (ControlStatusJsonTests,
      InvalidPutParseHasOnlyTheStructuredErrorEnvelope)
{
    const auto result = handleStatusPut (
        R"({"mode":"idle"})",
        immediateStatusDispatcher(),
        [] (const StatusRequest&)
        {
            return AcquisitionRecordingControlResult {};
        },
        std::chrono::milliseconds (50));
    const auto document =
        statusPutResultToJson (result);

    expectExactKeys (
        document,
        { "ok", "error" });
    EXPECT_EQ (document["ok"], false);
    expectExactError (
        document["error"],
        "invalid_request");
}
