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

#ifndef __LFPCHANNELDISPLAYINFO_H__
#define __LFPCHANNELDISPLAYINFO_H__

#include <VisualizerWindowHeaders.h>

#include <array>
#include <memory>
#include <vector>

#include "LfpChannelDisplay.h"
#include "LfpDisplayClasses.h"
#include "LfpDisplayNode.h"

namespace LfpViewer
{
class LfpWaveformVisibilityAccessibilityState;
class LfpWaveformVisibilityButton;
class LfpChannelActionAccessibilityState;
class LfpChannelActionAccessibilityComponent;

TESTABLE String encodeWaveformVisibilityUtf8Hex (StringRef text);

enum class LfpChannelAction
{
    select,
    toggleFocus,
    toggleInvert,
    monitor
};

enum class LfpChannelActionResult
{
    rejected,
    completed,
    completedNoChange
};

/**
    Displays meta data pertaining to an associated channel, such as channel number.
 
    The enableButton displays the channel number and toggles the drawing of the
    associated LfpChannelDisplay waveform on or off.
 */
class LfpChannelDisplayInfo : public LfpChannelDisplay,
                              public Button::Listener,
                              public TooltipClient
{
    friend class LfpDisplay;
    friend class LfpWaveformVisibilityAccessibilityState;
    friend class LfpWaveformVisibilityButton;
    friend class LfpChannelActionAccessibilityState;
    friend class LfpChannelActionAccessibilityComponent;

public:
    /** Constructor */
    LfpChannelDisplayInfo (LfpDisplaySplitter*, LfpDisplay*, LfpDisplayOptions*, int channelNumber);
    ~LfpChannelDisplayInfo() override;

    /** Draws this info for one channel */
    void paint (Graphics& g) override;

    /** Responds to button clicks (toggles channel on and off)*/
    void buttonClicked (Button* button) override;

    /** Sets component layout */
    void resized() override;

    /** Changes the colour of the display to show recorded channels*/
    void recordingStarted();

    /** Reverts the colour changes from start of recording */
    void recordingStopped();

    /** Sets whether this channel is enabled */
    void setEnabledState (bool);

    /** Updates the channel type (DATA, ADC, AUX) */
    void updateType (ContinuousChannel::Type) override;

    /** Updates the position of this component*/
    void updateXY (float, float);

    /** Updates the mean and RMS values of the channel */
    void updateMeanAndRMS();

    /** Sets whether this channel is in single-channel mode */
    void setSingleChannelState (bool);

    /** Returns the sample rate associated with this channel */
    int getChannelSampleRate();

    /** Sets the sample rate associated with this channel */
    void setChannelSampleRate (int samplerate);

    /** Updates the parent LfpDisplay that the track vertical zoom should update */
    virtual void mouseDrag (const MouseEvent& event) override;

    /** Disengages the mouse drag to resize track height */
    virtual void mouseUp (const MouseEvent& event) override;

protected:
    std::unique_ptr<AccessibilityHandler>
    createAccessibilityHandler() override;
    void visibilityChanged() override;
    void enablementChanged() override;
    void parentHierarchyChanged() override;

private:
    bool isSingleChannel;
    float x, y;

    float rms;
    float mean;

    int samplerate;
    int subProcessorIdx;

    std::unique_ptr<UtilityButton> enableButton;
    std::shared_ptr<LfpWaveformVisibilityAccessibilityState>
        waveformVisibilityAccessibilityState;
    std::array<
        std::unique_ptr<Component>,
        4>
        channelActionAccessibilityComponents;
    std::array<
        std::shared_ptr<LfpChannelActionAccessibilityState>,
        4>
        channelActionAccessibilityStates;

    bool channelTypeStringIsVisible;
    bool channelNumberHidden;

    Path pointerPath;

    /** Get/set whether enabled button is visible*/
    void setEnabledButtonVisibility (bool shouldBeVisible);
    bool getEnabledButtonVisibility();

    /** Get/set whether channel type string is visible*/
    void setChannelTypeStringVisibility (bool shouldBeVisible);
    bool getChannelTypeStringVisibility();

    /** Get/set whether channel number is hidden */
    void setChannelNumberIsHidden (bool shouldBeHidden);
    bool isChannelNumberHidden();

    void refreshWaveformVisibilityAccessibility (
        const std::shared_ptr<
            const LfpStableChannelIdentity>& identity,
        int nodeId,
        bool structurallyAvailable,
        bool waveformVisible);
    void revokeWaveformVisibilityAccessibility ();
    bool performWaveformVisibilityAccessibilityToggle (
        const std::shared_ptr<
            LfpWaveformVisibilityAccessibilityState>& state);
    bool matchesWaveformVisibilityAccessibilityIdentity (
        const LfpStableChannelIdentity& identity,
        int nodeId) const;
    bool isWaveformVisibilityAccessibilityControlAvailable () const;
    void handleWaveformVisibilityAccessibilityLifecycleChange ();

    void refreshChannelActionAccessibility (
        const std::shared_ptr<
            const LfpStableChannelIdentity>& identity,
        int nodeId,
        bool structurallyAvailable);
    void revokeChannelActionAccessibility ();
    LfpChannelActionResult
    performChannelActionAccessibility (
        const std::shared_ptr<
            LfpChannelActionAccessibilityState>& state);
    bool matchesChannelActionAccessibilityIdentity (
        const LfpStableChannelIdentity& identity,
        int nodeId) const;
    void handleChannelActionAccessibilityLifecycleChange ();
    void layoutChannelActionAccessibilityComponents ();

    String getTooltip() override;
};

}; // namespace LfpViewer
#endif
