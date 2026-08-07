#include "../../Source/Utils/StatusHttpAdapter.h"
#include "../../Source/Utils/httplib.h"
#include "../../Source/Utils/json.hpp"

#include "gtest/gtest.h"

#include <chrono>
#include <functional>

using namespace std::chrono_literals;

namespace
{
using Mode = AcquisitionRecordingMode;

AcquisitionRecordingControlSnapshot adapterSnapshot (Mode mode)
{
    const auto acquiring = mode != Mode::idle;
    const auto recording = mode == Mode::record;
    AcquisitionRecordingControlSnapshot result;
    result.status = deriveAcquisitionRecordingStatus (
        acquiring,
        { { recording, recording } });
    result.recordNodes = {
        { 17, recording, recording, true, true }
    };
    return result;
}
} // namespace

TEST (StatusHttpAdapterTests,
      GetFillsARealHttplibResponseInsideOneDispatch)
{
    int dispatchCount = 0;
    int readCount = 0;
    bool insideDispatch = false;
    httplib::Request request;
    request.method = "GET";
    request.path = "/api/status";
    httplib::Response response;

    handleStatusHttpGet (
        request,
        response,
        [&] (std::function<void()> operation)
        {
            ++dispatchCount;
            insideDispatch = true;
            operation();
            insideDispatch = false;
            return true;
        },
        [&]
        {
            EXPECT_TRUE (insideDispatch);
            ++readCount;
            return adapterSnapshot (Mode::acquire);
        },
        50ms);

    EXPECT_EQ (response.status, 200);
    EXPECT_EQ (
        response.get_header_value ("Content-Type"),
        "application/json");
    EXPECT_EQ (dispatchCount, 1);
    EXPECT_EQ (readCount, 1);
    const auto body =
        nlohmann::json::parse (response.body);
    EXPECT_EQ (body["mode"], "ACQUIRE");
    EXPECT_EQ (body["acquisition_active"], true);
    EXPECT_EQ (body["recording_active"], false);
}

TEST (StatusHttpAdapterTests,
      PutFillsARealHttplibResponseInsideOneDispatch)
{
    int dispatchCount = 0;
    int controllerCount = 0;
    bool insideDispatch = false;
    httplib::Request request;
    request.method = "PUT";
    request.path = "/api/status";
    request.body =
        R"({"mode":"RECORD","confirm_unsynchronized":false})";
    httplib::Response response;

    handleStatusHttpPut (
        request,
        response,
        [&] (std::function<void()> operation)
        {
            ++dispatchCount;
            insideDispatch = true;
            operation();
            insideDispatch = false;
            return true;
        },
        [&] (const StatusRequest& statusRequest)
        {
            EXPECT_TRUE (insideDispatch);
            EXPECT_EQ (statusRequest.mode, Mode::record);
            EXPECT_FALSE (
                statusRequest.confirmUnsynchronized);
            ++controllerCount;
            return AcquisitionRecordingControlResult {
                statusRequest.mode,
                adapterSnapshot (Mode::record),
                true,
                false,
                std::nullopt
            };
        },
        50ms);

    EXPECT_EQ (response.status, 200);
    EXPECT_EQ (
        response.get_header_value ("Content-Type"),
        "application/json");
    EXPECT_EQ (dispatchCount, 1);
    EXPECT_EQ (controllerCount, 1);
    const auto body =
        nlohmann::json::parse (response.body);
    EXPECT_EQ (body["requested_mode"], "RECORD");
    EXPECT_EQ (body["mode"], "RECORD");
    EXPECT_EQ (body["unsynchronized_confirmed"], false);
}
