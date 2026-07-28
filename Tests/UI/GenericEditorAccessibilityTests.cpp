#include "../../Source/Processors/Editors/GenericEditor.h"
#include "../../Source/Processors/Editors/ElectrodeButtons.h"
#include "../../Source/Processors/Editors/StreamSelector.h"
#include "../../Source/Processors/Editors/VisualizerEditor.h"
#include "../../Source/Processors/GenericProcessor/GenericProcessor.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/Processors/Settings/DataStream.h"
#include "../../Source/UI/EditorViewport.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace
{
class GenericEditorTestApplication final
    : public JUCEApplication
{
public:
    const String getApplicationName()
        override
    {
        return "Generic editor accessibility tests";
    }

    const String getApplicationVersion()
        override
    {
        return "1.0";
    }

    void initialise (
        const String&) override
    {
    }

    void shutdown() override
    {
    }
};

class ScopedGenericEditorTestApplication
{
public:
    ScopedGenericEditorTestApplication()
        : application (
              std::make_unique<
                  GenericEditorTestApplication>())
    {
        AccessClass::
            clearAccessClassStateForTesting();
        MessageManager::getInstance();
        initialised =
            static_cast<
                JUCEApplicationBase*> (
                    application.get())
                ->initialiseApp();
    }

    ~ScopedGenericEditorTestApplication()
    {
        if (initialised)
        {
            static_cast<
                JUCEApplicationBase*> (
                    application.get())
                ->shutdownApp();
        }
        AccessClass::
            clearAccessClassStateForTesting();
    }

    bool wasInitialised() const
    {
        return initialised;
    }

private:
    std::unique_ptr<
        GenericEditorTestApplication>
        application;
    bool initialised = false;
};

class ThreadTrackingButtonListener final
    : public Button::Listener
{
public:
    void buttonClicked (Button*) override
    {
        callbackCount.fetch_add (1);
        callbackUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
    }

    std::atomic<int> callbackCount { 0 };
    std::atomic<bool>
        callbackUsedMessageThread { false };
};

class TestStreamProcessor final : public GenericProcessor
{
public:
    explicit TestStreamProcessor (
        int nodeId = 0,
        Plugin::Processor::Type type =
            Plugin::Processor::FILTER)
        : GenericProcessor (
              "Test Processor")
    {
        setNodeId (nodeId);
        setProcessorType (type);
    }

    void process (AudioBuffer<float>&) override {}
};

class TestStreamToggleProcessor final
    : public GenericProcessor
{
public:
    TestStreamToggleProcessor()
        : GenericProcessor (
              "Test Processor")
    {
        setProcessorType (
            Plugin::Processor::FILTER);
    }

    void process (
        AudioBuffer<float>&) override
    {
    }

    void parameterChangeRequest (
        Parameter* parameter) override
    {
        parameterChangeUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        parameter->updateValue();
        parameter->valueChanged();

        if (parameter->getName()
                == "enable_stream"
            && parameter->getStreamId()
                   > 0)
        {
            getEditor()
                ->streamEnabledStateChanged (
                    parameter->getStreamId(),
                    static_cast<bool> (
                        parameter->getValue()));
        }
    }

    std::atomic<bool>
        parameterChangeUsedMessageThread {
            false
        };
};

class TestDataStream final : public DataStream
{
public:
    TestDataStream (Settings settings, int sourceNodeId)
        : DataStream (std::move (settings))
    {
        setSourceNodeId (sourceNodeId);
    }
};

class InspectableGenericEditor final : public GenericEditor
{
public:
    explicit InspectableGenericEditor (GenericProcessor* processor)
        : GenericEditor (processor)
    {
    }

    StreamSelectorTable& getStreamSelector() { return *streamSelector; }

    void selectedStreamHasChanged() override
    {
        selectedStreamChangeUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        selectedStreamChangeRan.store (true);
    }

    std::atomic<bool> selectedStreamChangeRan { false };
    std::atomic<bool> selectedStreamChangeUsedMessageThread { false };
};

class ProcessorProxyHarness
{
public:
    explicit ProcessorProxyHarness (
        Plugin::Processor::Type type =
            Plugin::Processor::FILTER)
        : processor (100, type),
          editor (&processor),
          viewport (
              new EditorViewport (
                  &navigation))
    {
        viewport->editorArray.add (
            &editor);
        viewport->addAndMakeVisible (
            &editor);
        viewport->setBounds (
            0,
            0,
            800,
            400);
        navigation.setBounds (
            0,
            0,
            800,
            400);
        viewport->refreshEditors();
        proxy =
            viewport
                ->getProcessorAccessibilityProxy (
                    editor);
    }

    TestStreamProcessor processor;
    InspectableGenericEditor editor;
    SignalChainTabComponent navigation;
    EditorViewport* viewport;
    Component* proxy = nullptr;
};

class InspectableUtilityButton final : public UtilityButton
{
public:
    explicit InspectableUtilityButton (String label)
        : UtilityButton (std::move (label))
    {
    }

    using UtilityButton::createAccessibilityHandler;
};

class InspectableElectrodeButton final : public ElectrodeButton
{
public:
    explicit InspectableElectrodeButton (int channel)
        : ElectrodeButton (channel)
    {
    }

    using ElectrodeButton::createAccessibilityHandler;
};

class InspectableSelectorButton final : public SelectorButton
{
public:
    explicit InspectableSelectorButton (String name)
        : SelectorButton (std::move (name))
    {
    }

    using SelectorButton::createAccessibilityHandler;
};

class InspectableVisualizerEditor final : public VisualizerEditor
{
public:
    explicit InspectableVisualizerEditor (GenericProcessor* processor)
        : VisualizerEditor (processor, "Test visualizer")
    {
    }

    Visualizer* createNewCanvas() override
    {
        return nullptr;
    }

    SelectorButton& getWindowSelector()
    {
        return *windowSelector;
    }

    SelectorButton& getTabSelector()
    {
        return *tabSelector;
    }
};

Component* findDescendantBySemanticId (Component& parent, const String& id)
{
    if (parent.getComponentID() == id)
        return &parent;

    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* descendant = findDescendantBySemanticId (*child, id))
            return descendant;
    }

    return nullptr;
}

Component* findDesktopComponentBySemanticId (
    StringRef id)
{
    auto& desktop =
        Desktop::getInstance();

    for (int index = 0;
         index < desktop
                     .getNumComponents();
         ++index)
    {
        if (auto* component =
                desktop.getComponent (
                    index))
        {
            if (auto* match =
                    findDescendantBySemanticId (
                        *component,
                        String (id)))
                return match;
        }
    }

    return nullptr;
}

const PopupMenu::Item* findMenuItem (
    const PopupMenu& menu,
    int itemId)
{
    PopupMenu::MenuItemIterator iterator (
        menu);

    while (iterator.next())
        if (iterator.getItem().itemID
            == itemId)
            return &iterator.getItem();

    return nullptr;
}
} // namespace

TEST (GenericEditorAccessibilityTests,
      ExposesVisualizerDestinationSelectors)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());

    TestStreamProcessor processor (100);
    InspectableVisualizerEditor editor (&processor);

    auto& window = editor.getWindowSelector();
    auto& tab = editor.getTabSelector();

    EXPECT_EQ (
        window.getComponentID(),
        "oe.processor.100.visualizer.open_in_window");
    EXPECT_EQ (
        window.getTitle(),
        "Open Test Processor visualizer in window");
    EXPECT_EQ (
        window.getDescription(),
        "Show the Test Processor visualizer in a separate window.");
    EXPECT_EQ (
        tab.getComponentID(),
        "oe.processor.100.visualizer.open_in_tab");
    EXPECT_EQ (
        tab.getTitle(),
        "Open Test Processor visualizer in tab");
    EXPECT_EQ (
        tab.getDescription(),
        "Show the Test Processor visualizer in a view tab.");

    EXPECT_TRUE (window.isAccessible());
    EXPECT_TRUE (tab.isAccessible());

    for (const auto& name :
         { String ("Visualizer Window Button"),
           String ("Visualizer Tab Button") })
    {
        InspectableSelectorButton selector (
            name);
        auto ownedHandler =
            selector
                .createAccessibilityHandler();
        auto* handler =
            ownedHandler.get();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::toggleButton);
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::toggle));
        EXPECT_FALSE (
            handler->getActions().contains (
                AccessibilityActionType::press));
        EXPECT_TRUE (
            handler->getCurrentState().isCheckable());
        EXPECT_FALSE (
            handler->getCurrentState().isChecked());
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_TRUE (
            handler->getValueInterface()
                ->isReadOnly());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            "Off");
    }
}

TEST (GenericEditorAccessibilityTests,
      VisualizerDestinationActionRunsOnMessageThread)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());

    InspectableSelectorButton selector (
        "Visualizer Window Button");
    applySemanticMetadata (
        selector,
        "oe.test.visualizer.open_in_window",
        "Open test visualizer in window",
        "Show the test visualizer in a window.",
        "Choose a separate visualizer window.");
    selector.refreshAccessibilityState();
    ThreadTrackingButtonListener listener;
    selector.addListener (&listener);
    auto handler =
        selector.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);

    std::atomic<bool> workerEntered { false };
    std::atomic<bool> invoked { false };
    String workerTitle;
    String workerDescription;
    String workerHelp;
    String workerValue;
    bool workerEnabled = false;
    bool workerChecked = true;
    std::thread worker (
        [&]
        {
            workerTitle =
                handler->getTitle();
            workerDescription =
                handler->getDescription();
            workerHelp =
                handler->getHelp();
            workerValue =
                value
                    ->getCurrentValueAsString();
            workerEnabled =
                handler->isEnabled();
            workerChecked =
                handler->getCurrentState()
                    .isChecked();
            workerEntered.store (true);
            invoked.store (
                handler->getActions().invoke (
                    AccessibilityActionType::toggle));
        });

    while (! workerEntered.load())
        std::this_thread::yield();

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 20
             && ! invoked.load();
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (
            10);
    }
    worker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_EQ (
        workerTitle,
        "Open test visualizer in window");
    EXPECT_EQ (
        workerDescription,
        "Show the test visualizer in a window.");
    EXPECT_EQ (
        workerHelp,
        "Choose a separate visualizer window.");
    EXPECT_EQ (
        workerValue,
        "Off");
    EXPECT_TRUE (workerEnabled);
    EXPECT_FALSE (workerChecked);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener.callbackUsedMessageThread
            .load());
    EXPECT_TRUE (
        selector.getToggleState());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "On");
}

TEST (GenericEditorAccessibilityTests,
      VisualizerDestinationRejectsDisabledAndStaleActions)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());

    ThreadTrackingButtonListener listener;
    auto selector =
        std::make_unique<
            InspectableSelectorButton> (
            "Visualizer Tab Button");
    selector->addListener (&listener);
    auto handler =
        selector
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    selector->setEnabled (false);
    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::toggle));
    EXPECT_FALSE (
        selector->getToggleState());
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);
    EXPECT_FALSE (
        handler->isEnabled());

    const auto staleActions =
        handler->getActions();
    selector->setEnabled (true);
    selector->setClickingTogglesState (
        false);
    EXPECT_TRUE (
        staleActions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_FALSE (
        selector->getToggleState());
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);

    selector->setClickingTogglesState (
        true);
    selector->setRadioGroupId (
        1,
        dontSendNotification);
    EXPECT_TRUE (
        staleActions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_FALSE (
        selector->getToggleState());
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);

    handler.reset();
    selector.reset();

    EXPECT_TRUE (
        staleActions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);
}

TEST (GenericEditorAccessibilityTests,
      QueuedVisualizerDestinationActionDoesNotOutliveButton)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());

    ThreadTrackingButtonListener listener;
    auto selector =
        std::make_unique<
            InspectableSelectorButton> (
            "Visualizer Window Button");
    selector->addListener (&listener);
    auto handler =
        selector
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();
    handler.reset();

    std::atomic<bool> workerEntered { false };
    std::atomic<bool> invoked { false };
    std::thread worker (
        [&, actions]
        {
            workerEntered.store (true);
            invoked.store (
                actions.invoke (
                    AccessibilityActionType::toggle));
        });

    while (! workerEntered.load())
        std::this_thread::yield();

    selector.reset();
    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 20
             && ! invoked.load();
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (
            10);
    }
    worker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);
}

TEST (GenericEditorAccessibilityTests,
      ExternalVisualizerCloseRefreshesPublishedState)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());

    InspectableSelectorButton selector (
        "Visualizer Window Button");
    selector.setToggleState (
        true,
        dontSendNotification);
    auto handler =
        selector.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "On");

    DataWindow window (
        &selector,
        "Test visualizer");
    window.closeButtonPressed();

    EXPECT_FALSE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Off");

    TestStreamProcessor processor (100);
    InspectableVisualizerEditor editor (
        &processor);
    editor.getTabSelector()
        .setToggleState (
            true,
            dontSendNotification);
    editor.tabWasClosed();
    EXPECT_FALSE (
        editor.getTabSelector()
            .getToggleState());
}

TEST (GenericEditorAccessibilityTests,
      ExposesProcessorAsContextMenuTarget)
{
    ProcessorProxyHarness harness;
    ASSERT_NE (
        harness.proxy,
        nullptr);

    EXPECT_EQ (
        harness.editor
            .getComponentID(),
        "oe.processor.100.editor");
    EXPECT_EQ (
        harness.editor.getTitle(),
        "Test Processor (node 100)");
    EXPECT_EQ (
        harness.editor
            .getDescription(),
        "Test Processor processor 100 in the signal chain.");
    EXPECT_EQ (
        harness.proxy
            ->getComponentID(),
        "oe.processor.100.editor.actions");
    EXPECT_EQ (
        harness.proxy->getTitle(),
        "Test Processor processor actions");
    EXPECT_EQ (
        harness.proxy
            ->getDescription(),
        "Select Test Processor processor 100 or open its existing processor menu.");

    auto handler =
        createProcessorAccessibilityHandlerForTesting (
            *harness.proxy);
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::group);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::
                showMenu));
    EXPECT_FALSE (
        handler->getCurrentState()
            .isSelectable());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isExpandable());

    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        harness.editor
            .getSelectionState());

    harness.processor
        .setNodeId (101);
    harness.editor.updateName();
    harness.viewport
        ->refreshEditors();
    EXPECT_EQ (
        harness.editor
            .getComponentID(),
        "oe.processor.101.editor");
    EXPECT_EQ (
        harness.editor.getTitle(),
        "Test Processor (node 101)");
    EXPECT_EQ (
        harness.editor
            .getDescription(),
        "Test Processor processor 101 in the signal chain.");
    EXPECT_EQ (
        harness.proxy
            ->getComponentID(),
        "oe.processor.101.editor.actions");
}

TEST (GenericEditorAccessibilityTests,
      ExposesEveryProcessorContextMenuAction)
{
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (
        &processor);

    const auto menu =
        createProcessorContextMenu (
            editor,
            false,
            false);

    struct ExpectedItem
    {
        int itemId;
        const char* text;
        const char* accessibilityId;
        const char* description;
    };

    const std::array<ExpectedItem, 6>
        expectedItems {
            ExpectedItem {
                3,
                "Collapse",
                "oe.processor.100.context.toggle_collapse",
                "Collapse Test Processor processor 100." },
            ExpectedItem {
                2,
                "Delete",
                "oe.processor.100.context.delete_selected",
                "Delete all selected processors from the signal chain." },
            ExpectedItem {
                1,
                "Rename",
                "oe.processor.100.context.rename",
                "Rename Test Processor processor 100." },
            ExpectedItem {
                4,
                "Save settings...",
                "oe.processor.100.context.save_settings",
                "Save settings for Test Processor processor 100." },
            ExpectedItem {
                5,
                "Load settings...",
                "oe.processor.100.context.load_settings",
                "Load settings for Test Processor processor 100." },
            ExpectedItem {
                6,
                "Save image...",
                "oe.processor.100.context.save_image",
                "Save an image of Test Processor processor 100." }
        };

    for (const auto& expected :
         expectedItems)
    {
        const auto* item =
            findMenuItem (
                menu,
                expected.itemId);
        ASSERT_NE (item, nullptr);
        EXPECT_EQ (
            item->text,
            expected.text);
        EXPECT_EQ (
            item->accessibilityId,
            expected.accessibilityId);
        EXPECT_EQ (
            item->accessibilityDescription,
            expected.description);
        EXPECT_TRUE (item->isEnabled);
    }

    const auto restrictedMenu =
        createProcessorContextMenu (
            editor,
            true,
            true);
    EXPECT_FALSE (
        findMenuItem (
            restrictedMenu,
            1)
            ->isEnabled);
    EXPECT_FALSE (
        findMenuItem (
            restrictedMenu,
            2)
            ->isEnabled);
    EXPECT_FALSE (
        findMenuItem (
            restrictedMenu,
            5)
            ->isEnabled);
    EXPECT_TRUE (
        findMenuItem (
            restrictedMenu,
            3)
            ->isEnabled);
    EXPECT_TRUE (
        findMenuItem (
            restrictedMenu,
            4)
            ->isEnabled);
    EXPECT_TRUE (
        findMenuItem (
            restrictedMenu,
            6)
            ->isEnabled);

}

TEST (GenericEditorAccessibilityTests,
      PreservesProcessorContextMenuMouseFallthrough)
{
    EXPECT_FALSE (
        processorContextMenuConsumesTitleClick (
            0));
    EXPECT_TRUE (
        processorContextMenuConsumesTitleClick (
            1));
    EXPECT_TRUE (
        processorContextMenuConsumesTitleClick (
            2));
    EXPECT_TRUE (
        processorContextMenuConsumesTitleClick (
            3));
    EXPECT_FALSE (
        processorContextMenuConsumesTitleClick (
            4));
    EXPECT_FALSE (
        processorContextMenuConsumesTitleClick (
            5));
    EXPECT_TRUE (
        processorContextMenuConsumesTitleClick (
            6));
    EXPECT_FALSE (
        processorContextMenuConsumesTitleClick (
            7));
}

TEST (GenericEditorAccessibilityTests,
      DoesNotAdvertiseUnavailableProcessorMenus)
{
    const std::array<
        Plugin::Processor::Type,
        3>
        unsupportedTypes {
            Plugin::Processor::EMPTY,
            Plugin::Processor::MERGER,
            Plugin::Processor::SPLITTER
        };

    for (const auto type :
         unsupportedTypes)
    {
        ProcessorProxyHarness harness (
            type);
        ASSERT_NE (
            harness.proxy,
            nullptr);
        auto handler =
            createProcessorAccessibilityHandlerForTesting (
                *harness.proxy);
        ASSERT_NE (handler, nullptr);
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        showMenu));
        EXPECT_FALSE (
            handler->getCurrentState()
                .isExpandable());
    }
}

TEST (GenericEditorAccessibilityTests,
      ProcessorSelectionRunsOnMessageThread)
{
    auto* messageManager =
        MessageManager::getInstance();
    ProcessorProxyHarness harness;
    ASSERT_NE (
        harness.proxy,
        nullptr);
    auto handler =
        createProcessorAccessibilityHandlerForTesting (
            *harness.proxy);
    ASSERT_NE (handler, nullptr);

    std::thread worker (
        [handler =
             handler.get()]
        {
            handler->getActions()
                .invoke (
                    AccessibilityActionType::
                        press);
        });
    worker.join();

    EXPECT_FALSE (
        harness.editor
            .getSelectionState());

    for (int attempt = 0;
         attempt < 20
             && ! harness.editor
                       .getSelectionState();
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }

    EXPECT_TRUE (
        harness.editor
            .getSelectionState());
}

TEST (GenericEditorAccessibilityTests,
      ProcessorContextMenuOpensWithSemanticItems)
{
    ScopedGenericEditorTestApplication
        application;
    ASSERT_TRUE (
        application.wasInitialised());
    auto* messageManager =
        MessageManager::getInstance();

    {
        MessageManagerLock lock;
        ProcessorProxyHarness harness;
        ASSERT_NE (
            harness.proxy,
            nullptr);
        harness.navigation
            .setVisible (
            true);
        harness.navigation
            .addToDesktop (0);
        harness.navigation
            .setAlwaysOnTop (
            true);
        harness.navigation
            .toFront (
            false);
        ASSERT_TRUE (
            harness.navigation
                .isShowing());
        ASSERT_TRUE (
            harness.proxy
                ->isShowing());

        auto* handler =
            harness.proxy
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);

#if JUCE_WINDOWS
        Microsoft::WRL::ComPtr<
            IUIAutomation>
            automation;
        ASSERT_TRUE (
            SUCCEEDED (
                CoCreateInstance (
                    CLSID_CUIAutomation,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS (
                        &automation))));
        Microsoft::WRL::ComPtr<
            IUIAutomationElement>
            rootElement;
        ASSERT_TRUE (
            SUCCEEDED (
                automation
                    ->ElementFromHandle (
                        static_cast<HWND> (
                            harness
                                .navigation
                                .getWindowHandle()),
                        &rootElement)));

        VARIANT expectedId;
        VariantInit (
            &expectedId);
        expectedId.vt = VT_BSTR;
        expectedId.bstrVal =
            SysAllocString (
                L"oe.processor.100.editor.actions");
        ASSERT_NE (
            expectedId.bstrVal,
            nullptr);
        Microsoft::WRL::ComPtr<
            IUIAutomationCondition>
            idCondition;
        const auto conditionResult =
            automation
                ->CreatePropertyCondition (
                    UIA_AutomationIdPropertyId,
                    expectedId,
                    &idCondition);
        VariantClear (
            &expectedId);
        ASSERT_TRUE (
            SUCCEEDED (
                conditionResult));

        Microsoft::WRL::ComPtr<
            IUIAutomationElement>
            editorElement;
        ASSERT_TRUE (
            SUCCEEDED (
                rootElement
                    ->FindFirst (
                        TreeScope_Subtree,
                        idCondition.Get(),
                        &editorElement)));
        ASSERT_NE (
            editorElement.Get(),
            nullptr);
        BSTR automationId =
            nullptr;
        ASSERT_TRUE (
            SUCCEEDED (
                editorElement
                    ->get_CurrentAutomationId (
                        &automationId)));
        ASSERT_NE (
            automationId,
            nullptr);
        EXPECT_EQ (
            std::wstring (
                automationId),
            std::wstring (
                L"oe.processor.100.editor.actions"));
        SysFreeString (
            automationId);

        Microsoft::WRL::ComPtr<
            IUIAutomationExpandCollapsePattern>
            expandCollapse;
        ASSERT_TRUE (
            SUCCEEDED (
                editorElement
                    ->GetCurrentPatternAs (
                        UIA_ExpandCollapsePatternId,
                        IID_PPV_ARGS (
                            &expandCollapse))));
        ASSERT_NE (
            expandCollapse.Get(),
            nullptr);
        EXPECT_TRUE (
            SUCCEEDED (
                expandCollapse
                    ->Expand()));
#else
        EXPECT_TRUE (
            handler->getActions().invoke (
                AccessibilityActionType::
                    showMenu));
#endif

        Component* deleteItem =
            nullptr;
        for (int attempt = 0;
             attempt < 30
                 && deleteItem
                        == nullptr;
             ++attempt)
        {
            messageManager
                ->runDispatchLoopUntil (
                    10);
            deleteItem =
                findDesktopComponentBySemanticId (
                    "oe.processor.100.context.delete_selected");
        }

        EXPECT_TRUE (
            handler->getCurrentState()
                .isExpanded());
        ASSERT_NE (
            deleteItem,
            nullptr);
        EXPECT_EQ (
            deleteItem->getDescription(),
            "Delete all selected processors from the signal chain.");
        auto* deleteHandler =
            deleteItem
                ->getAccessibilityHandler();
        ASSERT_NE (
            deleteHandler,
            nullptr);
        EXPECT_EQ (
            deleteHandler->getRole(),
            AccessibilityRole::
                menuItem);
        EXPECT_TRUE (
            deleteHandler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));

#if JUCE_WINDOWS
        EXPECT_TRUE (
            SUCCEEDED (
                expandCollapse
                    ->Collapse()));
#else
        EXPECT_TRUE (
            handler->getActions().invoke (
                AccessibilityActionType::
                    showMenu));
#endif
        EXPECT_TRUE (
            handler->getCurrentState()
                .isCollapsed());

        for (int attempt = 0;
         attempt < 30
                 && findDesktopComponentBySemanticId (
                        "oe.processor.100.context.delete_selected")
                        != nullptr;
             ++attempt)
        {
            messageManager
                ->runDispatchLoopUntil (
                    10);
        }

        EXPECT_TRUE (
            handler->getCurrentState()
                .isCollapsed());
        EXPECT_EQ (
            findDesktopComponentBySemanticId (
                "oe.processor.100.context.delete_selected"),
            nullptr);

#if JUCE_WINDOWS
        ExpandCollapseState
            expandCollapseState =
                ExpandCollapseState_Expanded;
        EXPECT_TRUE (
            SUCCEEDED (
                expandCollapse
                    ->get_CurrentExpandCollapseState (
                        &expandCollapseState)));
        EXPECT_EQ (
            expandCollapseState,
            ExpandCollapseState_Collapsed);
#endif
    }

}

TEST (GenericEditorAccessibilityTests,
      MouseOpenedProcessorMenuReportsExpandedUntilDismissed)
{
    ScopedGenericEditorTestApplication
        application;
    ASSERT_TRUE (
        application.wasInitialised());

    MessageManagerLock lock;
    ProcessorProxyHarness harness;
    ASSERT_NE (
        harness.proxy,
        nullptr);
    harness.navigation
        .setVisible (
        true);
    harness.navigation
        .addToDesktop (0);
    ASSERT_TRUE (
        harness.navigation
            .isShowing());

    auto* handler =
        harness.proxy
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);

    struct MenuObservation
    {
        bool sawExpanded = false;
        bool sawMenuItem = false;
    };
    auto observation =
        std::make_shared<
            MenuObservation>();
    const auto safeProxy =
        Component::SafePointer<
            Component> (
                harness.proxy);
    MessageManager::callAsync (
        [observation,
         safeProxy]
        {
            if (safeProxy
                != nullptr)
            {
                if (auto* currentHandler =
                        safeProxy
                            ->getAccessibilityHandler())
                {
                    observation
                        ->sawExpanded =
                            currentHandler
                                ->getCurrentState()
                                .isExpanded();
                }
            }
            observation
                ->sawMenuItem =
                findDesktopComponentBySemanticId (
                    "oe.processor.100.context.delete_selected")
                != nullptr;
            PopupMenu::
                dismissAllActiveMenus();
        });

    const auto eventTime =
        Time::getCurrentTime();
    MouseEvent rightClick (
        Desktop::getInstance()
            .getMainMouseSource(),
        Point<float> (
            10.0f,
            10.0f),
        ModifierKeys (
            ModifierKeys::
                rightButtonModifier),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &harness.editor,
        &harness.editor,
        eventTime,
        Point<float> (
            10.0f,
            10.0f),
        eventTime,
        1,
        false);

    harness.viewport
        ->mouseDown (
            rightClick);

    EXPECT_TRUE (
        observation
            ->sawExpanded);
    EXPECT_TRUE (
        observation
            ->sawMenuItem);
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isCollapsed());
}

TEST (GenericEditorAccessibilityTests,
      MouseMenuSurvivesProcessorRemovalDuringNestedDispatch)
{
    ScopedGenericEditorTestApplication
        application;
    ASSERT_TRUE (
        application.wasInitialised());

    MessageManagerLock lock;
    ProcessorProxyHarness harness;
    ASSERT_NE (
        harness.proxy,
        nullptr);
    harness.navigation
        .setVisible (
        true);
    harness.navigation
        .addToDesktop (0);
    ASSERT_TRUE (
        harness.navigation
            .isShowing());

    auto removalRan =
        std::make_shared<
            bool> (
                false);
    const auto safeViewport =
        Component::SafePointer<
            EditorViewport> (
                harness.viewport);
    const auto safeEditor =
        Component::SafePointer<
            GenericEditor> (
                &harness.editor);
    MessageManager::callAsync (
        [removalRan,
         safeViewport,
         safeEditor]
        {
            if (safeViewport
                    != nullptr
                && safeEditor
                       != nullptr)
            {
                safeViewport
                    ->removeEditor (
                        safeEditor
                            .getComponent());
                *removalRan =
                    true;
            }
            PopupMenu::
                dismissAllActiveMenus();
        });

    const auto eventTime =
        Time::getCurrentTime();
    MouseEvent rightClick (
        Desktop::getInstance()
            .getMainMouseSource(),
        Point<float> (
            10.0f,
            10.0f),
        ModifierKeys (
            ModifierKeys::
                rightButtonModifier),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &harness.editor,
        &harness.editor,
        eventTime,
        Point<float> (
            10.0f,
            10.0f),
        eventTime,
        1,
        false);

    harness.viewport
        ->mouseDown (
            rightClick);

    EXPECT_TRUE (
        *removalRan);
    EXPECT_EQ (
        harness.viewport
            ->getProcessorAccessibilityProxy (
                harness.editor),
        nullptr);
    EXPECT_FALSE (
        harness.viewport
            ->editorArray
            .contains (
                &harness.editor));
}

TEST (GenericEditorAccessibilityTests, PublishesElectrodeChannelAsValue)
{
    InspectableElectrodeButton button (-1);
    button.setTitle ("Left audio output channel");

    auto handler = button.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::button);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    ASSERT_NE (handler->getValueInterface(), nullptr);
    EXPECT_TRUE (handler->getValueInterface()->isReadOnly());
    EXPECT_EQ (
        handler->getValueInterface()->getCurrentValueAsString(),
        "None");

    button.setChannelNum (12);

    EXPECT_EQ (
        handler->getValueInterface()->getCurrentValueAsString(),
        "12");
}

TEST (GenericEditorAccessibilityTests,
      ElectrodeProvidersAndActionsAreWorkerSafe)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());
    auto* messageManager =
        MessageManager::getInstance();
    MessageManagerLock lock;
    auto button =
        std::make_unique<
            InspectableElectrodeButton> (
            4);
    button->setTitle (
        "Probe electrode 4");
    button->setDescription (
        "Enable or disable electrode 4 for acquisition.");
    ThreadTrackingButtonListener listener;
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
            workerState =
                handler->getCurrentState();
        });
    reader.join();

    EXPECT_EQ (
        workerTitle,
        "Probe electrode 4");
    EXPECT_EQ (
        workerDescription,
        "Enable or disable electrode 4 for acquisition.");
    ASSERT_EQ (
        workerHelp,
        workerDescription);
    EXPECT_EQ (workerValue, "4");
    EXPECT_TRUE (workerEnabled);
    EXPECT_TRUE (workerState.isFocusable());
    EXPECT_TRUE (workerState.isCheckable());
    EXPECT_TRUE (workerState.isChecked());

    const auto actions =
        handler->getActions();
    const auto invokeFromWorker =
        [&] (
            AccessibilityActionType action)
        {
            std::atomic<bool> completed {
                false
            };
            bool invoked = false;
            std::thread worker (
                [&]
                {
                    invoked = actions.invoke (
                        action);
                    completed.store (true);
                });
            while (! completed.load())
            {
                messageManager
                    ->runDispatchLoopUntil (
                        10);
            }
            worker.join();
            return invoked;
        };

    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                toggle));
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_FALSE (
        button->getToggleState());
    EXPECT_FALSE (
        handler->getCurrentState()
            .isChecked());

    button->setClickingTogglesState (
        false);
    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                toggle));
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_FALSE (
        button->getToggleState());
    auto nonToggleHandler =
        button->createAccessibilityHandler();
    ASSERT_NE (nonToggleHandler, nullptr);
    EXPECT_TRUE (
        nonToggleHandler->getActions()
            .contains (
                AccessibilityActionType::
                    press));
    EXPECT_FALSE (
        nonToggleHandler->getActions()
            .contains (
                AccessibilityActionType::
                    toggle));
    EXPECT_FALSE (
        nonToggleHandler->getCurrentState()
            .isCheckable());
    nonToggleHandler.reset();
    button->setClickingTogglesState (
        true);
    button->refreshAccessibilityState();

    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                press));
    for (int attempt = 0;
         attempt < 20
             && listener
                    .callbackCount
                    .load()
                 == 1;
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_EQ (
        listener.callbackCount.load(),
        2);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_TRUE (
        button->getToggleState());

    button->setRadioGroupId (
        42,
        dontSendNotification);
    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                toggle));
    EXPECT_EQ (
        listener.callbackCount.load(),
        2);
    EXPECT_TRUE (
        button->getToggleState());
    button->setRadioGroupId (
        0,
        dontSendNotification);
    button->refreshAccessibilityState();

    button->setChannelNum (12);
    std::thread updatedReader (
        [&]
        {
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
            workerTitle =
                handler->getTitle();
        });
    updatedReader.join();
    EXPECT_EQ (workerValue, "12");
    EXPECT_EQ (
        workerTitle,
        "Probe electrode 4");

    button->setEnabled (false);
    EXPECT_FALSE (handler->isEnabled());
    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                toggle));
    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                press));
    messageManager->runDispatchLoopUntil (
        20);
    EXPECT_EQ (
        listener.callbackCount.load(),
        2);
    EXPECT_TRUE (
        button->getToggleState());

    handler.reset();
    button.reset();
    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                toggle));
    EXPECT_TRUE (
        invokeFromWorker (
            AccessibilityActionType::
                press));
    messageManager->runDispatchLoopUntil (
        20);
    EXPECT_EQ (
        listener.callbackCount.load(),
        2);
}

TEST (GenericEditorAccessibilityTests,
      ElectrodeRadioButtonsUsePressSelection)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());
    auto* messageManager =
        MessageManager::getInstance();
    MessageManagerLock lock;
    Component parent;
    auto first =
        std::make_unique<
            InspectableElectrodeButton> (
            1);
    auto second =
        std::make_unique<
            InspectableElectrodeButton> (
            2);
    parent.addAndMakeVisible (first.get());
    parent.addAndMakeVisible (
        second.get());
    first->setRadioGroupId (
        42,
        dontSendNotification);
    second->setRadioGroupId (
        42,
        dontSendNotification);
    second->setToggleState (
        false,
        dontSendNotification);
    first->setToggleState (
        true,
        dontSendNotification);
    ThreadTrackingButtonListener listener;
    second->addListener (&listener);

    auto firstHandler =
        first->createAccessibilityHandler();
    auto secondHandler =
        second->createAccessibilityHandler();
    ASSERT_NE (firstHandler, nullptr);
    ASSERT_NE (secondHandler, nullptr);
    EXPECT_EQ (
        firstHandler->getRole(),
        AccessibilityRole::radioButton);
    EXPECT_EQ (
        secondHandler->getRole(),
        AccessibilityRole::radioButton);
    EXPECT_TRUE (
        secondHandler->getActions()
            .contains (
                AccessibilityActionType::
                    press));
    EXPECT_FALSE (
        secondHandler->getActions()
            .contains (
                AccessibilityActionType::
                    toggle));

    const auto actions =
        secondHandler->getActions();
    std::atomic<bool> completed { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked = actions.invoke (
                AccessibilityActionType::
                    press);
            completed.store (true);
        });
    while (! completed.load())
    {
        messageManager->runDispatchLoopUntil (
            10);
    }
    worker.join();
    EXPECT_TRUE (invoked);
    for (int attempt = 0;
         attempt < 20
             && listener
                    .callbackCount
                    .load()
                 == 0;
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (
            10);
    }

    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_FALSE (
        first->getToggleState());
    EXPECT_TRUE (
        second->getToggleState());
    EXPECT_FALSE (
        firstHandler->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        secondHandler->getCurrentState()
            .isChecked());
}

TEST (GenericEditorAccessibilityTests, PublishesUtilityButtonLabelAsValue)
{
    InspectableUtilityButton button ("16");
    button.setTitle ("Headstage channel count");
    button.setClickingTogglesState (true);

    auto handler = button.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::button);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::toggle));
    ASSERT_NE (handler->getValueInterface(), nullptr);
    EXPECT_TRUE (handler->getValueInterface()->isReadOnly());
    EXPECT_EQ (
        handler->getValueInterface()->getCurrentValueAsString(),
        "16");

    button.setLabel ("32");

    EXPECT_EQ (
        handler->getValueInterface()->getCurrentValueAsString(),
        "32");

    EXPECT_FALSE (button.getToggleState());
    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::toggle));
    EXPECT_TRUE (button.getToggleState());
}

TEST (GenericEditorAccessibilityTests, PreservesUtilityButtonRadioSemantics)
{
    InspectableUtilityButton button ("Data");
    button.setClickingTogglesState (true);
    button.setRadioGroupId (100, dontSendNotification);

    auto handler = button.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::radioButton);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::toggle));
}

TEST (GenericEditorAccessibilityTests, RefreshesUtilityButtonFallbackName)
{
    InspectableUtilityButton button ("16");
    auto handler = button.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getTitle(), "16");

    button.setLabel ("32");

    EXPECT_EQ (handler->getTitle(), "32");
}

TEST (GenericEditorAccessibilityTests, ExposesProcessorDrawerAsStableToggle)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());
    auto* messageManager =
        MessageManager::getInstance();
    MessageManagerLock lock;
    auto drawer = std::make_unique<DrawerButton> (
        "File Reader (100) Drawer Button",
        "oe.processor.100.drawer",
        "File Reader controls",
        "Show or hide controls for File Reader processor 100.");
    ThreadTrackingButtonListener listener;
    drawer->addListener (&listener);

    EXPECT_EQ (
        drawer->getComponentID(),
        "oe.processor.100.drawer");
    EXPECT_EQ (
        drawer->getTitle(),
        "File Reader controls");
    EXPECT_EQ (
        drawer->getDescription(),
        "Show or hide controls for File Reader processor 100.");
    EXPECT_TRUE (drawer->isAccessible());
    EXPECT_TRUE (
        drawer->getClickingTogglesState());

    auto handler =
        drawer->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::toggleButton);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::toggle));
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::showMenu));
    EXPECT_FALSE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler->getValueInterface()
            ->isReadOnly());
    EXPECT_FALSE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isExpandable());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isCollapsed());
    EXPECT_FALSE (
        handler->getCurrentState()
            .isExpanded());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Off");

    std::atomic<bool> workerEntered {
        false
    };
    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            workerEntered.store (true);
            invoked.store (
                handler->getActions().invoke (
                    AccessibilityActionType::
                        toggle));
        });
    while (! workerEntered.load())
        std::this_thread::yield();
    for (int attempt = 0;
         attempt < 20
             && ! invoked.load();
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (
                10);
    }
    worker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_TRUE (
        drawer->getToggleState());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isExpandable());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isExpanded());
    EXPECT_FALSE (
        handler->getCurrentState()
            .isCollapsed());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "On");

    listener.callbackCount.store (0);
    listener
        .callbackUsedMessageThread
        .store (false);
    drawer->setEnabled (false);
    workerEntered.store (false);
    invoked.store (false);
    std::thread disabledWorker (
        [&]
        {
            workerEntered.store (true);
            invoked.store (
                handler->getActions().invoke (
                    AccessibilityActionType::
                        toggle));
        });
    while (! workerEntered.load())
        std::this_thread::yield();
    for (int attempt = 0;
         attempt < 20
             && ! invoked.load();
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (
                10);
    }
    disabledWorker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);
    EXPECT_TRUE (
        drawer->getToggleState());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isChecked());

    const auto staleActions =
        handler->getActions();
    handler.reset();
    drawer.reset();
    EXPECT_TRUE (
        staleActions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);
}

TEST (GenericEditorAccessibilityTests, ExposesStreamSelectorNavigationControls)
{
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();

    EXPECT_EQ (selector.getComponentID(), "oe.processor.100.streams");
    EXPECT_EQ (selector.getTitle(), "Test Processor data streams");
    EXPECT_EQ (selector.getDescription(),
               "Select and inspect data streams for Test Processor (node 100).");

    auto* table = findDescendantBySemanticId (
        selector,
        "oe.processor.100.streams.table");
    ASSERT_NE (table, nullptr);
    EXPECT_EQ (table->getTitle(), "Available data streams");
    EXPECT_EQ (table->getDescription(),
               "Select the data stream shown by Test Processor (node 100).");

    auto* expand = findDescendantBySemanticId (
        selector,
        "oe.processor.100.streams.expand");
    ASSERT_NE (expand, nullptr);
    EXPECT_EQ (expand->getTitle(), "Expand data streams");
    EXPECT_EQ (expand->getDescription(),
               "Open the full data stream table for Test Processor (node 100).");
    auto expandHandler = expand->createAccessibilityHandler();
    ASSERT_NE (expandHandler, nullptr);
    EXPECT_EQ (expandHandler->getRole(), AccessibilityRole::button);
}

TEST (GenericEditorAccessibilityTests, ExposesUniqueNamedStreamRows)
{
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();
    TestDataStream first ({ "Probe AP",
                            "First Neuropixels action-potential stream",
                            "probe.ap",
                            30000.0f,
                            true },
                          101);
    TestDataStream second ({ "Probe AP",
                             "Second Neuropixels action-potential stream",
                             "probe.ap",
                             30000.0f,
                             true },
                           102);

    selector.add (&first);
    selector.add (&second);
    selector.finishedUpdate();

    auto* table = dynamic_cast<TableListBox*> (
        findDescendantBySemanticId (
            selector,
            "oe.processor.100.streams.table"));
    ASSERT_NE (table, nullptr);
    auto* firstRow = table->getComponentForRowNumber (0);
    auto* secondRow = table->getComponentForRowNumber (1);
    ASSERT_NE (firstRow, nullptr);
    ASSERT_NE (secondRow, nullptr);

    EXPECT_EQ (
        firstRow->getComponentID(),
        "oe.processor.100.streams.table.source_101.stream_probe_ap");
    EXPECT_EQ (
        secondRow->getComponentID(),
        "oe.processor.100.streams.table.source_102.stream_probe_ap");

    auto firstHandler = firstRow->createAccessibilityHandler();
    auto secondHandler = secondRow->createAccessibilityHandler();
    ASSERT_NE (firstHandler, nullptr);
    ASSERT_NE (secondHandler, nullptr);
    EXPECT_EQ (firstHandler->getTitle(), "Probe AP (source 101)");
    EXPECT_EQ (secondHandler->getTitle(), "Probe AP (source 102)");
}

TEST (GenericEditorAccessibilityTests,
      PublishesCurrentStreamAsReadOnlyValue)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());
    MessageManagerLock lock;
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    editor.setVisible (true);
    editor.addToDesktop (0);
    auto& selector = editor.getStreamSelector();
    TestDataStream first ({ "Probe AP",
                            "Neuropixels action-potential stream",
                            "probe.ap",
                            30000.0f,
                            true },
                          101);
    TestDataStream second ({ "Probe LFP",
                             "Neuropixels local-field-potential stream",
                             "probe.lfp",
                             2500.0f,
                             true },
                           101);

    ASSERT_TRUE (selector.isShowing());

    auto* handler =
        selector.getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler->getValueInterface()
            ->isReadOnly());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "No data streams");

    selector.add (&first);
    selector.add (&second);
    selector.finishedUpdate();
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Probe AP (source 101)");

    auto* table = dynamic_cast<TableListBox*> (
        findDescendantBySemanticId (
            selector,
            "oe.processor.100.streams.table"));
    ASSERT_NE (table, nullptr);
    auto* secondRow =
        table->getComponentForRowNumber (1);
    ASSERT_NE (secondRow, nullptr);
    auto rowHandler =
        secondRow->createAccessibilityHandler();
    ASSERT_NE (rowHandler, nullptr);

    EXPECT_TRUE (
        rowHandler->getActions().invoke (
            AccessibilityActionType::focus));
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Probe LFP (source 101)");

    selector.setViewedIndex (0);
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Probe AP (source 101)");

    selector.beginUpdate();
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Probe AP (source 101)");
    String workerValue;
    std::thread worker (
        [&selector,
         &workerValue]
        {
            auto workerHandler =
                selector.createAccessibilityHandler();
            ASSERT_NE (
                workerHandler,
                nullptr);
            ASSERT_NE (
                workerHandler->getValueInterface(),
                nullptr);
            workerValue =
                workerHandler->getValueInterface()
                    ->getCurrentValueAsString();
        });
    worker.join();
    EXPECT_EQ (
        workerValue,
        "Probe AP (source 101)");
    selector.add (&second);
    selector.finishedUpdate();
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Probe LFP (source 101)");
}

TEST (GenericEditorAccessibilityTests,
      ExposesStreamProcessingAsMessageThreadToggle)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());
    auto* messageManager =
        MessageManager::getInstance();
    MessageManagerLock lock;
    auto processorGraph =
        std::make_unique<
            ProcessorGraph> (
            true);
    TestStreamToggleProcessor processor;
    processor.setNodeId (100);
    auto* editor =
        dynamic_cast<GenericEditor*> (
            processor.createEditor());
    ASSERT_NE (editor, nullptr);
    editor->setVisible (true);
    editor->addToDesktop (0);
    auto* selector =
        dynamic_cast<
            StreamSelectorTable*> (
            findDescendantBySemanticId (
                *editor,
                "oe.processor.100.streams"));
    ASSERT_NE (selector, nullptr);
    TestDataStream stream ({ "Probe AP",
                             "Neuropixels action-potential stream",
                             "probe.ap",
                             30000.0f,
                             true },
                           101);
    stream.addProcessor (&processor);
    stream.addParameter (
        new BooleanParameter (
            &stream,
            Parameter::STREAM_SCOPE,
            "enable_stream",
            "Enable",
            "Enable processing for this stream.",
            true));

    selector->add (&stream);
    selector->finishedUpdate();

    auto* table = dynamic_cast<TableListBox*> (
        findDescendantBySemanticId (
            *selector,
            "oe.processor.100.streams.table"));
    ASSERT_NE (table, nullptr);
    const auto getToggle =
        [table]
        {
            return dynamic_cast<Button*> (
                table->getCellComponent (
                    StreamTableModel::Columns::
                        ENABLED,
                    0));
        };
    auto* toggle = getToggle();
    ASSERT_NE (toggle, nullptr);
    EXPECT_EQ (
        toggle->getComponentID(),
        "oe.processor.100.streams.table.source_101.stream_probe_ap.processing_enabled");
    EXPECT_EQ (
        toggle->getTitle(),
        "Probe AP processing enabled");

    auto* handler =
        toggle->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::toggleButton);
    ASSERT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::toggle));
    EXPECT_FALSE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    auto* enableStreamParameter =
        stream.getParameter (
            "enable_stream");
    ASSERT_NE (
        enableStreamParameter,
        nullptr);
    enableStreamParameter->setEnabled (
        false);
    table->updateContent();
    toggle = getToggle();
    ASSERT_NE (toggle, nullptr);
    handler =
        toggle->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_FALSE (
        toggle->isEnabled());
    EXPECT_FALSE (
        handler->isEnabled());

    enableStreamParameter->setEnabled (
        true);
    table->updateContent();
    toggle = getToggle();
    ASSERT_NE (toggle, nullptr);
    handler =
        toggle->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_TRUE (
        toggle->isEnabled());
    EXPECT_TRUE (
        handler->isEnabled());
    EXPECT_TRUE (
        toggle->getAccessibilityHandler()
            ->getCurrentState()
            .isChecked());

    std::atomic<bool> workerEntered { false };
    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            workerEntered.store (true);
            invoked.store (
                handler->getActions().invoke (
                    AccessibilityActionType::toggle));
        });

    while (! workerEntered.load())
        std::this_thread::yield();

    for (int attempt = 0;
         attempt < 20
             && ! invoked.load();
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (10);
    }
    worker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_FALSE (
        static_cast<bool> (
            stream.getParameter ("enable_stream")
                ->getValue()));
    EXPECT_FALSE (
        selector->checkStream (
            &stream));
    toggle = getToggle();
    ASSERT_NE (toggle, nullptr);
    handler =
        toggle->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_FALSE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        processor
            .parameterChangeUsedMessageThread
            .load());

    processorGraph
        ->getUndoManager()
        ->undo();
    EXPECT_TRUE (
        static_cast<bool> (
            stream.getParameter ("enable_stream")
                ->getValue()));
    EXPECT_TRUE (
        getToggle()
            ->getAccessibilityHandler()
            ->getCurrentState()
            .isChecked());

    processorGraph
        ->getUndoManager()
        ->redo();
    EXPECT_FALSE (
        static_cast<bool> (
            stream.getParameter ("enable_stream")
                ->getValue()));
    EXPECT_FALSE (
        getToggle()
            ->getAccessibilityHandler()
            ->getCurrentState()
            .isChecked());

    stream.getParameter (
              "enable_stream")
        ->setNextValue (
            true);
    EXPECT_TRUE (
        getToggle()
            ->getAccessibilityHandler()
            ->getCurrentState()
            .isChecked());

    std::thread externalWorker (
        [selector,
         streamId =
             stream.getStreamId()]
        {
            selector
                ->setStreamEnabledState (
                    streamId,
                    false);
        });
    externalWorker.join();
    EXPECT_TRUE (
        selector->checkStream (
            &stream));

    for (int attempt = 0;
         attempt < 20
             && selector->checkStream (
                    &stream);
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (
            10);
    }
    EXPECT_FALSE (
        selector->checkStream (
            &stream));
    EXPECT_FALSE (
        getToggle()
            ->getAccessibilityHandler()
            ->getCurrentState()
            .isChecked());
}

TEST (GenericEditorAccessibilityTests,
      StaleStreamToggleCannotFollowAReusedCell)
{
    ScopedGenericEditorTestApplication application;
    ASSERT_TRUE (application.wasInitialised());
    auto* messageManager =
        MessageManager::getInstance();
    MessageManagerLock lock;
    auto processorGraph =
        std::make_unique<
            ProcessorGraph> (
            true);
    TestStreamToggleProcessor processor;
    processor.setNodeId (100);
    auto* editor =
        dynamic_cast<GenericEditor*> (
            processor.createEditor());
    ASSERT_NE (editor, nullptr);
    editor->setVisible (true);
    editor->addToDesktop (0);
    auto* selector =
        dynamic_cast<
            StreamSelectorTable*> (
            findDescendantBySemanticId (
                *editor,
                "oe.processor.100.streams"));
    ASSERT_NE (selector, nullptr);
    TestDataStream first ({ "Probe AP",
                            "First probe stream",
                            "probe.ap",
                            30000.0f,
                            true },
                          101);
    TestDataStream second ({ "Probe LFP",
                             "Second probe stream",
                             "probe.lfp",
                             2500.0f,
                             true },
                           102);
    for (auto* stream :
         { &first,
           &second })
    {
        stream->addProcessor (
            &processor);
        stream->addParameter (
            new BooleanParameter (
                stream,
                Parameter::STREAM_SCOPE,
                "enable_stream",
                "Enable",
                "Enable processing for this stream.",
                true));
    }

    selector->add (&first);
    selector->finishedUpdate();
    auto* table = dynamic_cast<TableListBox*> (
        findDescendantBySemanticId (
            *selector,
            "oe.processor.100.streams.table"));
    ASSERT_NE (table, nullptr);
    auto* originalToggle =
        table->getCellComponent (
            StreamTableModel::Columns::ENABLED,
            0);
    ASSERT_NE (originalToggle, nullptr);
    auto* originalHandler =
        originalToggle
            ->getAccessibilityHandler();
    ASSERT_NE (originalHandler, nullptr);
    auto staleActions =
        originalHandler->getActions();

    selector->beginUpdate();
    selector->add (&second);
    selector->finishedUpdate();
    auto* reusedToggle =
        table->getCellComponent (
            StreamTableModel::Columns::ENABLED,
            0);
    ASSERT_EQ (
        reusedToggle,
        originalToggle);
    EXPECT_EQ (
        reusedToggle->getComponentID(),
        "oe.processor.100.streams.table.source_102.stream_probe_lfp.processing_enabled");

    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            invoked.store (
                staleActions.invoke (
                    AccessibilityActionType::toggle));
        });
    for (int attempt = 0;
         attempt < 20
             && ! invoked.load();
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (
            10);
    }
    worker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_TRUE (
        static_cast<bool> (
            second.getParameter (
                      "enable_stream")
                ->getValue()));
    EXPECT_TRUE (
        selector->checkStream (
            &second));

    auto queuedAfterDestruction =
        reusedToggle
            ->getAccessibilityHandler()
            ->getActions();
    selector->beginUpdate();
    selector->finishedUpdate();
    EXPECT_EQ (
        table->getCellComponent (
            StreamTableModel::Columns::ENABLED,
            0),
        nullptr);

    invoked.store (false);
    std::thread teardownWorker (
        [&]
        {
            invoked.store (
                queuedAfterDestruction.invoke (
                    AccessibilityActionType::toggle));
        });
    for (int attempt = 0;
         attempt < 20
             && ! invoked.load();
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (
            10);
    }
    teardownWorker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_TRUE (
        static_cast<bool> (
            second.getParameter (
                      "enable_stream")
                ->getValue()));
}

TEST (GenericEditorAccessibilityTests,
      OmitsStreamProcessingToggleForNonFilters)
{
    MessageManager::getInstance();
    MessageManagerLock lock;
    TestStreamProcessor processor (
        100,
        Plugin::Processor::SOURCE);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();
    TestDataStream stream ({ "Probe AP",
                             "Neuropixels action-potential stream",
                             "probe.ap",
                             30000.0f,
                             true },
                           101);

    selector.add (&stream);
    selector.finishedUpdate();

    auto* table = dynamic_cast<TableListBox*> (
        findDescendantBySemanticId (
            selector,
            "oe.processor.100.streams.table"));
    ASSERT_NE (table, nullptr);
    EXPECT_EQ (
        table->getCellComponent (
            StreamTableModel::Columns::ENABLED,
            0),
        nullptr);
}

TEST (GenericEditorAccessibilityTests, SelectingStreamRowUpdatesEditor)
{
    MessageManager::getInstance();
    MessageManagerLock lock;
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();
    TestDataStream first ({ "Probe AP",
                            "Neuropixels action-potential stream",
                            "probe.ap",
                            30000.0f,
                            true },
                          101);
    TestDataStream second ({ "Probe LFP",
                             "Neuropixels local-field-potential stream",
                             "probe.lfp",
                             2500.0f,
                             true },
                           101);

    selector.add (&first);
    selector.add (&second);
    selector.finishedUpdate();

    auto* table = dynamic_cast<TableListBox*> (
        findDescendantBySemanticId (
            selector,
            "oe.processor.100.streams.table"));
    ASSERT_NE (table, nullptr);
    auto* secondRow = table->getComponentForRowNumber (1);
    ASSERT_NE (secondRow, nullptr);
    auto handler = secondRow->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::focus));
    EXPECT_EQ (selector.getViewedIndex(), 1);
    EXPECT_EQ (editor.getCurrentStream(), second.getStreamId());
}

TEST (GenericEditorAccessibilityTests, ModelSelectionUpdatesOnMessageThread)
{
    auto* messageManager = MessageManager::getInstance();
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();
    TestDataStream first ({ "Probe AP",
                            "Neuropixels action-potential stream",
                            "probe.ap",
                            30000.0f,
                            true },
                          101);
    TestDataStream second ({ "Probe LFP",
                             "Neuropixels local-field-potential stream",
                             "probe.lfp",
                             2500.0f,
                             true },
                           101);

    selector.add (&first);
    selector.add (&second);
    selector.finishedUpdate();

    auto* table = dynamic_cast<TableListBox*> (
        findDescendantBySemanticId (
            selector,
            "oe.processor.100.streams.table"));
    ASSERT_NE (table, nullptr);
    auto* model = table->getTableListBoxModel();
    ASSERT_NE (model, nullptr);

    std::thread worker (
        [model]
        {
            model->selectedRowsChanged (1);
        });
    worker.join();

    EXPECT_FALSE (editor.selectedStreamChangeRan.load());

    for (int attempt = 0;
         attempt < 20
             && ! editor.selectedStreamChangeRan.load();
         ++attempt)
    {
        messageManager->runDispatchLoopUntil (10);
    }

    EXPECT_TRUE (editor.selectedStreamChangeRan.load());
    EXPECT_TRUE (
        editor.selectedStreamChangeUsedMessageThread.load());
    EXPECT_EQ (selector.getViewedIndex(), 1);
    EXPECT_EQ (editor.getCurrentStream(), second.getStreamId());
}

TEST (GenericEditorAccessibilityTests, ScopesStreamStatusControlsToProcessorAndStream)
{
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();
    TestDataStream stream ({ "Probe AP",
                             "Neuropixels action-potential stream",
                             "probe.ap",
                             30000.0f,
                             true },
                           101);

    selector.add (&stream);
    selector.finishedUpdate();

    auto* ttlMonitor = selector.getTTLMonitor (&stream);
    ASSERT_NE (ttlMonitor, nullptr);

    const String expectedId =
        "oe.processor.100.streams.source_101.stream_probe_ap.ttl_lines";
    EXPECT_EQ (ttlMonitor->getComponentID(), expectedId);
    EXPECT_EQ (ttlMonitor->getTitle(), "Probe AP TTL line states");
    EXPECT_EQ (ttlMonitor->getDescription(),
               "Current digital event state for Probe AP.");
    EXPECT_EQ (ttlMonitor->getChildComponent (0)->getComponentID(),
               expectedId + ".line_1");

    auto* delayMonitor = selector.getDelayMonitor (&stream);
    ASSERT_NE (delayMonitor, nullptr);
    EXPECT_EQ (
        delayMonitor->getComponentID(),
        "oe.processor.100.streams.source_101.stream_probe_ap.processing_delay");
    EXPECT_EQ (delayMonitor->getTitle(), "Probe AP processing delay");
    EXPECT_EQ (delayMonitor->getDescription(),
               "Processing delay for Probe AP.");

    auto delayHandler = delayMonitor->createAccessibilityHandler();
    ASSERT_NE (delayHandler, nullptr);
    ASSERT_NE (delayHandler->getValueInterface(), nullptr);
    EXPECT_EQ (
        delayHandler->getValueInterface()->getCurrentValueAsString(),
        "0.00 ms");
}
