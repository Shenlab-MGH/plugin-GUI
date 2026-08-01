#include "../../Source/Utils/SettingsDispatch.h"
#include "gtest/gtest.h"

#include <stdexcept>
#include <string>
#include <vector>

TEST (SettingsDispatchTests, AppliesAudioFieldsInLegacyOrderAndCapturesSnapshotLast)
{
    std::vector<std::string> events;
    const auto snapshot = applyAudioSettingsUpdate (
        { "ASIO", "Device", 30000, 512 },
        {
            [&] (const String& value) { events.push_back ("type:" + value.toStdString()); },
            [&] (const String& value) { events.push_back ("name:" + value.toStdString()); },
            [&] (int value) { events.push_back ("rate:" + std::to_string (value)); },
            [&] (int value) { events.push_back ("buffer:" + std::to_string (value)); },
            [&] { events.push_back ("graph-buffer"); },
            [&] { events.push_back ("snapshot"); return json ({ { "buffer_size", 512 } }); }
        });

    EXPECT_EQ (events, (std::vector<std::string> { "type:ASIO", "name:Device", "rate:30000", "buffer:512", "graph-buffer", "snapshot" }));
    EXPECT_EQ (snapshot, json ({ { "buffer_size", 512 } }));
}

TEST (SettingsDispatchTests, AppliesSparseRecordingFieldsAndOnlyExactTrueRequestsNewDirectory)
{
    std::vector<std::string> events;
    const auto snapshot = applyRecordingSettingsUpdate (
        { std::nullopt, "pre", std::nullopt, std::nullopt, "Binary", "true" },
        {
            [&] (const String&) { events.push_back ("parent"); },
            [&] (const String& value) { events.push_back ("prepend:" + value.toStdString()); },
            [&] (const String&) { events.push_back ("base"); },
            [&] (const String&) { events.push_back ("append"); },
            [&] (const String& value) { events.push_back ("engine:" + value.toStdString()); return false; },
            [&] { events.push_back ("new-directory"); },
            [&] { events.push_back ("snapshot"); return json ({ { "prepend_text", "pre" } }); }
        });

    EXPECT_EQ (events, (std::vector<std::string> { "prepend:pre", "engine:Binary", "new-directory", "snapshot" }));
    EXPECT_EQ (snapshot, json ({ { "prepend_text", "pre" } }));
}

TEST (SettingsDispatchTests, ForwardsTheParsedRecordNodeIdAndCapturesGlobalSnapshotLast)
{
    std::vector<std::string> events;
    const auto snapshot = applyRecordNodeSettingsUpdate (
        { 73, "D:/recordings", "Binary" },
        {
            [&] (const String& value, int id) { events.push_back ("directory:" + value.toStdString() + ":" + std::to_string (id)); },
            [&] (const String& value, int id) { events.push_back ("engine:" + value.toStdString() + ":" + std::to_string (id)); },
            [&] { events.push_back ("snapshot"); return json ({ { "parent_directory", "D:/recordings" } }); }
        });

    EXPECT_EQ (events, (std::vector<std::string> { "directory:D:/recordings:73", "engine:Binary:73", "snapshot" }));
    EXPECT_EQ (snapshot, json ({ { "parent_directory", "D:/recordings" } }));
}

TEST (SettingsDispatchTests, PropagatesSetterExceptionsWithoutApplyingLaterFieldsOrRollback)
{
    std::vector<std::string> events;
    EXPECT_THROW (
        applyAudioSettingsUpdate (
            { "ASIO", "Device", std::nullopt, std::nullopt },
            {
                [&] (const String&) { events.push_back ("type"); throw std::runtime_error ("type failure"); },
                [&] (const String&) { events.push_back ("name"); },
                [&] (int) { events.push_back ("rate"); },
                [&] (int) { events.push_back ("buffer"); },
                [&] { events.push_back ("graph-buffer"); },
                [&] { events.push_back ("snapshot"); return json::object(); }
            }),
        std::runtime_error);
    EXPECT_EQ (events, (std::vector<std::string> { "type" }));
}

TEST (SettingsDispatchTests, IgnoresMissingUnknownAndWrongTypeFieldsIndependently)
{
    const json document = { { "device_type", "ASIO" }, { "sample_rate", "wrong" }, { "unknown", 12 } };
    EXPECT_EQ (*optionalSettingsField<std::string> (document, "device_type"), "ASIO");
    EXPECT_FALSE (optionalSettingsField<int> (document, "sample_rate").has_value());
    EXPECT_FALSE (optionalSettingsField<int> (document, "missing").has_value());
}

TEST (SettingsDispatchTests, DoesNotRequestNewDirectoryForNonExactTrue)
{
    int requests = 0;
    applyRecordingSettingsUpdate ({ {}, {}, {}, {}, {}, "True" },
                                  { [] (const String&) {}, [] (const String&) {}, [] (const String&) {}, [] (const String&) {}, [] (const String&) { return false; }, [&] { ++requests; }, [] { return json::object(); } });
    EXPECT_EQ (requests, 0);
}
