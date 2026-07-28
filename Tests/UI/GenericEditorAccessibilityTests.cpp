#include "../../Source/Processors/Editors/GenericEditor.h"
#include "../../Source/Processors/Editors/ElectrodeButtons.h"
#include "../../Source/Processors/Editors/StreamSelector.h"
#include "../../Source/Processors/GenericProcessor/GenericProcessor.h"
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
    DrawerButton drawer ("File Reader (100) Drawer Button",
                         "oe.processor.100.drawer",
                         "File Reader controls",
                         "Show or hide controls for File Reader processor 100.");

    EXPECT_EQ (drawer.getComponentID(), "oe.processor.100.drawer");
    EXPECT_EQ (drawer.getTitle(), "File Reader controls");
    EXPECT_EQ (drawer.getDescription(), "Show or hide controls for File Reader processor 100.");
    EXPECT_TRUE (drawer.isAccessible());
    EXPECT_TRUE (drawer.getClickingTogglesState());
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
