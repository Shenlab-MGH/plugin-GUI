#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"

TEST (SemanticComponentTests, ValidatesStableSemanticIds)
{
    EXPECT_TRUE (isValidSemanticId ("oe.control.acquisition"));
    EXPECT_TRUE (isValidSemanticId ("oe.control.recording"));

    EXPECT_FALSE (isValidSemanticId (""));
    EXPECT_FALSE (isValidSemanticId ("oe"));
    EXPECT_FALSE (isValidSemanticId ("oe."));
    EXPECT_FALSE (isValidSemanticId ("OE.control.acquisition"));
    EXPECT_FALSE (isValidSemanticId ("oe.control.Record"));
    EXPECT_FALSE (isValidSemanticId ("oe.control.record-button"));
    EXPECT_FALSE (isValidSemanticId ("oe.control..recording"));
}

TEST (SemanticComponentTests, AppliesStableAccessibleMeaning)
{
    TextButton button ("R");

    applySemanticMetadata (button,
                           "oe.control.recording",
                           "Recording",
                           "Start or stop writing data to disk.");

    EXPECT_EQ (button.getComponentID(), "oe.control.recording");
    EXPECT_EQ (button.getTitle(), "Recording");
    EXPECT_EQ (button.getDescription(), "Start or stop writing data to disk.");
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
