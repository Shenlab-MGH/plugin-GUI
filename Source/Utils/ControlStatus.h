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

#ifndef CONTROL_STATUS_H
#define CONTROL_STATUS_H

#include "../../JuceLibraryCode/JuceHeader.h"

struct ClockStatus
{
    String display;
    int64 elapsedMilliseconds = 0;
    String mode;
    String reference;
    bool running = false;
    bool recording = false;
};

struct RecordingOptionsStatus
{
    bool expanded = false;
    bool forceNewDirectory = false;
    bool newDirectoryRequested = false;
    bool newDirectoryRequestAvailable = false;
    bool recording = false;
};

#endif
