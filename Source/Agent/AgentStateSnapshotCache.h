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

#pragma once

#include "AgentState.h"

#include <mutex>

class AgentStateSnapshotCache
{
public:
    explicit AgentStateSnapshotCache (AgentStateSnapshot initial);

    void publish (AgentStateSnapshot next);
    AgentStateSnapshot snapshot() const;

private:
    mutable std::mutex mutex;
    AgentStateSnapshot current;
};
