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
