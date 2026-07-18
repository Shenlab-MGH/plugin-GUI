/*
    ------------------------------------------------------------------
    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors
    ------------------------------------------------------------------
*/
#include "AgentAccessibilityAction.h"

std::optional<AgentObservedMode> AgentAccessibilityAction::targetFor (
    TransportControlKind kind,
    AgentObservedMode currentMode)
{
    if (currentMode == AgentObservedMode::unknown)
        return std::nullopt;

    if (kind == TransportControlKind::acquisition)
    {
        return currentMode == AgentObservedMode::idle
            ? AgentObservedMode::acquire
            : AgentObservedMode::idle;
    }

    return currentMode == AgentObservedMode::record
        ? AgentObservedMode::acquire
        : AgentObservedMode::record;
}
