#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"

TEST (SemanticComponentTests, ValidatesStableSemanticIds)
{
    EXPECT_TRUE (isValidSemanticId ("oe.window.main"));
    EXPECT_TRUE (isValidSemanticId ("oe.processor.100.parameter.start_time"));

    EXPECT_FALSE (isValidSemanticId (""));
    EXPECT_FALSE (isValidSemanticId ("oe"));
    EXPECT_FALSE (isValidSemanticId ("oe."));
    EXPECT_FALSE (isValidSemanticId ("OE.window.main"));
    EXPECT_FALSE (isValidSemanticId ("oe.window.Main"));
    EXPECT_FALSE (isValidSemanticId ("oe.window.main-button"));
    EXPECT_FALSE (isValidSemanticId ("oe.window..main"));
}

TEST (SemanticComponentTests, SanitisesDynamicIdSegments)
{
    EXPECT_EQ (sanitiseSemanticSegment ("Record Options"), "record_options");
    EXPECT_EQ (sanitiseSemanticSegment (" CH1 vs CH2 "), "ch1_vs_ch2");
    EXPECT_EQ (sanitiseSemanticSegment ("already_valid"), "already_valid");
    EXPECT_EQ (sanitiseSemanticSegment ("---"), "unnamed");
}

TEST (SemanticComponentTests, AppliesStableAccessibleMeaning)
{
    TextButton button ("R");

    applySemanticMetadata (button,
                           "oe.control.record_options",
                           "Recording options",
                           "Configure recording engine and directory behavior.",
                           "Opens the recording options panel.");

    EXPECT_EQ (button.getComponentID(), "oe.control.record_options");
    EXPECT_EQ (button.getTitle(), "Recording options");
    EXPECT_EQ (button.getDescription(), "Configure recording engine and directory behavior.");
    EXPECT_EQ (button.getHelpText(), "Opens the recording options panel.");
    EXPECT_TRUE (button.isAccessible());
}

TEST (SemanticComponentTests, IgnoresInvalidSemanticIds)
{
    TextButton button ("R");
    button.setComponentID ("existing.id");
    button.setTitle ("Existing title");

    applySemanticMetadata (button, "invalid-id", "New title", "New description");

    EXPECT_EQ (button.getComponentID(), "existing.id");
    EXPECT_EQ (button.getTitle(), "Existing title");
    EXPECT_TRUE (button.getDescription().isEmpty());
}
