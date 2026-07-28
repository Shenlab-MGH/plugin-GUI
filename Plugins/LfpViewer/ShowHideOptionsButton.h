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

#ifndef __SHOWHIDEOPTIONSBUTTON_H__
#define __SHOWHIDEOPTIONSBUTTON_H__

#include <VisualizerWindowHeaders.h>

#include <array>
#include <vector>

namespace LfpViewer
{

class LfpDisplayOptions;
struct ShowHideOptionsButtonAccessibilityState;

/**
 
 Toggles view options drawer for LfpDisplaySplitter.
 
 */
class ShowHideOptionsButton : public Button
{
public:
    /** Constructor */
    ShowHideOptionsButton (LfpDisplayOptions*);

    /** Destructor */
    virtual ~ShowHideOptionsButton();

    /** Renders the button */
    void paintButton (Graphics& g, bool, bool);

    /** Publishes current drawer state for worker-safe accessibility providers. */
    void refreshAccessibilityState();

protected:
    std::unique_ptr<AccessibilityHandler>
    createAccessibilityHandler() override;

private:
    void buttonStateChanged() override;
    void enablementChanged() override;
    void focusGained (
        FocusChangeType cause) override;
    void focusLost (
        FocusChangeType cause) override;

    std::shared_ptr<
        ShowHideOptionsButtonAccessibilityState>
        accessibilityState;
};

}; // namespace LfpViewer
#endif
