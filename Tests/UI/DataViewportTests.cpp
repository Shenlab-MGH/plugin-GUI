#include "../../Source/UI/DataViewport.h"
#include "../../Source/UI/InfoLabel.h"
#include "gtest/gtest.h"

class DataViewportTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();

        customLookAndFeel = std::make_unique<CustomLookAndFeel>();
        LookAndFeel::setDefaultLookAndFeel (customLookAndFeel.get());

        // Holds a lock for the test; necessary to prevent a bunch of assertion failures in debug
        lock = std::make_unique<MessageManagerLock>();
        viewport = std::make_unique<DataViewport>();
    }

    void TearDown() override
    {
        viewport = nullptr;
        lock.reset();

        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<DataViewport> viewport;
    std::unique_ptr<MessageManagerLock> lock;

    std::unique_ptr<CustomLookAndFeel> customLookAndFeel;
};

TEST_F (DataViewportTests, TestAddTabsAndSwitchTabs)
{
    auto info_label = std::make_unique<InfoLabel>();
    int node_id = 0;
    viewport->addTab ("Info", info_label.get(), 0);
    //ASSERT_EQ(viewport->getNumTabs(), 1);
    //ASSERT_EQ(viewport->getTabNames().size(), 1);
    //ASSERT_EQ(viewport->getTabNames()[0], juce::String("Info"));
    //ASSERT_EQ(viewport->getCurrentTabIndex(), 0);

    //auto info_label2 = std::make_unique<InfoLabel>();
    //viewport->addTab("Info2", info_label.get());
    //ASSERT_EQ(viewport->getNumTabs(), 2);
    //ASSERT_EQ(viewport->getTabNames().size(), 2);
    //ASSERT_EQ(viewport->getTabNames()[0], juce::String("Info"));
    //ASSERT_EQ(viewport->getTabNames()[1], juce::String("Info2"));

    //ASSERT_EQ(viewport->getCurrentTabIndex(), 1);
    //viewport->setCurrentTabIndex(0);
    //ASSERT_EQ(viewport->getCurrentTabIndex(), 0);
    //viewport->setCurrentTabIndex(1);
    //ASSERT_EQ(viewport->getCurrentTabIndex(), 1);
}

TEST_F (DataViewportTests, ExposesViewTabsAndLayoutActions)
{
    DraggableTabComponent tabs (viewport.get());
    CustomTabButton infoTab ("Info", &tabs, 0);
    EXPECT_EQ (infoTab.getComponentID(), "oe.view.tab.0.select");
    EXPECT_EQ (infoTab.getTitle(), "Info tab");
    EXPECT_EQ (infoTab.getDescription(), "Select the Info view tab.");
    EXPECT_TRUE (infoTab.isAccessible());

    CloseTabButton closeInfo (0, "Info");
    EXPECT_EQ (closeInfo.getComponentID(), "oe.view.tab.0.close");
    EXPECT_EQ (closeInfo.getTitle(), "Close Info tab");
    EXPECT_EQ (closeInfo.getDescription(), "Close the Info view tab.");
    EXPECT_TRUE (closeInfo.isAccessible());

    AddTabbedComponentButton addColumn;
    EXPECT_EQ (addColumn.getComponentID(), "oe.view.column.add");
    EXPECT_EQ (addColumn.getTitle(), "Add view column");
    EXPECT_EQ (addColumn.getDescription(), "Add another tabbed view column.");
    EXPECT_TRUE (addColumn.isAccessible());
}
