#include "../../Source/UI/ProcessorList.h"
#include "gtest/gtest.h"

TEST (ProcessorListAccessibilityTests, ExposesListAndSearchControls)
{
    MessageManager::getInstance();
    MessageManagerLock lock;
    Viewport viewport;
    ProcessorList processorList (&viewport);

    auto* expand = processorList.findChildWithID ("oe.processor_list.expand");
    ASSERT_NE (expand, nullptr);
    EXPECT_EQ (expand->getTitle(), "Available processors");
    EXPECT_EQ (expand->getDescription(), "Show or hide the available processor list.");
    EXPECT_TRUE (expand->isAccessible());
    auto expandHandler = expand->createAccessibilityHandler();
    ASSERT_NE (expandHandler, nullptr);
    EXPECT_TRUE (expandHandler->getCurrentState().isCheckable());
    EXPECT_TRUE (expandHandler->getActions().contains (AccessibilityActionType::toggle));

    auto* search = processorList.findChildWithID ("oe.processor_list.search.open");
    ASSERT_NE (search, nullptr);
    EXPECT_EQ (search->getTitle(), "Search processors");
    EXPECT_EQ (search->getDescription(), "Open the processor search field.");
    EXPECT_TRUE (search->isAccessible());

    auto* query = processorList.findChildWithID ("oe.processor_list.search.query");
    ASSERT_NE (query, nullptr);
    EXPECT_EQ (query->getTitle(), "Processor search");
    EXPECT_EQ (query->getDescription(), "Filter available processors by name.");
    EXPECT_TRUE (query->isAccessible());
}

TEST (ProcessorListAccessibilityTests, ExposesProcessorCategoryIds)
{
    MessageManager::getInstance();
    MessageManagerLock lock;
    Viewport viewport;
    ProcessorList processorList (&viewport);
    processorList.resized();

    const StringArray categories {
        "sources",
        "filters",
        "sinks",
        "utilities",
        "recording"
    };

    for (const auto& category : categories)
    {
        auto* component = processorList.findChildWithID (
            "oe.processor_list.category." + category);
        ASSERT_NE (component, nullptr) << category;
        EXPECT_TRUE (component->isAccessible());
        EXPECT_TRUE (component->getTitle().isNotEmpty());
        EXPECT_TRUE (component->getDescription().isNotEmpty());
        EXPECT_EQ (component->getComponentID(), "oe.processor_list.category." + category);
    }
}
