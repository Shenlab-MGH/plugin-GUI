#include "../../Source/Processors/RecordNode/RecordNodeEditor.h"
#include "../../Source/Processors/Parameter/ParameterOwner.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/AccessClass.h"
#include "gtest/gtest.h"

namespace
{
class RecordNodeEditorAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        messageManagerLock =
            std::make_unique<MessageManagerLock>();
        AccessClass::clearAccessClassStateForTesting();
        processorGraph =
            std::make_unique<ProcessorGraph> (true);
    }

    void TearDown() override
    {
        processorGraph.reset();
        AccessClass::clearAccessClassStateForTesting();
        Parameter::parameterMap.clear();
        messageManagerLock.reset();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<MessageManagerLock>
        messageManagerLock;
    std::unique_ptr<ProcessorGraph> processorGraph;
};

class TestParameterOwner final : public ParameterOwner
{
public:
    TestParameterOwner() : ParameterOwner (Type::OTHER) {}

    void parameterChangeRequest (Parameter* parameter) override
    {
        parameter->updateValue();
    }
};

Component* findDescendantById (
    Component& parent,
    const String& componentId)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == componentId)
            return child;

        if (auto* descendant =
                findDescendantById (*child, componentId))
            return descendant;
    }

    return nullptr;
}

template <typename ComponentType>
ComponentType* findDescendantOfType (Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* match =
                dynamic_cast<ComponentType*> (child))
            return match;

        if (auto* descendant =
                findDescendantOfType<ComponentType> (*child))
            return descendant;
    }

    return nullptr;
}
} // namespace

TEST_F (RecordNodeEditorAccessibilityTests,
        ExposesEventAndSpikeRecordingToggles)
{
    TestParameterOwner owner;

    for (const auto& settings :
         std::initializer_list<std::array<const char*, 3>> {
             { "events",
               "Record Events",
               "Toggle saving events coming into this node" },
             { "spikes",
               "Record Spikes",
               "Toggle saving spikes coming into this node" } })
    {
        BooleanParameter parameter (
            &owner,
            Parameter::PROCESSOR_SCOPE,
            settings[0],
            settings[1],
            settings[2],
            true);
        parameter.setKey (
            "100|" + std::string (settings[0]));
        Parameter::registerParameter (&parameter);
        RecordToggleParameterEditor editor (&parameter);
        auto* toggle = editor.getEditor();
        ASSERT_NE (toggle, nullptr);

        EXPECT_EQ (
            toggle->getComponentID(),
            "oe.parameter.100_" + String (settings[0]));
        EXPECT_EQ (toggle->getTitle(), settings[1]);
        EXPECT_EQ (toggle->getDescription(), settings[2]);
        auto* label =
            findDescendantOfType<Label> (editor);
        ASSERT_NE (label, nullptr);
        EXPECT_FALSE (label->isAccessible());

        auto handler = toggle->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::toggleButton);
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::toggle));
        EXPECT_TRUE (
            handler->getCurrentState().isChecked());

        EXPECT_TRUE (
            handler->getActions().invoke (
                AccessibilityActionType::toggle));
        EXPECT_FALSE (parameter.getBoolValue());
        EXPECT_FALSE (
            handler->getCurrentState().isChecked());
    }
}

TEST_F (RecordNodeEditorAccessibilityTests,
        ExposesRecordingDirectoryValueAndDefaultAction)
{
    TestParameterOwner owner;
    const auto directory =
        File::getSpecialLocation (
            File::tempDirectory);
    PathParameter parameter (
        &owner,
        Parameter::PROCESSOR_SCOPE,
        "directory",
        "Directory",
        "Select a directory to write data to",
        directory,
        StringArray {},
        true,
        false);
    parameter.setKey ("100|directory");
    Parameter::registerParameter (&parameter);
    parameter.setNextValue (
        directory.getFullPathName(),
        false);
    RecordPathParameterEditor editor (&parameter);
    editor.updateView();

    auto* browse = editor.getEditor();
    ASSERT_NE (browse, nullptr);
    EXPECT_EQ (
        browse->getComponentID(),
        "oe.parameter.100_directory");
    EXPECT_EQ (browse->getTitle(), "Directory");
    EXPECT_EQ (
        browse->getDescription(),
        "Select a directory to write data to");
    EXPECT_FALSE (editor.getLabel()->isAccessible());

    auto browseHandler =
        browse->createAccessibilityHandler();
    ASSERT_NE (browseHandler, nullptr);
    EXPECT_EQ (
        browseHandler->getRole(),
        AccessibilityRole::button);
    ASSERT_NE (
        browseHandler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        browseHandler->getValueInterface()
            ->isReadOnly());
    EXPECT_EQ (
        browseHandler->getValueInterface()
            ->getCurrentValueAsString(),
        directory.getFullPathName());

    auto* useDefault = findDescendantById (
        editor,
        "oe.parameter.100_directory.use_default");
    ASSERT_NE (useDefault, nullptr);
    EXPECT_TRUE (useDefault->isVisible());
    EXPECT_EQ (
        useDefault->getTitle(),
        "Use default recording directory");
    EXPECT_EQ (
        useDefault->getDescription(),
        "Remove this Record Node directory override.");
    auto defaultHandler =
        useDefault->createAccessibilityHandler();
    ASSERT_NE (defaultHandler, nullptr);
    EXPECT_TRUE (
        defaultHandler->getActions().invoke (
            AccessibilityActionType::press));
    MessageManager::getInstance()
        ->runDispatchLoopUntil (50);
    EXPECT_EQ (
        parameter.getValueAsString(),
        "None");
    EXPECT_EQ (
        browseHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "default");
}
