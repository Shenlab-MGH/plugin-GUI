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

#ifndef STATUS_HTTP_ADAPTER_H
#define STATUS_HTTP_ADAPTER_H

#include "StatusControl.h"
#include "httplib.h"

#include <functional>

struct StatusHttpHandlers
{
    std::function<StatusControlResult()> get;
    std::function<StatusControlResult (StringRef)> put;
};

/** Registers the GET transport adapter for the shared /api/status contract. */
TESTABLE void registerStatusHttpGetRoute (
    httplib::Server& server,
    std::function<StatusControlResult()> get);

/** Registers the transport adapter for the shared /api/status contract. */
TESTABLE void registerStatusHttpRoutes (
    httplib::Server& server,
    StatusHttpHandlers handlers);

#endif
