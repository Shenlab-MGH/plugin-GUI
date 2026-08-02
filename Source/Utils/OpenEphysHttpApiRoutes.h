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

/** Shared HTTP method/path constants for official routes used by both
    OpenEphysHttpServer registration and the Core R0 capability manifest.

    Values match existing string literals; replacing literals with these
    constants must not change runtime behavior.
*/
namespace OpenEphysHttpApi
{
inline constexpr const char* kMethodGet = "GET";
inline constexpr const char* kMethodPut = "PUT";

inline constexpr const char* kPathCapabilities = "/api/capabilities";
inline constexpr const char* kPathStatus = "/api/status";
inline constexpr const char* kPathRecording = "/api/recording";
inline constexpr const char* kPathCpu = "/api/cpu";
} // namespace OpenEphysHttpApi

#endif // OPEN_EPHYS_HTTP_API_ROUTES_H
