#include "../../Source/Processors/Parameter/ParameterEditor.h"
#include "gtest/gtest.h"

namespace
{
class ParameterEditorAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        messageManagerLock = std::make_unique<MessageManagerLock>();
    }

    void TearDown() override
    {
        messageManagerLock.reset();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    static void expectSemanticValueControl (ParameterEditor& editor,
                                            const String& expectedId,
                                            const String& expectedTitle,
                                            const String& expectedDescription)
    {
        auto* valueControl = editor.getEditor();
        ASSERT_NE (valueControl, nullptr);
        EXPECT_EQ (valueControl->getComponentID(), expectedId);
        EXPECT_EQ (valueControl->getTitle(), expectedTitle);
        EXPECT_EQ (valueControl->getDescription(), expectedDescription);
        EXPECT_TRUE (valueControl->isAccessible());
    }

    std::unique_ptr<MessageManagerLock> messageManagerLock;
};
} // namespace

TEST_F (ParameterEditorAccessibilityTests, AppliesSemanticMetadataToEverySharedValueControl)
{
    {
        BooleanParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "enabled", "Enabled", "Enables processing.", false);
        parameter.setKey ("101|enabled");
        ToggleParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_enabled", "Enabled", "Enables processing.");
    }

    {
        StringParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "label", "Label", "Sets the label.", "default");
        parameter.setKey ("101|label");
        TextBoxParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_label", "Label", "Sets the label.");
    }

    {
        Array<String> categories { "First", "Second" };
        CategoricalParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "mode", "Mode", "Selects the mode.", categories, 0);
        parameter.setKey ("101|mode");
        ComboBoxParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_mode", "Mode", "Selects the mode.");
    }

    {
        IntParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "count", "Count", "Sets the count.", 4, 0, 8);
        parameter.setKey ("101|count");
        BoundedValueParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_count", "Count", "Sets the count.");
    }

    {
        Array<var> selectedChannels;
        SelectedChannelsParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "channels", "Channels", "Selects channels.", selectedChannels);
        parameter.setKey ("101|channels");
        parameter.setChannelCount (4);
        SelectedChannelsParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_channels", "Channels", "Selects channels.");
    }

    {
        MaskChannelsParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "mask", "Mask", "Masks channels.");
        parameter.setKey ("101|mask");
        parameter.setChannelCount (4);
        MaskChannelsParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_mask", "Mask", "Masks channels.");
    }

    {
        Array<String> streams { "Stream A", "Stream B" };
        SelectedStreamParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "stream", "Stream", "Selects a stream.", streams, 0);
        parameter.setKey ("101|stream");
        SelectedStreamParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_stream", "Stream", "Selects a stream.");
    }

    {
        TtlLineParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "ttl_line", "TTL line", "Selects a TTL line.");
        parameter.setKey ("101|ttl_line");
        TtlLineParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_ttl_line", "TTL line", "Selects a TTL line.");
    }

    {
        PathParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "directory", "Directory", "Selects a directory.", File(), {}, true, false);
        parameter.setKey ("101|directory");
        PathParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_directory", "Directory", "Selects a directory.");
    }

    {
        TimeParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "start_time", "Start time", "Sets the start time.", "00:00:00.000");
        parameter.setKey ("101|start_time");
        TimeParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_start_time", "Start time", "Sets the start time.");
    }
}

TEST_F (ParameterEditorAccessibilityTests, RefreshesSemanticMetadataWhenAControlIsReused)
{
    StringParameter placeholder (nullptr, Parameter::GLOBAL_SCOPE, "unknown", "Unknown", "Placeholder parameter.", "");
    StringParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "channel", "Channel", "Selects the monitored channel.", "1");
    parameter.setKey ("106|channel");

    TextBoxParameterEditor editor (&placeholder);
    editor.setParameter (&parameter);

    expectSemanticValueControl (editor,
                                "oe.parameter.106_channel",
                                "Channel",
                                "Selects the monitored channel.");
}
