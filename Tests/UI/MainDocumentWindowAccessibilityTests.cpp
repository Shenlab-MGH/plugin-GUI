#include "../../Source/MainWindow.h"
#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"

namespace
{
class AccessibilityTestApplication final : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Open Ephys UIA Test"; }
    const String getApplicationVersion() override { return "1.0.2"; }
    void initialise (const String&) override {}
    void shutdown() override {}
};

JUCEApplicationBase* createAccessibilityTestApplication()
{
    return new AccessibilityTestApplication();
}

class TestMenuModel final : public MenuBarModel
{
public:
    StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "View", "Help" };
    }

    PopupMenu getMenuForIndex (int, const String&) override
    {
        return {};
    }

    void menuItemSelected (int, int) override {}
};

class MainDocumentWindowAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        JUCEApplicationBase::createInstance = createAccessibilityTestApplication;
        application = std::make_unique<AccessibilityTestApplication>();
        MessageManager::getInstance();
        messageManagerLock = std::make_unique<MessageManagerLock>();
    }

    void TearDown() override
    {
        messageManagerLock.reset();
        application.reset();
        JUCEApplicationBase::createInstance = nullptr;
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<AccessibilityTestApplication> application;
    std::unique_ptr<MessageManagerLock> messageManagerLock;
};
} // namespace

TEST_F (MainDocumentWindowAccessibilityTests, ExposesTheWindowAndItsContentToAccessibilityClients)
{
    MainDocumentWindow window;
    TextButton contentButton ("Accessible child");

    window.setContentNonOwned (&contentButton, false);
    window.addToDesktop();

    EXPECT_EQ (window.getComponentID(), "oe.window.main");
    EXPECT_TRUE (window.isAccessible());
    EXPECT_TRUE (contentButton.isAccessible());
    EXPECT_NE (window.getAccessibilityHandler(), nullptr);
    EXPECT_NE (contentButton.getAccessibilityHandler(), nullptr);
}

TEST_F (MainDocumentWindowAccessibilityTests, ExposesTheApplicationMenuBar)
{
    TestMenuModel model;
    ApplicationMenuBarComponent menuBar (&model);
    menuBar.addToDesktop (0);

    EXPECT_EQ (menuBar.getComponentID(), "oe.menu.main");
    EXPECT_EQ (menuBar.getTitle(), "Application menu");
    EXPECT_EQ (menuBar.getDescription(),
               "Open application commands and settings.");
    EXPECT_TRUE (menuBar.isAccessible());
    EXPECT_TRUE (menuBar.isFocusContainer());

    auto* handler = menuBar.getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::menuBar);
    EXPECT_FALSE (handler->isIgnored());

    const StringArray expectedIds {
        "oe.menu.file",
        "oe.menu.edit",
        "oe.menu.view",
        "oe.menu.help"
    };

    ASSERT_EQ (menuBar.getNumChildComponents(), expectedIds.size());
    for (int index = 0; index < expectedIds.size(); ++index)
        EXPECT_EQ (menuBar.getChildComponent (index)->getComponentID(),
                   expectedIds[index]);
}

TEST_F (MainDocumentWindowAccessibilityTests, KeepsDisabledSemanticControlsDiscoverable)
{
    Component content;
    TextButton unavailable ("Unavailable action");

    content.setSize (300, 200);
    content.setFocusContainerType (Component::FocusContainerType::focusContainer);
    content.addAndMakeVisible (unavailable);
    unavailable.setBounds (10, 10, 120, 30);
    unavailable.setEnabled (false);
    applySemanticMetadata (unavailable,
                           "oe.test.unavailable",
                           "Unavailable action",
                           "Action exists but is not currently available.");
    content.addToDesktop (0);

    auto* contentHandler = content.getAccessibilityHandler();
    ASSERT_NE (contentHandler, nullptr);

    const auto children = contentHandler->getChildren();
    const auto unavailableHandler = std::find_if (
        children.begin(),
        children.end(),
        [] (const AccessibilityHandler* handler)
        {
            return handler->getComponent().getComponentID()
                   == "oe.test.unavailable";
        });

    ASSERT_NE (unavailableHandler, children.end());
    EXPECT_FALSE ((*unavailableHandler)->isEnabled());
}
