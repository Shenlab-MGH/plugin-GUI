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

#ifndef __LFPDISPLAYOPTIONS_H__
#define __LFPDISPLAYOPTIONS_H__

#include <VisualizerWindowHeaders.h>
#include <EditorHeaders.h>

#include <atomic>
#include <array>
#include <vector>

#include "LfpDisplayClasses.h"

#include "EventDisplayInterface.h"
#include "ShowHideOptionsButton.h"

namespace LfpViewer
{

/** Returns a valid colour-grouping item id, defaulting malformed values to 1. */
TESTABLE int normaliseLfpColourGroupingId (
    int requestedId,
    int numberOfChoices);

/** Returns a valid channel-skip item id, defaulting malformed values to None. */
TESTABLE int normaliseLfpChannelSkipId (
    int requestedId,
    int numberOfChoices);

struct LfpOptionButtonAccessibilityState;
struct LfpTtlWordAccessibilityState;
class LfpTtlWordLabel;

/** A pause control with worker-safe UI Automation state and actions. */
class TESTABLE LfpPauseButton : public UtilityButton
{
public:
    explicit LfpPauseButton (String label);
    ~LfpPauseButton() override;

    /** Publishes the current state for accessibility providers. */
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
        LfpOptionButtonAccessibilityState>
        accessibilityState;
};

/** A worker-safe on/off control for display-only options. */
class TESTABLE LfpOptionToggleButton
    : public UtilityButton
{
public:
    explicit LfpOptionToggleButton (
        String label);
    ~LfpOptionToggleButton() override;

    /** Publishes the current state for accessibility providers. */
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
        LfpOptionButtonAccessibilityState>
        accessibilityState;
};

/** A channel-type radio control with worker-safe UI Automation state and actions. */
class TESTABLE LfpChannelTypeButton : public UtilityButton
{
public:
    explicit LfpChannelTypeButton (
        String label);
    ~LfpChannelTypeButton() override;

    /** Publishes the current state for accessibility providers. */
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
        LfpOptionButtonAccessibilityState>
        accessibilityState;
};

/**
 
    Holds the LfpDisplay UI controls
 
 */
class TESTABLE LfpDisplayOptions : public Component,
                                   public ComboBox::Listener,
                                   public Button::Listener,
                                   public Timer
{
public:
    /** Construtor */
    LfpDisplayOptions (LfpDisplayCanvas*,
                       LfpDisplaySplitter*,
                       LfpTimescale*,
                       LfpDisplay*,
                       LfpDisplayNode*);

    /** Destructor */
    ~LfpDisplayOptions();

    /** Paint background*/
    void paint (Graphics& g);

    /** Set sub-component locations*/
    void resized();

    /** Respond to combo box selection */
    void comboBoxChanged (ComboBox* cb);

    /** Respond to button clicks */
    void buttonClicked (Button* button);

    /** Set range selection combo box to correct value if it has been changed by scolling etc. */
    void setRangeSelection (float range, bool canvasMustUpdate = false);

    /** Set spread selection combo box to correct value if it has been changed by scolling etc. */
    void setSpreadSelection (int spread, bool canvasMustUpdate = false, bool deferDisplayRefresh = false);

    /** Changes the timebase value used by LfpTimescale and LfpDisplayCanvas. */
    void setTimebaseAndSelectionText (float timebase);

    /** Returns the selected channel height*/
    int getChannelHeight();

    /** Returns true if channel polarity is inverted */
    bool getInputInvertedState();

    /** Returns true if channel names should be shown*/
    bool getChannelNameState();

    /** Toggles pause button (e.g. if space bar is pressed) */
    void togglePauseButton (bool sendUpdate = true);

    /** Saves all options to XML */
    void saveParameters (XmlElement* xml);

    /** Loads options from XML */
    void loadParameters (XmlElement* xml);

    /** Saves the official numeric colour-grouping XML attribute. */
    void saveColourGroupingParameter (
        XmlElement& xmlNode) const;

    /** Restores and validates the official colour-grouping XML attribute. */
    void restoreColourGroupingParameter (
        const XmlElement& xmlNode);

    /** Returns the channel type of a given channel index */
    ContinuousChannel::Type getChannelType (int index);

    /** Returns the selected channel type for the range editor */
    ContinuousChannel::Type getSelectedType();

    /** Returns true if AUX channels are set to auto-scale */
    bool isAuxAutoScaleEnabled();

    /** Returns the name for a given channel type (DATA, AUX, ADC) */
    String getTypeName (ContinuousChannel::Type type);

    /** Returns the range step size for a given channel type (DATA, AUX, ADC) */
    int getRangeStep (ContinuousChannel::Type type);

    /** Set the selected channel type (DATA, AUX, ADC) */
    void setSelectedType (ContinuousChannel::Type type, bool toggleButton = true);

    /** Applies a colour-grouping item id and refreshes display and accessibility state. */
    void setColourGroupingSelection (
        int itemId);

    /** Applies a channel-skip item id and refreshes display and accessibility state. */
    void setChannelDisplaySkipSelection (
        int itemId);

    /** Sets whether channel order should be reversed */
    void setChannelsReversed (bool);

    /** Sets whether channels should be sorted by depth*/
    void setSortByDepth (bool);

    /** Sets whether signal polarity should be inverted */
    void setInputInverted (bool);

    /** Sets whether the median of each channel should be subtracted */
    void setMedianOffset (bool);

    /** Sets whether to use averaging in triggered display*/
    void setAveraging (bool);

    /** Sets whether channel numbers should be shown instead of names */
    void setShowChannelNumbers (bool);

    /** Publishes a non-zero TTL word from the acquisition thread. */
    void setTTLWord (uint64 word);

    /** Returns the visible event-overlay lines as the official 8-bit mask. */
    int getEventOverlayMask() const;

    /** Writes the official EventButtonState attribute. */
    void saveEventOverlayState (
        XmlElement& xml) const;

    /** Restores the official EventButtonState attribute and UIA snapshots. */
    void restoreEventOverlayState (
        const XmlElement& xml);

    /** Sets the state of the pause button and can enable / disable some options */
    void setPausedState (bool isPaused);

    void setShowHideOptionsButtonState (bool showOptions);

    /** Returns the laid-out advanced-options viewport height. */
    int getExtendedOptionsHeight()
        const;

    int selectedSpread;
    String selectedSpreadValue;

    int selectedTimebase;
    String selectedTimebaseValue;

    int selectedOverlap;
    String selectedOverlapValue;

    int selectedChannelDisplaySkip;
    String selectedChannelDisplaySkipValue;

    int selectedSpikeRasterThreshold;
    String selectedSpikeRasterThresholdValue;

    // this enum is a candidate option for refactoring, not used yet
    enum ChannelDisplaySkipValue
    {
        None = 0,
        One,
        Two,
        Four,
        Eight,
        Sixteen,
        ThirtyTwo
    } enum_selectedChannelDisplaySkipValue = None;

    float selectedSaturationValueFloat;

    void timerCallback();

private:
    void updateRangeAccessibilityMetadata();

    LfpDisplayCanvas* canvas;
    LfpDisplaySplitter* canvasSplit;
    LfpDisplay* lfpDisplay;
    LfpTimescale* timescale;
    LfpDisplayNode* processor;

    Colour labelColour;

    std::atomic<uint64>
        latestTtlWord { 0 };
    std::atomic<bool>
        hasLatestTtlWord { false };

    // Main options
    std::unique_ptr<Component> mainOptions;
    std::unique_ptr<Viewport> mainOptionsHolder;

    std::unique_ptr<
        MessageThreadComboBox>
        timebaseSelection;
    std::unique_ptr<Label> timebaseSelectionLabel;

    std::unique_ptr<
        MessageThreadComboBox>
        spreadSelection;
    std::unique_ptr<Label> spreadSelectionLabel;

    std::unique_ptr<
        MessageThreadComboBox>
        rangeSelection;
    std::unique_ptr<Label> rangeSelectionLabel;

    OwnedArray<UtilityButton> typeButtons;

    std::unique_ptr<ComboBox> overlapSelection; // what do we do with this?

    OwnedArray<EventDisplayInterface> eventDisplayInterfaces;
    std::unique_ptr<Label> overlayEventsLabel;

    std::unique_ptr<
        LfpTtlWordLabel>
        ttlWordLabel;
    std::unique_ptr<Label> ttlWordNameLabel;

    std::unique_ptr<LfpPauseButton> pauseButton;

    std::unique_ptr<ComboBox> colourSchemeOptionSelection;
    std::unique_ptr<Label> colourSchemeOptionLabel;

    std::unique_ptr<
        MessageThreadComboBox>
        colourGroupingSelection;
    std::unique_ptr<Label> colourGroupingLabel;

    std::unique_ptr<ShowHideOptionsButton> showHideOptionsButton;

    // EXTENDED OPTIONS
    std::unique_ptr<Component> extendedOptions;
    std::unique_ptr<Viewport> extendedOptionsHolder;

    // THRESHOLDS SECTION
    std::unique_ptr<GroupComponent> thresholdsGroup;

    std::unique_ptr<
        MessageThreadComboBox>
        spikeRasterSelection;
    std::unique_ptr<Label> spikeRasterabel;

    std::unique_ptr<ComboBox> saturationWarningSelection; // optionally raise hell if the actual data is saturating
    std::unique_ptr<Label> saturationWarningLabel;

    std::unique_ptr<
        MessageThreadComboBox>
        clipWarningSelection; // optinally draw (subtle) warning if data is clipped in display
    std::unique_ptr<Label> clipWarningLabel;

    // CHANNELS SECTION
    std::unique_ptr<GroupComponent> channelsGroup;

    std::unique_ptr<
        LfpOptionToggleButton>
        reverseChannelsDisplayButton;
    std::unique_ptr<Label> reverseChannelsLabel;

    std::unique_ptr<
        LfpOptionToggleButton>
        sortByDepthButton;
    std::unique_ptr<Label> sortByDepthLabel;

    std::unique_ptr<
        MessageThreadComboBox>
        channelDisplaySkipSelection;
    std::unique_ptr<Label> channelDisplaySkipLabel;

    std::unique_ptr<LfpOptionToggleButton> showChannelNumberButton;
    std::unique_ptr<Label> showChannelNumberLabel;

    // SIGNAL PROCESSING SECTION
    std::unique_ptr<GroupComponent> signalProcessingGroup;

    std::unique_ptr<LfpOptionToggleButton> medianOffsetPlottingButton;
    std::unique_ptr<Label> medianOffsetPlottingLabel;

    std::unique_ptr<LfpOptionToggleButton> invertInputButton;
    std::unique_ptr<Label> invertInputLabel;

    // TRIGGERED DISPLAY SECTION
    std::unique_ptr<GroupComponent> triggeredDisplayGroup;
    
    std::unique_ptr<ComboBox> triggerSourceSelection;
    std::unique_ptr<Label> triggerSourceLabel;

    std::unique_ptr<UtilityButton> averageSignalButton;
    std::unique_ptr<Label> averageSignalLabel;

    std::unique_ptr<UtilityButton> resetButton;

    Array<String> voltageRanges[CHANNEL_TYPES];
    Array<String> timebases;
    Array<String> spreads; // option for vertical spacing between channels
    Array<String> colourGroupings; // option for colouring every N channels the same
    Array<String> triggerSources; // option for trigger source event channel
    Array<String> overlaps; //
    Array<String> saturationThresholds; //default values for when different amplifiers saturate
    Array<String> clipThresholds;
    Array<String> spikeRasterSelectionOptions;
    Array<String> channelDisplaySkipOptions;
    Array<String> sectionTitles;

    ContinuousChannel::Type selectedChannelType;
    int selectedVoltageRange[CHANNEL_TYPES];
    String selectedVoltageRangeValues[CHANNEL_TYPES];
    float rangeGain[CHANNEL_TYPES];
    Array<String> rangeUnits;
    Array<String> typeNames;
    int rangeSteps[CHANNEL_TYPES];

    bool medianOffsetOnForSpikeRaster;
};

}; // namespace LfpViewer

#endif
