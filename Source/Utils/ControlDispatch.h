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

#ifndef CONTROL_DISPATCH_H
#define CONTROL_DISPATCH_H

#include "ControlRead.h"

template <typename Value>
using ControlDispatchResult = ControlReadResult<Value>;

template <typename Dispatcher, typename Operation>
auto handleControlDispatch (Dispatcher&& dispatcher,
                            Operation&& operation,
                            std::chrono::milliseconds timeout)
    -> ControlDispatchResult<std::invoke_result_t<std::decay_t<Operation>>>
{
    return handleControlRead (
        std::forward<Dispatcher> (dispatcher),
        std::forward<Operation> (operation),
        timeout);
}

#endif
