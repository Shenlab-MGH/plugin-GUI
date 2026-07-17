/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "AgentRecordingSafety.h"

AgentRecordingGateDecision AgentRecordingSafety::evaluate (
    const AgentRecordingGateContext& context,
    const AgentRecordingGateObservation& observation) const
{
    if (observation.mode != AgentObservedMode::idle
        && observation.mode != AgentObservedMode::acquire)
    {
        return { AgentRecordingGateOutcome::stateUnknown };
    }

    if (context.agentInitiated
        && observation.revision != context.expectedRevision)
    {
        return { AgentRecordingGateOutcome::revisionConflict };
    }

    if (! observation.hasRecordNodes
        || ! observation.recordNodeDirectoriesValid)
    {
        return { AgentRecordingGateOutcome::recordNodesNotReady };
    }

    if (context.agentInitiated && ! observation.directoryPrepared)
        return { AgentRecordingGateOutcome::directoryNotPrepared };

    if (! observation.synchronized)
        return { AgentRecordingGateOutcome::syncBlocked };

    return { AgentRecordingGateOutcome::ready };
}
