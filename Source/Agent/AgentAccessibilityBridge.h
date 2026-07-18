/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include "../../JuceLibraryCode/JuceHeader.h"
#include "AgentTransportEndpoint.h"

#include <memory>

class AgentAccessibilityBridge final : public Component
{
public:
    explicit AgentAccessibilityBridge (
        std::shared_ptr<AgentTransportEndpoint> endpoint,
        bool interactive);
    ~AgentAccessibilityBridge() override;

    std::unique_ptr<AccessibilityHandler>
        createAccessibilityHandler() override;

private:
    class TransportNode;

    std::unique_ptr<TransportNode> acquisitionNode;
    std::unique_ptr<TransportNode> recordingNode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        AgentAccessibilityBridge)
};
