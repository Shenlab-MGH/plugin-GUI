#include "../../Source/MainWindow.h"
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

class TestMainDocumentWindow final : public MainDocumentWindow
{
public:
    using MainDocumentWindow::createAccessibilityHandler;
};

class TestTextButton final : public TextButton
{
public:
    using TextButton::TextButton;
    using TextButton::createAccessibilityHandler;
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
    TestMainDocumentWindow window;
    TestTextButton contentButton ("Accessible child");

    window.setContentNonOwned (&contentButton, false);

    EXPECT_EQ (window.getComponentID(), "oe.window.main");
    EXPECT_TRUE (window.isAccessible());
    EXPECT_TRUE (contentButton.isAccessible());

    auto windowHandler = window.createAccessibilityHandler();
    ASSERT_NE (windowHandler, nullptr);
    EXPECT_EQ (&windowHandler->getComponent(), &window);

    auto contentHandler = contentButton.createAccessibilityHandler();
    ASSERT_NE (contentHandler, nullptr);
    EXPECT_EQ (&contentHandler->getComponent(), &contentButton);
    EXPECT_EQ (contentHandler->getRole(), AccessibilityRole::button);

   #if JUCE_WINDOWS
    window.addToDesktop();
    ASSERT_NE (window.getWindowHandle(), nullptr);
    EXPECT_NE (window.getAccessibilityHandler(), nullptr);
    EXPECT_NE (contentButton.getAccessibilityHandler(), nullptr);
   #endif
}
