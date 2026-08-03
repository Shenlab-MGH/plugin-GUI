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

#ifndef STATUS_API_HANDLER_H
#define STATUS_API_HANDLER_H

#include "../TestableExport.h"
#include "StatusControl.h"
#include "httplib.h"
#include "json.hpp"

#include <functional>

struct StatusApiHandlers
{
    std::function<StatusMode()> readMode;
    std::function<StatusTransitionResult (StatusMode)> requestMode;
};

TESTABLE void handleStatusGet (
    const httplib::Request& request,
    httplib::Response& response,
    const StatusApiHandlers& handlers);

TESTABLE void handleStatusPut (
    const httplib::Request& request,
    httplib::Response& response,
    const StatusApiHandlers& handlers);

#endif
