/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "AgentAccessibilityBridge.h"

#include "AgentAccessibilityAction.h"
#include "AgentAccessibilityState.h"
#include "TransportAccessibilityRegistry.h"
#include "../Utils/Utils.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>

namespace
{
const char* observedModeName (AgentObservedMode mode)
{
    switch (mode)
    {
        case AgentObservedMode::idle: return "IDLE";
        case AgentObservedMode::acquire: return "ACQUIRE";
        case AgentObservedMode::record: return "RECORD";
        case AgentObservedMode::unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* submitOutcomeName (AgentMailboxSubmitOutcome outcome)
{
    switch (outcome)
    {
        case AgentMailboxSubmitOutcome::accepted: return "ACCEPTED";
        case AgentMailboxSubmitOutcome::duplicate: return "DUPLICATE";
        case AgentMailboxSubmitOutcome::idConflict: return "ID_CONFLICT";
        case AgentMailboxSubmitOutcome::busy: return "BUSY";
        case AgentMailboxSubmitOutcome::invalidRequest: return "INVALID_REQUEST";
        case AgentMailboxSubmitOutcome::shuttingDown: return "SHUTTING_DOWN";
        case AgentMailboxSubmitOutcome::unavailable: return "UNAVAILABLE";
    }
    return "UNKNOWN";
}

const char* requestStateName (AgentMailboxRequestState state)
{
    switch (state)
    {
        case AgentMailboxRequestState::unknown: return "UNKNOWN";
        case AgentMailboxRequestState::pending: return "PENDING";
        case AgentMailboxRequestState::active: return "ACTIVE";
        case AgentMailboxRequestState::completed: return "COMPLETED";
        case AgentMailboxRequestState::expired: return "EXPIRED";
        case AgentMailboxRequestState::cancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

const char* applyOutcomeName (AgentTransportApplyOutcome outcome)
{
    switch (outcome)
    {
        case AgentTransportApplyOutcome::completed: return "COMPLETED";
        case AgentTransportApplyOutcome::alreadySatisfied: return "ALREADY_SATISFIED";
        case AgentTransportApplyOutcome::rejected: return "REJECTED";
        case AgentTransportApplyOutcome::wrongThread: return "WRONG_THREAD";
        case AgentTransportApplyOutcome::busy: return "BUSY";
        case AgentTransportApplyOutcome::preconditionChanged: return "PRECONDITION_CHANGED";
        case AgentTransportApplyOutcome::executionFailed: return "EXECUTION_FAILED";
        case AgentTransportApplyOutcome::readbackMismatch: return "READBACK_MISMATCH";
    }
    return "UNKNOWN";
}

std::uint64_t monotonicMilliseconds()
{
    return static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

class InteractionTracker
{
public:
    void set (std::string next)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        requestId = std::move (next);
    }

    std::string get() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return requestId;
    }

private:
    mutable std::mutex mutex;
    std::string requestId;
};

AccessibleState modalResilientReadOnlyState()
{
    return AccessibleState().withFocusable().withAccessibleOffscreen();
}

AccessibilityActions transportActions (
    const std::shared_ptr<AgentTransportEndpoint>& endpoint,
    const std::shared_ptr<InteractionTracker>& tracker,
    TransportControlKind kind,
    bool interactive)
{
    AccessibilityActions actions;
    if (! interactive)
        return actions;

    actions.addAction (
        AccessibilityActionType::press,
        [endpoint, tracker, kind]
        {
            if (! endpoint)
                return;

            const auto snapshot = endpoint->snapshot();
            const auto target = AgentAccessibilityAction::targetFor (
                kind,
                snapshot.mode);
            if (! target)
                return;

            static std::atomic<std::uint64_t> sequence { 0 };
            const auto requestId =
                std::string ("uia-")
                + std::to_string (++sequence);
            tracker->set (requestId);
            constexpr std::uint64_t actionLifetimeMs = 5000;
            const auto outcome = endpoint->submit ({
                requestId,
                *target,
                snapshot.revision,
                snapshot.mode,
                {}, {}, {}, {},
                monotonicMilliseconds() + actionLifetimeMs
            });
            LOGA (
                "Agent UIA request_id=", requestId,
                " control=",
                kind == TransportControlKind::acquisition
                    ? "ACQUISITION" : "RECORDING",
                " source=", observedModeName (snapshot.mode),
                " target=", observedModeName (*target),
                " revision=", snapshot.revision,
                " submit=", submitOutcomeName (outcome));
            MessageManager::callAsync (
                [endpoint, requestId]
                {
                    const auto lookup = endpoint->query (requestId);
                    const auto finalMode = lookup.result
                        ? observedModeName (
                              lookup.result->finalState.mode)
                        : "UNKNOWN";
                    const auto applyOutcome = lookup.result
                        ? applyOutcomeName (lookup.result->outcome)
                        : "NONE";
                    LOGA (
                        "Agent UIA terminal request_id=", requestId,
                        " state=", requestStateName (lookup.state),
                        " apply=", applyOutcome,
                        " final_mode=", finalMode);
                });
        });
    return actions;
}

class ReadOnlyTransportValue final
    : public AccessibilityTextValueInterface
{
public:
    ReadOnlyTransportValue (
        std::shared_ptr<AgentTransportEndpoint> endpointToUse,
        std::shared_ptr<InteractionTracker> trackerToUse,
        TransportControlKind kindToUse)
        : endpoint (std::move (endpointToUse)),
          tracker (std::move (trackerToUse)),
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
        String result (
            AgentAccessibilityState::toString (value).c_str());
        const auto requestId = tracker ? tracker->get() : std::string();
        if (! requestId.empty() && endpoint)
        {
            const auto lookup = endpoint->query (requestId);
            result << "|REQUEST=" << requestId
                   << "|STATE=" << requestStateName (lookup.state);
            if (lookup.result)
                result << "|OUTCOME="
                       << applyOutcomeName (lookup.result->outcome);
        }
        return result;
    }

    void setValueAsString (const String&) override
    {
    }

private:
    std::shared_ptr<AgentTransportEndpoint> endpoint;
    std::shared_ptr<InteractionTracker> tracker;
    TransportControlKind kind;
};

class TransportHandler final : public AccessibilityHandler
{
public:
    TransportHandler (
        Component& component,
        std::shared_ptr<AgentTransportEndpoint> endpoint,
        std::shared_ptr<InteractionTracker> tracker,
        TransportControlKind kind,
        bool interactive)
        : AccessibilityHandler (
              component,
              AccessibilityRole::button,
              transportActions (endpoint, tracker, kind, interactive),
              Interfaces {
                  std::make_unique<ReadOnlyTransportValue> (
                      endpoint,
                      tracker,
                      kind) })
    {
    }

    AccessibleState getCurrentState() const override
    {
        return modalResilientReadOnlyState();
    }
};

class ReadOnlyAgentRootHandler final : public AccessibilityHandler
{
public:
    explicit ReadOnlyAgentRootHandler (Component& component)
        : AccessibilityHandler (
              component,
              AccessibilityRole::group,
              AccessibilityActions {})
    {
    }

    AccessibleState getCurrentState() const override
    {
        return modalResilientReadOnlyState();
    }
};
}

class AgentAccessibilityBridge::TransportNode final
    : public Component
{
public:
    TransportNode (
        std::shared_ptr<AgentTransportEndpoint> endpointToUse,
        TransportAccessibilityDescriptor descriptorToUse,
        bool interactiveToUse)
        : endpoint (std::move (endpointToUse)),
          descriptor (std::move (descriptorToUse)),
          interactive (interactiveToUse)
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
        return std::make_unique<TransportHandler> (
            *this,
            endpoint,
            tracker,
            descriptor.kind,
            interactive);
    }

private:
    std::shared_ptr<AgentTransportEndpoint> endpoint;
    TransportAccessibilityDescriptor descriptor;
    bool interactive = false;
    std::shared_ptr<InteractionTracker> tracker =
        std::make_shared<InteractionTracker>();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportNode)
};

AgentAccessibilityBridge::~AgentAccessibilityBridge() = default;

AgentAccessibilityBridge::AgentAccessibilityBridge (
    std::shared_ptr<AgentTransportEndpoint> endpoint,
    bool interactive)
{
    const TransportAccessibilityRegistry registry;
    setComponentID ("oe.agent.root");
    setTitle ("Open Ephys Agent");
    setDescription (
        interactive
            ? "Allowlisted Open Ephys Agent transport controls"
            : "Read-only Open Ephys Agent state");
    setHelpText (
        interactive
            ? "Exposes only Acquisition and Recording press actions."
            : "Exposes allowlisted transport state without control actions.");
    setAccessible (true);
    setFocusContainerType (FocusContainerType::focusContainer);
    setInterceptsMouseClicks (false, false);

    acquisitionNode = std::make_unique<TransportNode> (
        endpoint,
        registry.get (TransportControlKind::acquisition),
        interactive);
    recordingNode = std::make_unique<TransportNode> (
        std::move (endpoint),
        registry.get (TransportControlKind::recording),
        interactive);

    addAndMakeVisible (acquisitionNode.get());
    addAndMakeVisible (recordingNode.get());
    acquisitionNode->setBounds (0, 0, 1, 1);
    recordingNode->setBounds (0, 0, 1, 1);
}

std::unique_ptr<AccessibilityHandler>
AgentAccessibilityBridge::createAccessibilityHandler()
{
    return std::make_unique<ReadOnlyAgentRootHandler> (*this);
}
