/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ------------------------------------------------------------------
*/

#include "AgentStateSnapshotCache.h"

#include <utility>

AgentStateSnapshotCache::AgentStateSnapshotCache (
    AgentStateSnapshot initial)
    : current (std::move (initial))
{
}

void AgentStateSnapshotCache::publish (AgentStateSnapshot next)
{
    const std::lock_guard<std::mutex> lock (mutex);
    current = std::move (next);
}

AgentStateSnapshot AgentStateSnapshotCache::snapshot() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return current;
}
