/*
    ------------------------------------------------------------------
    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors
    ------------------------------------------------------------------
*/
#pragma once

#include "AgentState.h"
#include "TransportAccessibilityRegistry.h"

#include <optional>

namespace AgentAccessibilityAction
{
std::optional<AgentObservedMode> targetFor (
    TransportControlKind kind,
    AgentObservedMode currentMode);
}
