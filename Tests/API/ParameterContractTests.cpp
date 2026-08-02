#include "../../Source/Processors/Parameter/ParameterEditor.h"
#include "../../Source/Processors/Settings/DataStream.h"

#define private public
#include "../../Source/Utils/OpenEphysHttpServer.h"
#undef private

#include "gtest/gtest.h"

#include <string>

namespace
{
using json = nlohmann::json;

bool expectParameterContractFields (const json& document)
{
    const auto hasRequiredFields =
        document.contains ("name")
        && document.contains ("type")
        && document.contains ("value")
        && document.contains ("key")
        && document.contains ("display_name")
        && document.contains ("description")
        && document.contains ("enabled")
        && document.contains ("deactivate_during_acquisition")
        && document.contains ("uia");
    EXPECT_TRUE (hasRequiredFields);
    if (! hasRequiredFields)
        return false;

    const auto hasExpectedTypes =
        document["name"].is_string()
        && document["type"].is_string()
        && document["value"].is_string()
        && document["key"].is_string()
        && document["display_name"].is_string()
        && document["description"].is_string()
        && document["enabled"].is_boolean()
        && document["deactivate_during_acquisition"].is_boolean()
        && document["uia"].is_object()
        && document["uia"].contains ("automation_id")
        && document["uia"]["automation_id"].is_string();
    EXPECT_TRUE (hasExpectedTypes);
    return hasExpectedTypes;
}
} // namespace

TEST (ParameterContractTests,
      SerialisesProcessorParameterWithLegacyFieldsAndEditorAutomationId)
{
    BooleanParameter parameter (
        nullptr,
        Parameter::PROCESSOR_SCOPE,
        "gain",
        "Gain",
        "Controls the processor gain.",
        true,
        true);
    parameter.setKey ("101|Gain: Fast/Slow");
    parameter.setEnabled (false);

    ToggleParameterEditor editor (&parameter);
    json document;
    OpenEphysHttpServer::parameter_to_json (&parameter, &document);

    if (! expectParameterContractFields (document))
        return;
    EXPECT_EQ (document["name"], "gain");
    EXPECT_EQ (document["type"], "Boolean");
    EXPECT_EQ (document["value"], "1");
    EXPECT_EQ (document["key"], "101|Gain: Fast/Slow");
    EXPECT_EQ (document["display_name"], "Gain");
    EXPECT_EQ (document["description"], "Controls the processor gain.");
    EXPECT_EQ (document["enabled"], false);
    EXPECT_EQ (document["deactivate_during_acquisition"], true);
    EXPECT_EQ (
        document["uia"]["automation_id"],
        editor.getEditor()->getComponentID().toStdString());
    EXPECT_EQ (
        document["uia"]["automation_id"],
        "oe.parameter.101_gain_fast_slow");
}

TEST (ParameterContractTests,
      SerialisesStreamParameterWithLegacyFieldsAndEditorAutomationId)
{
    DataStream::Settings settings {
        "Probe Stream",
        "Test stream",
        "probe.stream",
        30000.0f,
        false
    };
    DataStream stream (settings);
    StringParameter parameter (
        &stream,
        Parameter::STREAM_SCOPE,
        "threshold",
        "Threshold",
        "Sets the stream threshold.",
        "50",
        true);
    parameter.setKey ("99|Probe Stream|99|threshold: chan 1");

    TextBoxParameterEditor editor (&parameter);
    json document;
    OpenEphysHttpServer::parameter_to_json (&parameter, &document);

    if (! expectParameterContractFields (document))
        return;
    EXPECT_EQ (document["name"], "threshold");
    EXPECT_EQ (document["type"], "String");
    EXPECT_EQ (document["value"], "50");
    EXPECT_EQ (document["key"], "99|Probe Stream|99|threshold: chan 1");
    EXPECT_EQ (document["display_name"], "Threshold");
    EXPECT_EQ (document["description"], "Sets the stream threshold.");
    EXPECT_EQ (document["enabled"], true);
    EXPECT_EQ (document["deactivate_during_acquisition"], true);
    EXPECT_EQ (
        document["uia"]["automation_id"],
        editor.getEditor()->getComponentID().toStdString());
    EXPECT_EQ (
        document["uia"]["automation_id"],
        "oe.parameter.99_probe_stream_99_threshold_chan_1");
}
