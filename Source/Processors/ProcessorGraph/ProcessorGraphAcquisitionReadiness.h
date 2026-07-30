/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#ifndef PROCESSOR_GRAPH_ACQUISITION_READINESS_H
#define PROCESSOR_GRAPH_ACQUISITION_READINESS_H

#include "../../../JuceLibraryCode/JuceHeader.h"
#include "../../TestableExport.h"

#include <cstddef>
#include <optional>
#include <vector>

enum class ProcessorGraphAcquisitionReadinessIssueCode
{
    invalidParameter,
    insufficientNodeCount,
    multipleNwbUsers,
    disabledProcessors,
    processorNotReady
};

struct ProcessorGraphAcquisitionReadinessParameter
{
    String key;
    String displayName;
    bool valid = true;
};

struct ProcessorGraphAcquisitionReadinessProcessor
{
    String name;
    int id = 0;
    bool enabled = true;
    bool ready = true;
    bool nwbUser = false;
    bool outputNode = false;
    std::vector<
        ProcessorGraphAcquisitionReadinessParameter>
        parameters;
};

struct ProcessorGraphAcquisitionReadinessFacts
{
    std::size_t nodeCount = 0;
    std::size_t minimumNodeCount = 4;
    std::vector<
        ProcessorGraphAcquisitionReadinessProcessor>
        processors;
};

struct ProcessorGraphAcquisitionReadinessIssue
{
    ProcessorGraphAcquisitionReadinessIssueCode code =
        ProcessorGraphAcquisitionReadinessIssueCode::
            invalidParameter;
    std::optional<
        ProcessorGraphAcquisitionReadinessProcessor>
        processor;
    String parameterKey;
    String parameterDisplayName;
    std::size_t actualCount = 0;
    std::size_t requiredCount = 0;
    std::vector<
        ProcessorGraphAcquisitionReadinessProcessor>
        processors;
};

struct ProcessorGraphAcquisitionReadiness
{
    bool ready = true;
    std::vector<
        ProcessorGraphAcquisitionReadinessProcessor>
        disabledProcessorsObservedBeforeFailure;
    std::vector<
        ProcessorGraphAcquisitionReadinessIssue>
        issues;
};

TESTABLE ProcessorGraphAcquisitionReadiness
evaluateProcessorGraphAcquisitionReadiness (
    const ProcessorGraphAcquisitionReadinessFacts& facts);

template <typename StagedOwner>
ProcessorGraphAcquisitionReadiness
inspectProcessorGraphAcquisitionReadinessStaged (
    StagedOwner& owner)
{
    ProcessorGraphAcquisitionReadinessFacts facts;
    const auto processorCount =
        owner.processorCount();
    facts.processors.reserve (processorCount);

    for (std::size_t index = 0;
         index < processorCount;
         ++index)
    {
        auto copied =
            owner.readBase (index);
        facts.processors.push_back (
            std::move (copied));

        const auto& current =
            facts.processors.back();
        const auto parameterCount =
            owner.parameterCount (index);
        for (std::size_t parameterIndex = 0;
             parameterIndex < parameterCount;
             ++parameterIndex)
        {
            if (! owner.readParameterValid (
                    index,
                    parameterIndex))
            {
                const auto parameterKey =
                    owner.readParameterKey (
                        index,
                        parameterIndex);
                const auto parameterDisplayName =
                    owner.readParameterDisplayName (
                        index,
                        parameterIndex);
                ProcessorGraphAcquisitionReadiness
                    result;
                result.ready = false;
                result.issues.push_back ({
                    ProcessorGraphAcquisitionReadinessIssueCode::
                        invalidParameter,
                    current,
                    parameterKey,
                    parameterDisplayName
                });
                return result;
            }
        }
    }

    facts.nodeCount = owner.nodeCount();
    if (facts.nodeCount < facts.minimumNodeCount)
    {
        return evaluateProcessorGraphAcquisitionReadiness (
            facts);
    }

    std::size_t nwbUserCount = 0;
    for (std::size_t index = 0;
         index < processorCount;
         ++index)
    {
        auto& current = facts.processors[index];
        current.nwbUser =
            owner.readNwbUser (index);
        if (current.nwbUser
            && ++nwbUserCount > 1)
        {
            return evaluateProcessorGraphAcquisitionReadiness (
                facts);
        }

        if (current.outputNode)
            continue;

        current.enabled =
            owner.readEnabled (index);
        current.ready =
            owner.readReady (index);
        if (! current.ready)
        {
            return evaluateProcessorGraphAcquisitionReadiness (
                facts);
        }
    }

    return evaluateProcessorGraphAcquisitionReadiness (
        facts);
}

enum class
    ProcessorGraphAcquisitionReadinessPresentationPolicy
{
    legacyInteractive,
    legacyHeadless,
    silent
};

template <typename PresentationPort>
bool presentProcessorGraphAcquisitionReadiness (
    const ProcessorGraphAcquisitionReadiness& result,
    ProcessorGraphAcquisitionReadinessPresentationPolicy policy,
    PresentationPort& port)
{
    using Code =
        ProcessorGraphAcquisitionReadinessIssueCode;
    using Policy =
        ProcessorGraphAcquisitionReadinessPresentationPolicy;

    if (result.ready || result.issues.empty())
        return result.ready;

    if (policy == Policy::silent)
        return false;

    const auto interactive =
        policy == Policy::legacyInteractive;
    const auto& issue = result.issues.front();

    for (const auto& processor :
         result.disabledProcessorsObservedBeforeFailure)
    {
        port.logCritical (
            processor.name
            + " ("
            + String (processor.id)
            + ") is disabled and blocking acquisition.");
    }

    switch (issue.code)
    {
        case Code::invalidParameter:
        {
            if (interactive
                && issue.processor.has_value())
            {
                port.playAlertSound();
                port.showBubble (
                    issue.processor->name
                    + " ("
                    + String (issue.processor->id)
                    + ") - "
                    + issue.parameterDisplayName
                    + " is invalid and blocking"
                      " acquisition.");
            }

            port.sendStatusMessage (
                "Parameter "
                + issue.parameterKey
                + " is not valid.");
            port.disableCallbacks();
            return false;
        }

        case Code::insufficientNodeCount:
            if (interactive)
            {
                port.playAlertSound();
                port.showBubble (
                    "Add a source processor to the signal chain"
                    " before starting acquisition");
            }
            port.sendStatusMessage (
                "Not enough processors in signal chain"
                " to acquire data.");
            port.disableCallbacks();
            return false;

        case Code::multipleNwbUsers:
            port.disableCallbacks();
            if (interactive)
            {
                port.showModalAsync (
                    "WARNING",
                    "Open Ephys currently does not support"
                    " multiple processors using NWB format."
                    " Please modify the signal chain accordingly"
                    " to proceed with acquisition.");
            }
            port.logCritical (
                "Multiple processors using NWB format"
                " is not supported. Please modify the signal"
                " chain accordingly to proceed with acquisition.");
            return false;

        case Code::processorNotReady:
            if (issue.processor.has_value())
            {
                const auto message =
                    issue.processor->name
                    + " ("
                    + String (issue.processor->id)
                    + ") is not ready and blocking acquisition";
                if (interactive)
                    port.showBubble (message);
                port.sendStatusMessage (message);
            }
            port.disableCallbacks();
            return false;

        case Code::disabledProcessors:
            if (interactive)
            {
                StringArray names;
                for (const auto& processor :
                     issue.processors)
                {
                    names.add (
                        processor.name
                        + " ("
                        + String (processor.id)
                        + ")");
                }

                port.playAlertSound();
                port.showBubble (
                    "The following processors are disabled"
                    " and blocking acquisition:\n\n"
                    + names.joinIntoString ("\n"));
            }
            port.disableCallbacks();
            return false;
    }

    return false;
}

template <typename OwnerFactory>
bool presentProcessorGraphAcquisitionReadinessWithLazyOwner (
    const ProcessorGraphAcquisitionReadiness& result,
    ProcessorGraphAcquisitionReadinessPresentationPolicy policy,
    OwnerFactory&& ownerFactory)
{
    if (result.ready)
        return true;

    auto owner = ownerFactory();
    if (! owner)
        return false;

    return presentProcessorGraphAcquisitionReadiness (
        result,
        policy,
        *owner);
}

#endif
