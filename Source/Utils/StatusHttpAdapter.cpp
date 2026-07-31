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

    ------------------------------------------------------------------
*/

#include "StatusHttpAdapter.h"

#include "ControlStatusJson.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace
{
StatusControlResult operationError (
    int httpStatus,
    StringRef code,
    StringRef message)
{
    StatusControlResult result;
    result.httpStatus = httpStatus;
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}

StatusControlResult unavailableOperation()
{
    return operationError (
        503,
        "operation_unavailable",
        "The status operation is unavailable.");
}

StatusControlResult failedOperation()
{
    return operationError (
        500,
        "operation_failed",
        "The status operation failed.");
}

StatusControlResult invalidRequestBody()
{
    return operationError (
        400,
        "invalid_request",
        "The status request body must be valid UTF-8 without embedded null bytes.");
}

bool isValidRequestBody (const std::string& body)
{
    if (body.size()
        > static_cast<size_t> ((std::numeric_limits<int>::max)()))
        return false;

    if (body.find ('\0') != std::string::npos)
        return false;

    const auto continuation = [] (uint8_t byte)
    {
        return byte >= 0x80 && byte <= 0xbf;
    };

    for (size_t index = 0; index < body.size();)
    {
        const auto first = static_cast<uint8_t> (body[index++]);
        if (first <= 0x7f)
            continue;

        if (first >= 0xc2 && first <= 0xdf)
        {
            if (index >= body.size()
                || ! continuation (
                    static_cast<uint8_t> (body[index++])))
                return false;
            continue;
        }

        if (first >= 0xe0 && first <= 0xef)
        {
            if (index + 1 >= body.size())
                return false;

            const auto second =
                static_cast<uint8_t> (body[index++]);
            const auto third =
                static_cast<uint8_t> (body[index++]);
            auto secondIsValid = continuation (second);
            if (first == 0xe0)
                secondIsValid = second >= 0xa0 && second <= 0xbf;
            else if (first == 0xed)
                secondIsValid = second >= 0x80 && second <= 0x9f;

            if (! secondIsValid || ! continuation (third))
                return false;
            continue;
        }

        if (first >= 0xf0 && first <= 0xf4)
        {
            if (index + 2 >= body.size())
                return false;

            const auto second =
                static_cast<uint8_t> (body[index++]);
            const auto third =
                static_cast<uint8_t> (body[index++]);
            const auto fourth =
                static_cast<uint8_t> (body[index++]);
            auto secondIsValid = continuation (second);
            if (first == 0xf0)
                secondIsValid = second >= 0x90 && second <= 0xbf;
            else if (first == 0xf4)
                secondIsValid = second >= 0x80 && second <= 0x8f;

            if (! secondIsValid
                || ! continuation (third)
                || ! continuation (fourth))
                return false;
            continue;
        }

        return false;
    }

    return true;
}

template <typename Operation, typename Serialiser>
void handleRequest (
    httplib::Response& response,
    Operation operation,
    Serialiser serialise)
{
    StatusControlResult result;
    try
    {
        result = operation();
    }
    catch (const std::exception&)
    {
        result = failedOperation();
    }
    catch (...)
    {
        result = failedOperation();
    }

    response.status = result.httpStatus;
    response.set_content (
        serialise (result).dump(),
        "application/json");
}
} // namespace

void registerStatusHttpRoutes (
    httplib::Server& server,
    StatusHttpHandlers handlers)
{
    server.Get (
        "/api/status",
        [get = std::move (handlers.get)] (
            const httplib::Request&,
            httplib::Response& response)
        {
            handleRequest (
                response,
                [&]
                {
                    return get ? get()
                               : unavailableOperation();
                },
                statusGetResultToJson);
        });

    server.Put (
        "/api/status",
        [put = std::move (handlers.put)] (
            const httplib::Request& request,
            httplib::Response& response)
        {
            handleRequest (
                response,
                [&]
                {
                    if (! put)
                        return unavailableOperation();

                    if (! isValidRequestBody (request.body))
                        return invalidRequestBody();

                    const auto body = String::fromUTF8 (
                        request.body.data(),
                        static_cast<int> (request.body.size()));
                    return put (body);
                },
                statusPutResultToJson);
        });
}
