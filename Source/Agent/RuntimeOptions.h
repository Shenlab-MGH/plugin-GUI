/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include <string>
#include <vector>

struct RuntimeIsolationOptions
{
    bool headless = false;
    bool disableNativeHttp = false;
    bool disableUserPlugins = false;
    bool agentPortExplicit = false;
    int agentPort = 37498;
    std::string stateDirectory;
    std::string configurationFile;
};

struct RuntimeOptionsParseResult
{
    RuntimeIsolationOptions options;
    std::string error;

    bool ok() const
    {
        return error.empty();
    }
};

namespace RuntimeOptions
{
RuntimeOptionsParseResult parse (
    const std::vector<std::string>& arguments);
}
