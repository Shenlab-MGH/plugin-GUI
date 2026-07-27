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

#include "DelayMonitor.h"
#include "../../UI/LookAndFeel/CustomLookAndFeel.h"
#include "../../UI/SemanticComponent.h"

DelayMonitor::DelayMonitor() : delay (0.0f),
                               isEnabled (true),
                               colour (Colours::white)
{
    setInterceptsMouseClicks (false, false);
    setAccessibilityContext (
        "oe.status.processing_delay",
        "Processing delay",
        "Elapsed processing time for this data stream.");
}

DelayMonitor::~DelayMonitor()
{
}

void DelayMonitor::setDelay (float delayMs)
{
    delay = delayMs;

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (AccessibilityEvent::valueChanged);
}

void DelayMonitor::setAccessibilityContext (StringRef semanticId,
                                            StringRef title,
                                            StringRef description)
{
    applySemanticMetadata (*this, semanticId, title, description);
}

void DelayMonitor::setEnabled (bool state)
{
    isEnabled = state;

    if (isEnabled)
        colour = Colours::white;
    else
        colour = Colours::grey;

    repaint();
}

void DelayMonitor::startAcquisition()
{
    startTimer (500);
}

void DelayMonitor::stopAcquisition()
{
    stopTimer();
}

void DelayMonitor::timerCallback()
{
    repaint();
}

std::unique_ptr<AccessibilityHandler> DelayMonitor::createAccessibilityHandler()
{
    return createReadOnlyTextAccessibilityHandler (
        *this,
        [this]
        { return String (delay, 2) + " ms"; });
}

void DelayMonitor::paint (Graphics& g)
{
    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions ("Fira Sans", "SemiBold", 12));
    g.drawText (String (delay, 2) + " ms", 0, 0, 60, 20, Justification::centredLeft);
}

void DelayMonitor::handleCommandMessage (int commandId)
{
    repaint();
}
