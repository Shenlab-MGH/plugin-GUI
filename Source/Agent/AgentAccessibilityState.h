/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include "AgentState.h"

#include <string>

enum class AgentAccessibilityValue
{
    off,
    on,
    unknown
};

namespace AgentAccessibilityState
{
AgentAccessibilityValue acquisition (AgentObservedMode mode);
AgentAccessibilityValue recording (AgentObservedMode mode);
std::string toString (AgentAccessibilityValue value);
}
