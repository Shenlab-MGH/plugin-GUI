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

#include <cstdint>
#include <string>
#include <vector>

enum class TransportControlKind
{
    acquisition,
    recording
};

enum class TransportAccessibilityPattern
{
    invoke
};

struct TransportAccessibilityDescriptor
{
    std::uint32_t schemaVersion;
    TransportControlKind kind;
    std::string automationId;
    std::string name;
    std::string description;
    std::string helpText;
    TransportAccessibilityPattern requiredPattern;
    bool requiresRecordingPreflight;
};

class TransportAccessibilityRegistry
{
public:
    const std::vector<TransportAccessibilityDescriptor>& controls() const;

    const TransportAccessibilityDescriptor& get (
        TransportControlKind kind) const;

    const TransportAccessibilityDescriptor* find (
        const std::string& automationId) const;
};
