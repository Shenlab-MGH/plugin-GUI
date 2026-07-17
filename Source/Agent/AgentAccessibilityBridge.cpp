/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "AgentAccessibilityBridge.h"

#include "AgentAccessibilityState.h"
#include "TransportAccessibilityRegistry.h"

#include <utility>

namespace
{
class ReadOnlyTransportValue final
    : public AccessibilityTextValueInterface
{
public:
    ReadOnlyTransportValue (
        std::shared_ptr<AgentTransportEndpoint> endpointToUse,
        TransportControlKind kindToUse)
        : endpoint (std::move (endpointToUse)),
          kind (kindToUse)
    {
    }

    bool isReadOnly() const override
    {
        return true;
    }

    String getCurrentValueAsString() const override
    {
        const auto mode = endpoint
                              ? endpoint->snapshot().mode
                              : AgentObservedMode::unknown;
        const auto value =
            kind == TransportControlKind::acquisition
                ? AgentAccessibilityState::acquisition (mode)
                : AgentAccessibilityState::recording (mode);
        return String (
            AgentAccessibilityState::toString (value).c_str());
    }

    void setValueAsString (const String&) override
    {
    }

private:
    std::shared_ptr<AgentTransportEndpoint> endpoint;
    TransportControlKind kind;
};

class ReadOnlyTransportHandler final : public AccessibilityHandler
{
public:
    ReadOnlyTransportHandler (
        Component& component,
        std::shared_ptr<AgentTransportEndpoint> endpoint,
        TransportControlKind kind)
        : AccessibilityHandler (
              component,
              AccessibilityRole::button,
              AccessibilityActions {},
              Interfaces {
                  std::make_unique<ReadOnlyTransportValue> (
                      std::move (endpoint),
                      kind) })
    {
    }

    AccessibleState getCurrentState() const override
    {
        return AccessibilityHandler::getCurrentState()
            .withAccessibleOffscreen();
    }
};
}

class AgentAccessibilityBridge::TransportNode final
    : public Component
{
public:
    TransportNode (
        std::shared_ptr<AgentTransportEndpoint> endpointToUse,
        TransportAccessibilityDescriptor descriptorToUse)
        : endpoint (std::move (endpointToUse)),
          descriptor (std::move (descriptorToUse))
    {
        setComponentID (descriptor.automationId);
        setTitle (descriptor.name);
        setDescription (descriptor.description);
        setHelpText (descriptor.helpText);
        setAccessible (true);
        setInterceptsMouseClicks (false, false);
    }

    std::unique_ptr<AccessibilityHandler>
        createAccessibilityHandler() override
    {
        return std::make_unique<ReadOnlyTransportHandler> (
            *this,
            endpoint,
            descriptor.kind);
    }

private:
    std::shared_ptr<AgentTransportEndpoint> endpoint;
    TransportAccessibilityDescriptor descriptor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportNode)
};

AgentAccessibilityBridge::~AgentAccessibilityBridge() = default;

AgentAccessibilityBridge::AgentAccessibilityBridge (
    std::shared_ptr<AgentTransportEndpoint> endpoint)
{
    const TransportAccessibilityRegistry registry;
    setComponentID ("oe.agent.root");
    setTitle ("Open Ephys Agent");
    setDescription ("Read-only Open Ephys Agent state");
    setHelpText (
        "Exposes allowlisted transport state without control actions.");
    setAccessible (true);
    setInterceptsMouseClicks (false, false);

    acquisitionNode = std::make_unique<TransportNode> (
        endpoint,
        registry.get (TransportControlKind::acquisition));
    recordingNode = std::make_unique<TransportNode> (
        std::move (endpoint),
        registry.get (TransportControlKind::recording));

    addAndMakeVisible (acquisitionNode.get());
    addAndMakeVisible (recordingNode.get());
    acquisitionNode->setBounds (0, 0, 1, 1);
    recordingNode->setBounds (0, 0, 1, 1);
}

std::unique_ptr<AccessibilityHandler>
AgentAccessibilityBridge::createAccessibilityHandler()
{
    return std::make_unique<AccessibilityHandler> (
        *this,
        AccessibilityRole::group,
        AccessibilityActions {});
}
