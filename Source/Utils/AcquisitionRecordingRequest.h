/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ------------------------------------------------------------------
*/

#ifndef ACQUISITION_RECORDING_REQUEST_H
#define ACQUISITION_RECORDING_REQUEST_H

#include "AcquisitionRecordingStatus.h"

struct StatusRequest
{
    AcquisitionRecordingMode mode = AcquisitionRecordingMode::idle;
    bool confirmUnsynchronized = false;
};

#endif
