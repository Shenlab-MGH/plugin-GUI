/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "AgentAccessibilityState.h"

AgentAccessibilityValue AgentAccessibilityState::acquisition (
    AgentObservedMode mode)
{
    switch (mode)
    {
        case AgentObservedMode::idle:
            return AgentAccessibilityValue::off;
        case AgentObservedMode::acquire:
        case AgentObservedMode::record:
            return AgentAccessibilityValue::on;
        case AgentObservedMode::unknown:
            return AgentAccessibilityValue::unknown;
    }

    return AgentAccessibilityValue::unknown;
}

AgentAccessibilityValue AgentAccessibilityState::recording (
    AgentObservedMode mode)
{
    switch (mode)
    {
        case AgentObservedMode::idle:
        case AgentObservedMode::acquire:
            return AgentAccessibilityValue::off;
        case AgentObservedMode::record:
            return AgentAccessibilityValue::on;
        case AgentObservedMode::unknown:
            return AgentAccessibilityValue::unknown;
    }

    return AgentAccessibilityValue::unknown;
}

std::string AgentAccessibilityState::toString (
    AgentAccessibilityValue value)
{
    switch (value)
    {
        case AgentAccessibilityValue::off:
            return "OFF";
        case AgentAccessibilityValue::on:
            return "ON";
        case AgentAccessibilityValue::unknown:
            return "UNKNOWN";
    }

    return "UNKNOWN";
}
