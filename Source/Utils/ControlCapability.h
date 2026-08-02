/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef CONTROL_CAPABILITY_H
#define CONTROL_CAPABILITY_H

#include "../../JuceLibraryCode/JuceHeader.h"
#include "../TestableExport.h"
#include <vector>

enum class ControlCapabilityKind
{
    action,
    toggle,
    value,
    range,
    status,
    selection,
    collection
};

struct ControlApiOperation
{
    String operation;
    String method;
    String path;
    StringArray requestFields;
    StringArray responseFields;
};

struct ControlCapability
{
    String id;
    String name;
    String description;
    ControlCapabilityKind kind;
    std::vector<ControlApiOperation> operations;
};

TESTABLE const std::vector<ControlCapability>& getCoreControlCapabilities();
TESTABLE const ControlCapability* findControlCapability (StringRef id);

#endif
