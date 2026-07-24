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

        auto handler = component->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_TRUE (handler->getCurrentState().isExpandable());
        EXPECT_TRUE (handler->getCurrentState().isExpanded());
        EXPECT_TRUE (handler->getActions().contains (AccessibilityActionType::showMenu));
    }
}

TEST (ProcessorListAccessibilityTests, GivesProcessorCatalogItemsStableSelectableSemantics)
{
    MessageManager::getInstance();
    MessageManagerLock lock;
    Viewport viewport;
    ProcessorList processorList (&viewport);
    ProcessorListItem item ("File Reader", 0, Plugin::BUILT_IN, Plugin::Processor::SOURCE);
    item.setParentName ("Sources");

    EXPECT_EQ (createProcessorCatalogAutomationId ("File Reader", 1),
               "oe.processor_catalog.file_reader");
    EXPECT_EQ (createProcessorCatalogAutomationId ("File Reader", 2),
               "oe.processor_catalog.file_reader.2");

    configureProcessorCatalogItemAccessibility (
        item,
        processorList,
        createProcessorCatalogAutomationId ("File Reader", 1));

    EXPECT_EQ (item.getComponentID(), "oe.processor_catalog.file_reader");
    EXPECT_EQ (item.getTitle(), "File Reader");
    EXPECT_TRUE (item.getDescription().containsIgnoreCase ("processor"));

    auto handler = item.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::treeItem);
    EXPECT_TRUE (handler->getCurrentState().isSelectable());
    EXPECT_TRUE (handler->getActions().contains (AccessibilityActionType::press));

    EXPECT_TRUE (handler->getActions().invoke (AccessibilityActionType::press));
    EXPECT_TRUE (item.isSelected());
}
