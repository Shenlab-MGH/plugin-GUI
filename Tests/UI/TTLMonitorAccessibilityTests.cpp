#include "../../Source/Processors/Editors/TTLMonitor.h"
#include "gtest/gtest.h"

TEST (TTLMonitorAccessibilityTests, PublishesEveryLineAsReadOnlyState)
{
    TTLMonitor monitor (8, 4);

    EXPECT_EQ (monitor.getComponentID(), "oe.status.ttl_lines");
    EXPECT_EQ (monitor.getTitle(), "TTL line states");
    EXPECT_EQ (monitor.getDescription(),
               "Current digital event state for each TTL line.");
    EXPECT_TRUE (monitor.isAccessible());

    ASSERT_EQ (monitor.getNumChildComponents(), 4);

    for (int index = 0; index < monitor.getNumChildComponents(); ++index)
    {
        auto* line = monitor.getChildComponent (index);
        ASSERT_NE (line, nullptr);

        const auto lineNumber = index + 1;
        EXPECT_EQ (line->getComponentID(),
                   "oe.status.ttl_lines.line_" + String (lineNumber));
        EXPECT_EQ (line->getTitle(), "TTL line " + String (lineNumber));
        EXPECT_EQ (line->getDescription(),
                   "Current state of TTL line " + String (lineNumber) + ".");
        EXPECT_TRUE (line->isAccessible());

        auto handler = line->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (handler->getRole(), AccessibilityRole::staticText);

        auto* value = handler->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_TRUE (value->isReadOnly());
        EXPECT_EQ (value->getCurrentValueAsString(), "inactive");
    }

    auto* thirdLine = monitor.getChildComponent (2);
    auto thirdLineHandler = thirdLine->createAccessibilityHandler();
    auto* thirdLineValue = thirdLineHandler->getValueInterface();

    monitor.setState (2, true);

    EXPECT_EQ (thirdLineValue->getCurrentValueAsString(), "active");
}
