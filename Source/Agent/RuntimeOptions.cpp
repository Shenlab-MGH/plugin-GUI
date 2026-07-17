/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "RuntimeOptions.h"

#include <charconv>

namespace
{
bool consumeOnce (
    bool& observed,
    RuntimeOptionsParseResult& result,
    const char* option)
{
    if (observed)
    {
        result.error =
            std::string ("Duplicate option: ") + option;
        return false;
    }

    observed = true;
    return true;
}
}

RuntimeOptionsParseResult RuntimeOptions::parse (
    const std::vector<std::string>& arguments)
{
    RuntimeOptionsParseResult result;
    bool sawHeadless = false;
    bool sawNoHttp = false;
    bool sawNoUserPlugins = false;
    bool sawAgentUiaReadOnly = false;
    bool sawStateDirectory = false;
    bool sawAgentPort = false;

    for (std::size_t index = 0;
         index < arguments.size();
         ++index)
    {
        const auto& argument = arguments[index];

        if (argument == "--headless")
        {
            if (! consumeOnce (
                    sawHeadless,
                    result,
                    "--headless"))
                return result;
            result.options.headless = true;
        }
        else if (argument == "--no-http")
        {
            if (! consumeOnce (
                    sawNoHttp,
                    result,
                    "--no-http"))
                return result;
            result.options.disableNativeHttp = true;
        }
        else if (argument == "--no-user-plugins")
        {
            if (! consumeOnce (
                    sawNoUserPlugins,
                    result,
                    "--no-user-plugins"))
                return result;
            result.options.disableUserPlugins = true;
        }
        else if (argument == "--agent-uia-readonly")
        {
            if (! consumeOnce (
                    sawAgentUiaReadOnly,
                    result,
                    "--agent-uia-readonly"))
                return result;
            result.options.enableAgentUiaReadOnly = true;
        }
        else if (argument == "--state-dir")
        {
            if (! consumeOnce (
                    sawStateDirectory,
                    result,
                    "--state-dir"))
                return result;
            if (++index >= arguments.size()
                || arguments[index].empty()
                || arguments[index][0] == '-')
            {
                result.error =
                    "--state-dir requires a path";
                return result;
            }
            result.options.stateDirectory =
                arguments[index];
        }
        else if (argument == "--agent-port")
        {
            if (! consumeOnce (
                    sawAgentPort,
                    result,
                    "--agent-port"))
                return result;
            if (++index >= arguments.size())
            {
                result.error =
                    "--agent-port requires a port";
                return result;
            }

            const auto& text = arguments[index];
            int port = 0;
            const auto conversion = std::from_chars (
                text.data(),
                text.data() + text.size(),
                port);
            if (conversion.ec != std::errc()
                || conversion.ptr != text.data() + text.size()
                || port < 1024
                || port > 65535
                || port == 37497)
            {
                result.error =
                    "--agent-port must be 1024-65535 "
                    "and must not be 37497";
                return result;
            }
            result.options.agentPort = port;
            result.options.agentPortExplicit = true;
        }
        else if (! argument.empty()
                 && argument[0] == '-')
        {
            result.error =
                std::string ("Unknown option: ") + argument;
            return result;
        }
        else if (
            ! result.options.configurationFile.empty())
        {
            result.error =
                "Only one configuration file may be provided";
            return result;
        }
        else
        {
            result.options.configurationFile = argument;
        }
    }

    return result;
}
