#include "../../Source/Processors/Parameter/ParameterEditor.h"
#include "../../Source/Processors/Parameter/ParameterOwner.h"
#include "gtest/gtest.h"

namespace
{
class TestParameterOwner final : public ParameterOwner
{
public:
    TestParameterOwner() : ParameterOwner (Type::OTHER) {}

    void parameterChangeRequest (
        Parameter* parameter) override
    {
        parameter->updateValue();
    }
};

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
        auto wrapperHandler =
            editor.createAccessibilityHandler();
        ASSERT_NE (wrapperHandler, nullptr);
        EXPECT_EQ (
            wrapperHandler->getRole(),
            AccessibilityRole::ignored);

        auto* valueControl = editor.getEditor();
        ASSERT_NE (valueControl, nullptr);
        EXPECT_EQ (valueControl->getComponentID(), expectedId);
        EXPECT_EQ (valueControl->getTitle(), expectedTitle);
        EXPECT_EQ (valueControl->getDescription(), expectedDescription);
        EXPECT_TRUE (valueControl->isAccessible());

        auto* visualLabel = editor.getLabel();
        ASSERT_NE (visualLabel, nullptr);
        EXPECT_FALSE (visualLabel->isAccessible());
    }

    static void expectReadOnlyButtonValue (
        ParameterEditor& editor,
        const String& expectedValue)
    {
        auto* valueControl = editor.getEditor();
        ASSERT_NE (valueControl, nullptr);

        auto handler =
            valueControl->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::button);
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_TRUE (
            handler->getValueInterface()
                ->isReadOnly());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            expectedValue);
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

TEST_F (ParameterEditorAccessibilityTests,
        ExposesCurrentValuesForButtonBasedEditors)
{
    TestParameterOwner owner;

    {
        Array<var> selected { 0, 2 };
        SelectedChannelsParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "channels",
            "Channels",
            "Selects channels.",
            selected);
        parameter.setKey ("101|channels");
        parameter.setChannelCount (4);
        SelectedChannelsParameterEditor editor (
            &parameter);
        expectReadOnlyButtonValue (editor, "1, 3");
        auto liveHandler =
            editor.getEditor()
                ->createAccessibilityHandler();
        ASSERT_NE (liveHandler, nullptr);
        ASSERT_NE (
            liveHandler->getValueInterface(),
            nullptr);

        Array<var> updated { 1 };
        parameter.setNextValue (updated, false);
        EXPECT_EQ (
            liveHandler->getValueInterface()
                ->getCurrentValueAsString(),
            "2");
    }

    {
        MaskChannelsParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "mask",
            "Mask",
            "Masks channels.");
        parameter.setKey ("101|mask");
        parameter.setChannelCount (4);
        MaskChannelsParameterEditor editor (&parameter);
        expectReadOnlyButtonValue (editor, "4/4");
    }

    {
        TtlLineParameter parameter (
            &owner,
            Parameter::STREAM_SCOPE,
            "ttl_line",
            "TTL line",
            "Selects a TTL line.");
        parameter.setKey ("101|ttl_line");
        TtlLineParameterEditor editor (&parameter);
        expectReadOnlyButtonValue (editor, "Line 1");
    }

    {
        const auto directory =
            File::getSpecialLocation (
                File::tempDirectory);
        PathParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "directory",
            "Directory",
            "Selects a directory.",
            directory,
            StringArray {},
            true,
            true);
        parameter.setKey ("101|directory");
        PathParameterEditor editor (&parameter);
        editor.updateView();
        expectReadOnlyButtonValue (
            editor,
            directory.getFullPathName());
    }

    {
        TimeParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "start_time",
            "Start time",
            "Sets the start time.",
            "00:00:01.000");
        parameter.setKey ("101|start_time");
        TimeParameterEditor editor (&parameter);
        expectReadOnlyButtonValue (
            editor,
            "00:00:01.000");
    }
}

TEST_F (ParameterEditorAccessibilityTests,
        ExposesSynchronizationStatusAndClockRole)
{
    SynchronizingProcessor processor;
    processor.synchronizer.addDataStream (
        "main",
        30000.0f);
    processor.synchronizer.addDataStream (
        "secondary",
        30000.0f);
    processor.setMainDataStream ("main");

    SyncControlButton mainButton (
        &processor,
        "Main synchronization",
        "main");
    SyncControlButton secondaryButton (
        &processor,
        "Secondary synchronization",
        "secondary");

    auto mainHandler =
        static_cast<Component&> (mainButton)
            .createAccessibilityHandler();
    auto secondaryHandler =
        static_cast<Component&> (secondaryButton)
            .createAccessibilityHandler();
    ASSERT_NE (mainHandler, nullptr);
    ASSERT_NE (secondaryHandler, nullptr);
    EXPECT_EQ (
        mainHandler->getRole(),
        AccessibilityRole::button);
    EXPECT_TRUE (
        mainHandler->getActions().contains (
            AccessibilityActionType::press));
    ASSERT_NE (
        mainHandler->getValueInterface(),
        nullptr);
    ASSERT_NE (
        secondaryHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        mainHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "off; main clock");
    EXPECT_EQ (
        secondaryHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "off; secondary clock");

    processor.synchronizer.startAcquisition();
    EXPECT_EQ (
        mainHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "synchronized; main clock");
    EXPECT_EQ (
        secondaryHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "synchronizing; secondary clock");
    processor.synchronizer.stopAcquisition();

    SynchronizingProcessor hardwareProcessor;
    hardwareProcessor.synchronizer.addDataStream (
        "hardware",
        30000.0f,
        -1,
        true);
    SyncControlButton hardwareButton (
        &hardwareProcessor,
        "Hardware synchronization",
        "hardware");
    auto hardwareHandler =
        static_cast<Component&> (hardwareButton)
            .createAccessibilityHandler();
    ASSERT_NE (hardwareHandler, nullptr);
    ASSERT_NE (
        hardwareHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        hardwareHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "hardware synchronized; secondary clock");
}
