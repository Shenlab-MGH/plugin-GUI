/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.

*/

#include "AgentState.h"

#include <utility>

AgentStateStore::AgentStateStore (std::string guiVersion)
    : current { std::move (guiVersion), AgentObservedMode::unknown, 0 }
{
}

AgentStateSnapshot AgentStateStore::snapshot() const
{
    return current;
}

AgentStateSnapshot AgentStateStore::observe (AgentObservedMode mode)
{
    if (current.mode != mode)
    {
        current.mode = mode;
        ++current.revision;
    }

    return current;
}
