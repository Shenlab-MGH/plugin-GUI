/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#include "ProcessorGraphAcquisitionReadiness.h"

ProcessorGraphAcquisitionReadiness
evaluateProcessorGraphAcquisitionReadiness (
    const ProcessorGraphAcquisitionReadinessFacts& facts)
{
    using Code =
        ProcessorGraphAcquisitionReadinessIssueCode;
    ProcessorGraphAcquisitionReadiness result;

    for (const auto& processor : facts.processors)
    {
        if (processor.outputNode)
            continue;

        for (const auto& parameter :
             processor.parameters)
        {
            if (! parameter.valid)
            {
                result.issues.push_back ({
                    Code::invalidParameter,
                    processor,
                    parameter.key,
                    parameter.displayName
                });
                result.ready = false;
                return result;
            }
        }
    }

    if (facts.nodeCount < facts.minimumNodeCount)
    {
        ProcessorGraphAcquisitionReadinessIssue issue;
        issue.code = Code::insufficientNodeCount;
        issue.actualCount = facts.nodeCount;
        issue.requiredCount =
            facts.minimumNodeCount;
        result.issues.push_back (std::move (issue));
        result.ready = false;
        return result;
    }

    std::size_t nwbUserCount = 0;

    for (const auto& processor : facts.processors)
    {
        if (processor.nwbUser)
        {
            ++nwbUserCount;
            if (nwbUserCount > 1)
            {
                ProcessorGraphAcquisitionReadinessIssue
                    issue;
                issue.code = Code::multipleNwbUsers;
                issue.actualCount = nwbUserCount;
                issue.requiredCount = 1;
                result.issues.push_back (
                    std::move (issue));
                result.ready = false;
                return result;
            }
        }

        if (processor.outputNode)
            continue;

        if (! processor.enabled)
        {
            result
                .disabledProcessorsObservedBeforeFailure
                .push_back (processor);
        }

        if (! processor.ready)
        {
            ProcessorGraphAcquisitionReadinessIssue
                issue;
            issue.code = Code::processorNotReady;
            issue.processor = processor;
            result.issues.push_back (
                std::move (issue));
            result.ready = false;
            return result;
        }
    }

    if (! result
             .disabledProcessorsObservedBeforeFailure
             .empty())
    {
        ProcessorGraphAcquisitionReadinessIssue issue;
        issue.code = Code::disabledProcessors;
        issue.processors =
            result
                .disabledProcessorsObservedBeforeFailure;
        result.issues.push_back (std::move (issue));
    }

    result.ready = result.issues.empty();
    return result;
}
