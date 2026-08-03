#include "../../Source/UI/ControlPanel.h"
#include "gtest/gtest.h"

TEST (ControlPanelRecordingSafetyTests, ForcedRecordingPolicyIsLimitedToOneSynchronousTransition)
{
    ScopedRecordingForce force;
    bool forcedDuringTransition = false;

    force.run (true, [&]
               { forcedDuringTransition = force.isActive(); });

    EXPECT_TRUE (forcedDuringTransition);
    EXPECT_FALSE (force.isActive());
}
