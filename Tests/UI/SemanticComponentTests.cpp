#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

namespace
{
class TestCommandTarget final : public ApplicationCommandTarget
{
public:
    enum : CommandID
    {
        openCommand = 0x2000
    };

    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

    void getAllCommands (Array<CommandID>& commands) override
    {
        commands.add (openCommand);
    }

    void getCommandInfo (CommandID commandId,
                         ApplicationCommandInfo& result) override
    {
        if (commandId == openCommand)
            result.setInfo ("Open",
                            "Open a saved signal chain.",
                            "Test",
                            0);
    }

    bool perform (const InvocationInfo&) override { return true; }
};

class DisabledAccessibilityHandler final : public AccessibilityHandler
{
public:
    explicit DisabledAccessibilityHandler (Component& component)
        : AccessibilityHandler (component, AccessibilityRole::menuItem)
    {
    }

    bool isEnabled() const override { return false; }
};

class ButtonClickListener final : public Button::Listener
{
public:
    void buttonClicked (Button*) override
    {
        clickUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        clickCount.fetch_add (1);
    }

    std::atomic<int> clickCount { 0 };
    std::atomic<bool> clickUsedMessageThread { false };
};

class SemanticComponentMessageThreadTests
    : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        messageManagerLock =
            std::make_unique<MessageManagerLock>();
    }

    void TearDown() override
    {
        messageManagerLock.reset();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<MessageManagerLock>
        messageManagerLock;
};
} // namespace

TEST (SemanticComponentTests, ValidatesStableSemanticIds)
{
    EXPECT_TRUE (isValidSemanticId ("oe.window.main"));
    EXPECT_TRUE (isValidSemanticId ("oe.processor.100.parameter.start_time"));

    EXPECT_FALSE (isValidSemanticId (""));
    EXPECT_FALSE (isValidSemanticId ("oe"));
    EXPECT_FALSE (isValidSemanticId ("oe."));
    EXPECT_FALSE (isValidSemanticId ("OE.window.main"));
    EXPECT_FALSE (isValidSemanticId ("oe.window.Main"));
    EXPECT_FALSE (isValidSemanticId ("oe.window.main-button"));
    EXPECT_FALSE (isValidSemanticId ("oe.window..main"));
}

TEST (SemanticComponentTests, SanitisesDynamicIdSegments)
{
    EXPECT_EQ (sanitiseSemanticSegment ("Record Options"), "record_options");
    EXPECT_EQ (sanitiseSemanticSegment (" CH1 vs CH2 "), "ch1_vs_ch2");
    EXPECT_EQ (sanitiseSemanticSegment ("already_valid"), "already_valid");
    EXPECT_EQ (sanitiseSemanticSegment ("---"), "unnamed");
    EXPECT_EQ (createProcessorControlSemanticId (101, "FIFO drawer"),
               "oe.processor.101.fifo_drawer");
}

TEST (SemanticComponentTests, BuildsConfigurationScopedStreamRowIds)
{
    const StringArray uniqueSegments {
        "source_101.stream_probe_ap"
    };
    EXPECT_FALSE (
        streamSemanticSegmentNeedsIndexSuffix (
            uniqueSegments,
            0));
    EXPECT_EQ (
        createStreamSelectorRowSemanticId (
            "oe.processor.100.streams.table",
            uniqueSegments,
            0),
        "oe.processor.100.streams.table.source_101.stream_probe_ap");

    const StringArray collidingSegments {
        "source_101.stream_probe_ap",
        "source_101.stream_probe_ap",
        "source_101.stream_probe_ap"
    };
    EXPECT_TRUE (
        streamSemanticSegmentNeedsIndexSuffix (
            collidingSegments,
            2));
    EXPECT_FALSE (
        streamSemanticSegmentNeedsIndexSuffix (
            collidingSegments,
            -1));
    EXPECT_EQ (
        createStreamSelectorRowSemanticId (
            "oe.processor.100.streams.table",
            collidingSegments,
            2),
        "oe.processor.100.streams.table.source_101.stream_probe_ap.index_2");
    EXPECT_TRUE (
        createStreamSelectorRowSemanticId (
            "oe.processor.100.streams.table",
            collidingSegments,
            -1)
            .isEmpty());
    EXPECT_TRUE (
        createStreamSelectorRowSemanticId (
            "oe.processor.100.streams.table",
            collidingSegments,
            3)
            .isEmpty());
}

TEST (SemanticComponentTests, AppliesStableAccessibleMeaning)
{
    TextButton button ("R");

    applySemanticMetadata (button,
                           "oe.control.record_options",
                           "Recording options",
                           "Configure recording engine and directory behavior.",
                           "Opens the recording options panel.");

    EXPECT_EQ (button.getComponentID(), "oe.control.record_options");
    EXPECT_EQ (button.getTitle(), "Recording options");
    EXPECT_EQ (button.getDescription(), "Configure recording engine and directory behavior.");
    EXPECT_EQ (button.getHelpText(), "Opens the recording options panel.");
    EXPECT_TRUE (button.isAccessible());
    EXPECT_EQ (button.getTooltip(), "Opens the recording options panel.");
}

TEST (SemanticComponentTests, CreatesReadOnlyProgressSemantics)
{
    Component meter;
    double value = 0.25;
    auto handler = createReadOnlyProgressAccessibilityHandler (
        meter,
        [&] { return value; });

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::progressBar);

    auto* range = handler->getValueInterface();
    ASSERT_NE (range, nullptr);
    EXPECT_TRUE (range->isReadOnly());
    EXPECT_DOUBLE_EQ (range->getCurrentValue(), 0.25);
    EXPECT_DOUBLE_EQ (range->getRange().getMinimumValue(), 0.0);
    EXPECT_DOUBLE_EQ (range->getRange().getMaximumValue(), 1.0);

    value = 2.0;
    EXPECT_DOUBLE_EQ (range->getCurrentValue(), 1.0);
}

TEST (SemanticComponentTests, CreatesReadOnlyTextSemantics)
{
    Component status;
    String value = "idle";
    auto handler = createReadOnlyTextAccessibilityHandler (
        status,
        [&] { return value; });

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::staticText);

    auto* text = handler->getValueInterface();
    ASSERT_NE (text, nullptr);
    EXPECT_TRUE (text->isReadOnly());
    EXPECT_EQ (text->getCurrentValueAsString(), "idle");

    value = "active";
    EXPECT_EQ (text->getCurrentValueAsString(), "active");
}

TEST_F (SemanticComponentMessageThreadTests,
        ReadOnlyValueButtonProvidersAndActionsAreWorkerSafe)
{
    auto button =
        std::make_unique<ReadOnlyValueTextButton> (
            "initial");
    applySemanticMetadata (
        *button,
        "oe.parameter.100_directory",
        "Directory",
        "Select a directory to write data to");
    setButtonTextWithAccessibilityValue (
        *button,
        "C:\\recordings");

    ButtonClickListener listener;
    button->addListener (&listener);
    auto handler =
        button->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);

    String workerTitle;
    String workerDescription;
    String workerHelp;
    String workerValue;
    bool workerEnabled = false;
    AccessibleState workerState;
    std::thread reader (
        [&]
        {
            workerTitle = handler->getTitle();
            workerDescription =
                handler->getDescription();
            workerHelp = handler->getHelp();
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
            workerEnabled = handler->isEnabled();
            workerState = handler->getCurrentState();
        });
    reader.join();

    EXPECT_EQ (workerTitle, "Directory");
    EXPECT_EQ (
        workerDescription,
        "Select a directory to write data to");
    EXPECT_EQ (workerHelp, workerDescription);
    EXPECT_EQ (workerValue, "C:\\recordings");
    EXPECT_TRUE (workerEnabled);
    EXPECT_TRUE (workerState.isFocusable());

    const auto actions = handler->getActions();
    const auto invokePressFromWorker =
        [&]
        {
            std::atomic<bool> completed { false };
            bool invoked = false;
            std::thread worker (
                [&]
                {
                    invoked = actions.invoke (
                        AccessibilityActionType::press);
                    completed.store (true);
                });
            while (! completed.load())
            {
                MessageManager::getInstance()
                    ->runDispatchLoopUntil (10);
            }
            worker.join();
            return invoked;
        };

    EXPECT_TRUE (invokePressFromWorker());
    for (int attempt = 0;
         attempt < 20
             && listener.clickCount.load() == 0;
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    EXPECT_EQ (listener.clickCount.load(), 1);
    EXPECT_TRUE (
        listener.clickUsedMessageThread.load());

    Component parent;
    parent.addAndMakeVisible (button.get());
    parent.setEnabled (false);
    EXPECT_FALSE (handler->isEnabled());
    EXPECT_TRUE (invokePressFromWorker());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.clickCount.load(), 1);
    parent.setEnabled (true);
    EXPECT_TRUE (handler->isEnabled());

    button->setEnabled (false);
    EXPECT_FALSE (handler->isEnabled());
    EXPECT_TRUE (invokePressFromWorker());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.clickCount.load(), 1);

    handler.reset();
    button.reset();
    EXPECT_TRUE (invokePressFromWorker());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.clickCount.load(), 1);
}

TEST (SemanticComponentTests, PreservesPopupMenuAccessibilityMetadata)
{
    PopupMenu menu;
    PopupMenu::Item item ("Open");
    item.itemID = 1;
    item.accessibilityId = "oe.menu.file.open";
    item.accessibilityDescription = "Open a saved signal chain.";
    item.accessibilityHelp = "Choose an existing Open Ephys settings file.";
    menu.addItem (std::move (item));

    PopupMenu copiedMenu (menu);
    PopupMenu::MenuItemIterator iterator (copiedMenu);

    ASSERT_TRUE (iterator.next());
    EXPECT_EQ (iterator.getItem().accessibilityId, "oe.menu.file.open");
    EXPECT_EQ (iterator.getItem().accessibilityDescription,
               "Open a saved signal chain.");
    EXPECT_EQ (iterator.getItem().accessibilityHelp,
               "Choose an existing Open Ephys settings file.");
}

TEST (SemanticComponentTests, CopiesCommandMeaningToSemanticMenuItems)
{
    TestCommandTarget target;
    ApplicationCommandManager commandManager;
    commandManager.registerAllCommandsForTarget (&target);
    PopupMenu menu;

    addSemanticCommandItem (
        menu,
        &commandManager,
        TestCommandTarget::openCommand,
        "oe.menu.file.open");

    PopupMenu::MenuItemIterator iterator (menu);
    ASSERT_TRUE (iterator.next());
    EXPECT_EQ (iterator.getItem().accessibilityId,
               "oe.menu.file.open");
    EXPECT_EQ (iterator.getItem().accessibilityDescription,
               "Open a saved signal chain.");
    EXPECT_EQ (iterator.getItem().accessibilityHelp,
               "Open a saved signal chain.");
}

TEST (SemanticComponentTests, AddsSemanticSubMenus)
{
    PopupMenu childMenu;
    childMenu.addItem (1, "Default");
    PopupMenu parentMenu;

    addSemanticSubMenu (
        parentMenu,
        "Clock display mode",
        std::move (childMenu),
        "oe.menu.view.clock_display_mode",
        "Choose how elapsed time is displayed.");

    PopupMenu::MenuItemIterator iterator (parentMenu);
    ASSERT_TRUE (iterator.next());
    EXPECT_EQ (iterator.getItem().accessibilityId,
               "oe.menu.view.clock_display_mode");
    EXPECT_EQ (iterator.getItem().accessibilityDescription,
               "Choose how elapsed time is displayed.");
    EXPECT_EQ (iterator.getItem().accessibilityHelp,
               "Choose how elapsed time is displayed.");
    ASSERT_NE (iterator.getItem().subMenu, nullptr);
    EXPECT_EQ (iterator.getItem().subMenu->getNumItems(), 1);
}

TEST (SemanticComponentTests, AllowsSemanticElementsToReportDisabledState)
{
    Component component;
    DisabledAccessibilityHandler handler (component);

    EXPECT_TRUE (component.isEnabled());
    EXPECT_FALSE (handler.isEnabled());
}

TEST (SemanticComponentTests, IgnoresInvalidSemanticIds)
{
    TextButton button ("R");
    button.setComponentID ("existing.id");
    button.setTitle ("Existing title");

    applySemanticMetadata (button, "invalid-id", "New title", "New description");

    EXPECT_EQ (button.getComponentID(), "existing.id");
    EXPECT_EQ (button.getTitle(), "Existing title");
    EXPECT_TRUE (button.getDescription().isEmpty());
}

TEST (SemanticComponentTests, ExposesEditableTextThroughAValueInterface)
{
    TextEditor editor;
    editor.setText ("before");

    auto handler = editor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::editableText);

    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    EXPECT_FALSE (value->isReadOnly());
    EXPECT_EQ (value->getCurrentValueAsString(), "before");

    value->setValueAsString ("after");
    EXPECT_EQ (editor.getText(), "after");
}
