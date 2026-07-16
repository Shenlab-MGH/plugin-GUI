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

#include "TransportAccessibilityRegistry.h"

#include <algorithm>

namespace
{
const std::vector<TransportAccessibilityDescriptor> transportControls {
    {
        1,
        TransportControlKind::acquisition,
        "oe.transport.acquisition",
        "Acquisition",
        "Start or stop data acquisition",
        "Starts or stops data acquisition without changing recording settings.",
        TransportAccessibilityPattern::invoke,
        false
    },
    {
        1,
        TransportControlKind::recording,
        "oe.transport.recording",
        "Recording",
        "Start or stop recording to disk",
        "Starts or stops recording using the existing recording safety checks.",
        TransportAccessibilityPattern::invoke,
        true
    }
};
}

const std::vector<TransportAccessibilityDescriptor>&
TransportAccessibilityRegistry::controls() const
{
    return transportControls;
}

const TransportAccessibilityDescriptor&
TransportAccessibilityRegistry::get (TransportControlKind kind) const
{
    const auto match = std::find_if (
        transportControls.begin(),
        transportControls.end(),
        [kind] (const auto& descriptor)
        {
            return descriptor.kind == kind;
        });

    return *match;
}

const TransportAccessibilityDescriptor*
TransportAccessibilityRegistry::find (
    const std::string& automationId) const
{
    const auto match = std::find_if (
        transportControls.begin(),
        transportControls.end(),
        [&automationId] (const auto& descriptor)
        {
            return ! automationId.empty()
                && descriptor.automationId == automationId;
        });

    return match == transportControls.end() ? nullptr : &*match;
}
