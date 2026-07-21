#include "../../Source/Processors/Editors/GenericEditor.h"
#include "gtest/gtest.h"

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
