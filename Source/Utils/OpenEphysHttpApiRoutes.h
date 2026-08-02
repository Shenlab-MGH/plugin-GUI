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

#ifndef OPEN_EPHYS_HTTP_API_ROUTES_H
#define OPEN_EPHYS_HTTP_API_ROUTES_H

#include <utility>

/** Shared HTTP method+path route descriptors for official Core R0 routes
    (plus GET /api/capabilities). Used by both OpenEphysHttpServer registration
    and the Core R0 capability manifest so method/path cannot drift.

    Scope is intentionally small: only existing Core R0 routes and discovery.
*/
namespace OpenEphysHttpApi
{
enum class Method
{
    Get,
    Put
};

struct Route
{
    Method method;
    const char* path;

    constexpr const char* methodString() const noexcept
    {
        return method == Method::Get ? "GET" : "PUT";
    }
};

inline constexpr Route kCapabilitiesGet { Method::Get, "/api/capabilities" };
inline constexpr Route kStatusGet { Method::Get, "/api/status" };
inline constexpr Route kStatusPut { Method::Put, "/api/status" };
inline constexpr Route kRecordingGet { Method::Get, "/api/recording" };
inline constexpr Route kRecordingPut { Method::Put, "/api/recording" };
inline constexpr Route kCpuGet { Method::Get, "/api/cpu" };

/** Register a handler using the route descriptor's method (Get or Put).
    The same Route instance must drive capability serialization and registration.
*/
template <typename Server, typename Handler>
void registerRoute (Server& server, const Route& route, Handler&& handler)
{
    switch (route.method)
    {
        case Method::Get:
            server.Get (route.path, std::forward<Handler> (handler));
            break;
        case Method::Put:
            server.Put (route.path, std::forward<Handler> (handler));
            break;
    }
}
} // namespace OpenEphysHttpApi

#endif // OPEN_EPHYS_HTTP_API_ROUTES_H
