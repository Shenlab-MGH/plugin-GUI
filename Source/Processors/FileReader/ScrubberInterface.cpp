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

#include "ScrubberInterface.h"
#include "../../UI/SemanticComponent.h"

namespace
{
class WritableNormalizedValue final
    : public AccessibilityRangedNumericValueInterface
{
public:
    WritableNormalizedValue (
        std::function<double()> getValueIn,
        std::function<void (double)> setValueIn,
        std::function<double()> getStepIn)
        : getValue (std::move (getValueIn)),
          setValueCallback (std::move (setValueIn)),
          getStep (std::move (getStepIn))
    {
    }

    bool isReadOnly() const override { return false; }

    void setValue (double value) override
    {
        setValueCallback (
            jlimit (0.0, 1.0, value));
    }

    double getCurrentValue() const override
    {
        return jlimit (
            0.0,
            1.0,
            getValue());
    }

    AccessibleValueRange getRange() const override
    {
        return {
            { 0.0, 1.0 },
            jlimit (
                0.000001,
                1.0,
                getStep())
        };
    }

private:
    std::function<double()> getValue;
    std::function<void (double)> setValueCallback;
    std::function<double()> getStep;
};

class ScrubberTimeLabel final : public Label
{
public:
    using Label::Label;

    std::unique_ptr<AccessibilityHandler>
    createAccessibilityHandler() override
    {
        return createReadOnlyTextAccessibilityHandler (
            *this,
            [safeLabel =
                 Component::SafePointer<ScrubberTimeLabel> (
                     this)]
            {
                return safeLabel != nullptr
                           ? safeLabel->getText()
                           : String();
            });
    }
};

ScrubberInterface*
getScrubberInterfaceIfAvailable (
    FileReader* fileReader)
{
    if (fileReader == nullptr)
        return nullptr;

    if (auto* editor =
            dynamic_cast<FileReaderEditor*> (
                fileReader->getEditor()))
        return editor->getScrubberInterface();

    return nullptr;
}
} // namespace

void FullTimeline::paint (Graphics& g)
{
    /* Draw timeline background */
    int tickHeight = 4;
    int borderThickness = 1;
    g.setColour (findColour (ThemeColours::componentParentBackground));

    int numTicks = 5;

    for (int i = 0; i < numTicks; i++)
    {
        float dX = float (i) / numTicks * 450;

        Path tick;
        tick.startNewSubPath (dX, this->getHeight() - tickHeight);
        tick.lineTo (dX, this->getHeight());
        g.strokePath (tick, PathStrokeType (1.0));
    }

    g.fillRect (0, 0, this->getWidth(), this->getHeight() - tickHeight);
    g.setColour (findColour (ThemeColours::widgetBackground));
    g.fillRect (borderThickness, borderThickness, this->getWidth() - 2 * borderThickness, this->getHeight() - 2 * borderThickness - tickHeight);

    /* Draw a coloured vertical bar for each event */

    float sampleRate = fileReader->getCurrentSampleRate();
    int64 totalSamples = (stopMs - startMs) / 1000.0f * sampleRate;

    int64 startSample = startMs / 1000.0f * sampleRate;
    int64 stopSample = stopMs / 1000.0f * sampleRate;

    std::map<int, bool> eventMap; //keeps track of events that would get rendered at the same timeline position

    for (auto info : fileReader->getActiveEventInfo())
    {
        for (int i = 0; i < info.sampleNumbers.size(); i++)
        {
            int64 sampleNumber = info.sampleNumbers[i]; //TODO: Update EventInfo object name timestamps -> sampleNumbers
            int16 state = info.channelStates[i];

            if (state && sampleNumber >= startSample && sampleNumber <= stopSample)
            {
                float timelinePos = (sampleNumber - startSample) / float (totalSamples) * getWidth();

                //if timelinePos is already in eventMap, skip overlaying a new event to avoid bogging down the GUI while scrubbing
                if (eventMap.find (timelinePos) != eventMap.end())
                    continue;
                eventMap[timelinePos] = true;
                Colour c = eventChannelColours[info.channels[i] + 1];
                g.setColour (c);

                g.setOpacity (1.0f);
                g.fillRoundedRectangle (timelinePos, 0, 1, this->getHeight() - tickHeight, 0.2);
            }
        }
    }

    /* Draw the MAX_ZOOM_DURATION_IN_SECONDS interval */
    g.setColour (findColour (ThemeColours::componentParentBackground));
    g.setOpacity (0.8f);

    if (intervalStartPosition < 0)
        return;

    setIntervalPosition (intervalStartPosition);

    // Draw the scrubber interval bounds
    g.fillRoundedRectangle (intervalStartPosition, 0, 2, this->getHeight(), 2);
    g.fillRoundedRectangle (intervalStartPosition + intervalWidth, 0, 2, this->getHeight(), 2);

    // Draw the scrubber interval as a highlight
    g.setColour (findColour (ThemeColours::menuHighlightBackground));
    g.setOpacity (0.3);
    g.fillRect (intervalStartPosition + 1, 0, intervalWidth - 2, this->getHeight()-2);

    /* Draw the current playback position */
    g.setColour (findColour (ThemeColours::defaultText));
    float timelinePos = (float) (fileReader->getPlayheadPosition() - startSample) / totalSamples * getWidth();
    g.setOpacity (1.0f);
    g.fillRoundedRectangle (timelinePos, 0, 1, this->getHeight(), 0.2);

    // Update all time labels
    fileReader->getScrubberInterface()->updateTimeLabels();
}

void FullTimeline::setIntervalPosition (int pos)
{
    const auto totalTimeInSeconds =
        jmax (0.0, getIntervalDurationInSeconds());

    if (totalTimeInSeconds >= MAX_ZOOM_DURATION_IN_SECONDS)
        intervalWidth =
            roundToInt (
                MAX_ZOOM_DURATION_IN_SECONDS
                / totalTimeInSeconds
                * getWidth());
    else
        intervalWidth = getWidth();

    const auto maximumStartPosition =
        jmax (0, getWidth() - intervalWidth);
    const auto newPosition =
        jlimit (
            0,
            maximumStartPosition,
            pos);

    if (intervalStartPosition == newPosition)
        return;

    intervalStartPosition = newPosition;
    repaint();

    if (auto* handler =
            getAccessibilityHandler())
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
}

double FullTimeline::getIntervalDurationInSeconds()
{
    return ((stopMs - startMs) / 1000.0f);
}

double FullTimeline::getNormalizedPosition() const
{
    const auto maximumStartPosition =
        jmax (0, getWidth() - intervalWidth);

    return maximumStartPosition > 0
               ? static_cast<double> (
                     intervalStartPosition)
                     / maximumStartPosition
               : 0.0;
}

void FullTimeline::setNormalizedPosition (
    double position)
{
    setIntervalPosition (
        intervalStartPosition);

    const auto maximumStartPosition =
        jmax (0, getWidth() - intervalWidth);
    setIntervalPosition (
        roundToInt (
            jlimit (0.0, 1.0, position)
            * maximumStartPosition));

    if (auto* scrubber =
            getScrubberInterfaceIfAvailable (
                fileReader))
        scrubber->updateTimeLabels();

    if (auto* editor =
            fileReader->getEditor())
        editor->repaint();

    seekToIntervalStart();
}

std::unique_ptr<AccessibilityHandler>
FullTimeline::createAccessibilityHandler()
{
    return std::make_unique<AccessibilityHandler> (
        *this,
        AccessibilityRole::slider,
        AccessibilityActions {},
        AccessibilityHandler::Interfaces {
            std::make_unique<
                WritableNormalizedValue> (
                [safeTimeline =
                     Component::SafePointer<
                         FullTimeline> (this)]
                {
                    return safeTimeline != nullptr
                               ? safeTimeline
                                     ->getNormalizedPosition()
                               : 0.0;
                },
                [safeTimeline =
                     Component::SafePointer<
                         FullTimeline> (this)] (
                    double position)
                {
                    if (safeTimeline != nullptr)
                        safeTimeline
                            ->setNormalizedPosition (
                                position);
                },
                [safeTimeline =
                     Component::SafePointer<
                         FullTimeline> (this)]
                {
                    if (safeTimeline == nullptr)
                        return 1.0;

                    const auto maximumTravel =
                        jmax (
                            0,
                            safeTimeline->getWidth()
                                - safeTimeline
                                      ->getIntervalWidth());
                    return maximumTravel > 0
                               ? 1.0
                                     / maximumTravel
                               : 1.0;
                }) });
}

void FullTimeline::mouseDown (const MouseEvent& event)
{
    if (event.x >= intervalStartPosition && event.x <= intervalStartPosition + intervalWidth)
    {
        intervalIsSelected = true;
    }
}

void FullTimeline::mouseDrag (const MouseEvent& event)
{
    if (intervalIsSelected)
    {
        if (event.x >= intervalWidth / 2 && event.x < getWidth() - intervalWidth / 2)
            setIntervalPosition (
                event.x
                - intervalWidth / 2);
    }

    repaint();
    if (auto* scrubber =
            getScrubberInterfaceIfAvailable (
                fileReader))
        scrubber->updateTimeLabels();

    if (auto* editor =
            fileReader->getEditor())
        editor->repaint();
}

void FullTimeline::mouseUp (const MouseEvent& event)
{
    if (intervalIsSelected)
        seekToIntervalStart();

    intervalIsSelected = false;
}

void FullTimeline::seekToIntervalStart()
{
    if (fileReader == nullptr
        || getWidth() <= 0
        || fileReader
                   ->getCurrentNumTotalSamples()
               <= 0
        || fileReader->getCurrentSampleRate()
               <= 0.0f)
        return;

    const auto currentSample =
        static_cast<int64> (
            static_cast<double> (
                getStartInterval())
            / getWidth()
            * fileReader
                  ->getCurrentNumTotalSamples())
        + fileReader->getPlaybackStart();
    fileReader->setCurrentSample (
        currentSample);
}

void ZoomTimeline::setSliderPosition (
    int position)
{
    const auto maximumPosition =
        jmax (0, getWidth() - sliderWidth);
    const auto newPosition =
        jlimit (
            0,
            maximumPosition,
            position);

    if (sliderPosition == newPosition)
        return;

    sliderPosition =
        static_cast<float> (newPosition);
    repaint();

    if (auto* handler =
            getAccessibilityHandler())
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
}

double ZoomTimeline::getNormalizedPosition() const
{
    const auto maximumPosition =
        jmax (0, getWidth() - sliderWidth);

    return maximumPosition > 0
               ? sliderPosition
                     / maximumPosition
               : 0.0;
}

void ZoomTimeline::setNormalizedPosition (
    double position)
{
    const auto maximumPosition =
        jmax (0, getWidth() - sliderWidth);
    setSliderPosition (
        roundToInt (
            jlimit (0.0, 1.0, position)
            * maximumPosition));

    if (fileReader->getCurrentSampleRate()
            > 0.0f)
        if (auto* scrubber =
                getScrubberInterfaceIfAvailable (
                    fileReader))
            scrubber->setCurrentSample (
                getSliderPosition());
}

std::unique_ptr<AccessibilityHandler>
ZoomTimeline::createAccessibilityHandler()
{
    return std::make_unique<AccessibilityHandler> (
        *this,
        AccessibilityRole::slider,
        AccessibilityActions {},
        AccessibilityHandler::Interfaces {
            std::make_unique<
                WritableNormalizedValue> (
                [safeTimeline =
                     Component::SafePointer<
                         ZoomTimeline> (this)]
                {
                    return safeTimeline != nullptr
                               ? safeTimeline
                                     ->getNormalizedPosition()
                               : 0.0;
                },
                [safeTimeline =
                     Component::SafePointer<
                         ZoomTimeline> (this)] (
                    double position)
                {
                    if (safeTimeline != nullptr)
                        safeTimeline
                            ->setNormalizedPosition (
                                position);
                },
                [safeTimeline =
                     Component::SafePointer<
                         ZoomTimeline> (this)]
                {
                    if (safeTimeline == nullptr)
                        return 1.0;

                    const auto maximumTravel =
                        jmax (
                            0,
                            safeTimeline->getWidth()
                                - safeTimeline
                                      ->sliderWidth);
                    return maximumTravel > 0
                               ? 1.0
                                     / maximumTravel
                               : 1.0;
                }) });
}

void ZoomTimeline::paint (Graphics& g)
{
    /* Draw timeline background */
    int tickHeight = 4;
    int borderThickness = 1;

    g.setColour (findColour (ThemeColours::componentParentBackground));

    int numTicks = 7;

    for (int i = 0; i < numTicks; i++)
    {
        float dX = float (i) / numTicks * 420;

        Path tick;
        tick.startNewSubPath (dX, 0);
        tick.lineTo (dX, tickHeight);
        g.strokePath (tick, PathStrokeType (1.0));
    }

    g.fillRect (0, tickHeight, this->getWidth(), this->getHeight() - tickHeight);
    g.setColour (findColour (ThemeColours::widgetBackground));
    g.fillRect (borderThickness, tickHeight + borderThickness, this->getWidth() - 2 * borderThickness, this->getHeight() - 2 * borderThickness - tickHeight);

    float sampleRate = fileReader->getCurrentSampleRate();
    int64 totalSamples = (stopMs - startMs) / 1000.0f * sampleRate;
    int64 intervalSamples = totalSamples > MAX_ZOOM_DURATION_IN_SECONDS * sampleRate ? MAX_ZOOM_DURATION_IN_SECONDS * sampleRate : totalSamples;

    int intervalStartPos = fileReader->getScrubberInterface()->getFullTimelineStartPosition();

    int64 offset = float (intervalStartPos) / float (getWidth()) * totalSamples;

    int64 startSampleNumber = float (startMs) / 1000.0f * sampleRate + offset;
    int64 stopSampleNumber = startSampleNumber + intervalSamples;

    for (auto info : fileReader->getActiveEventInfo())
    {
        for (int i = 0; i < info.sampleNumbers.size(); i++)
        {
            int64 sampleNumber = info.sampleNumbers[i];
            int16 state = info.channelStates[i];

            if (state && sampleNumber >= startSampleNumber && sampleNumber <= stopSampleNumber)
            {
                float timelinePos = (sampleNumber - startSampleNumber) / float (intervalSamples) * getWidth();
                Colour c = eventChannelColours[info.channels[i] + 1];
                g.setColour (c);
                g.setOpacity (1.0f);
                g.fillRect (int (timelinePos), tickHeight, 1, this->getHeight() - tickHeight);
            }
        }
    }

    /* Draw the current playback position */
    g.setColour (findColour (ThemeColours::defaultText));
    float timelinePos = (float) (fileReader->getPlayheadPosition() - startSampleNumber) / (stopSampleNumber - startSampleNumber) * getWidth();
    if (0 < timelinePos < sliderPosition + sliderWidth)
    {
        g.setOpacity (1.0f);
        g.fillRoundedRectangle (timelinePos, 0, 1, this->getHeight(), 0.2);
    }

    /* Draw the scrubber interval */
    g.setColour (findColour (ThemeColours::componentParentBackground));
    g.fillRoundedRectangle (sliderPosition, 0, sliderWidth, this->getHeight(), 2);
    g.setColour (findColour (ThemeColours::componentBackground));
    g.setOpacity (0.8f);
    g.fillRoundedRectangle (sliderPosition + 1, 1, sliderWidth - 2, this->getHeight() - 2, 2);
}

void ZoomTimeline::mouseDown (const MouseEvent& event)
{
    if (event.x > sliderPosition && event.x < sliderPosition + sliderWidth)
    {
        sliderIsSelected = true;
    }
}

void ZoomTimeline::mouseDrag (const MouseEvent& event)
{
    if (sliderIsSelected)
    {
        setSliderPosition (
            event.x
            - sliderWidth / 2);
    }
    /*
    else if (rightSliderIsSelected)
    {
        if (event.x > leftSliderPosition + 1.5 * sliderWidth && event.x < getWidth() - sliderWidth / 2)
            rightSliderPosition = event.x - sliderWidth / 2;
    }
    else if (playbackRegionIsSelected)
    {
        if (leftSliderPosition >= 0 && rightSliderPosition <= getWidth() - sliderWidth)
        {
            if (event.x >= regionWidth / 2 + sliderWidth && event.x <= getWidth() - regionWidth / 2)
            {
                leftSliderPosition = event.x - regionWidth / 2 - sliderWidth;
                rightSliderPosition = event.x + regionWidth / 2 - sliderWidth;
            }
        }
    }
    */

    repaint();
}

void ZoomTimeline::mouseUp (const MouseEvent& event)
{
    if (sliderIsSelected)
    {
        if (auto* scrubber =
                getScrubberInterfaceIfAvailable (
                    fileReader))
            scrubber->setCurrentSample (
                getSliderPosition());
    }
    sliderIsSelected = false;
}

ScrubberInterface::ScrubberInterface (FileReader* fileReader_)
{
    fileReader = fileReader_;

    int scrubInterfaceWidth = 420;
    const auto scrubberId =
        createProcessorControlSemanticId (
            fileReader->getNodeId(),
            "scrubber");
    applySemanticMetadata (
        *this,
        scrubberId + ".panel",
        "File reader scrubber",
        "Inspect and navigate playback time in the loaded recording.");

    auto createTimeLabel =
        [&scrubberId] (
            const String& internalName,
            const String& initialText,
            const String& idSuffix,
            const String& title,
            const String& description)
    {
        auto label =
            std::make_unique<ScrubberTimeLabel> (
                internalName,
                initialText);
        applySemanticMetadata (
            *label,
            scrubberId + "." + idSuffix,
            title,
            description);
        label->setTooltip (description);
        return label;
    };

    zoomStartTimeLabel =
        createTimeLabel (
            "ZoomStartTime",
            "",
            "zoom_start",
            "Zoom window start",
            "Start time of the zoom timeline.");
    zoomStartTimeLabel->setBounds (0, 30, 100, 10);
    addAndMakeVisible (zoomStartTimeLabel.get());

    zoomMiddleTimeLabel =
        createTimeLabel (
            "ZoomMidTime",
            "",
            "zoom_playhead",
            "Zoom playhead position",
            "Current playhead position in the zoom timeline.");
    zoomMiddleTimeLabel->setBounds (0.39 * scrubInterfaceWidth, 30, 100, 10);
    addAndMakeVisible (zoomMiddleTimeLabel.get());

    //Compute zoom end time based on start/stop time from fileReader
    int64 startMs = fileReader->getPlaybackStart() / 1000.0f;
    int64 stopMs = fileReader->getPlaybackStop() / 1000.0f;
    int64 durationMs = stopMs - startMs;
    int64 intervalSamples = durationMs > 1000*MAX_ZOOM_DURATION_IN_SECONDS ? 1000*MAX_ZOOM_DURATION_IN_SECONDS : durationMs;
    int64 endMs = startMs + intervalSamples;

    TimeParameter::TimeValue duration = TimeParameter::TimeValue (durationMs);
    TimeParameter::TimeValue endTime = TimeParameter::TimeValue (endMs);

    zoomEndTimeLabel =
        createTimeLabel (
            "ZoomEndTime",
            duration.toString(),
            "zoom_end",
            "Zoom window end",
            "End time of the zoom timeline.");
    zoomEndTimeLabel->setBounds (0.75 * scrubInterfaceWidth, 30, 100, 10);
    addAndMakeVisible (zoomEndTimeLabel.get());

    fullStartTimeLabel =
        createTimeLabel (
            "FullStartTime",
            "",
            "playback_start",
            "Playback start",
            "Configured start time of playback.");
    fullStartTimeLabel->setBounds (0, 100, 100, 10);
    addAndMakeVisible (fullStartTimeLabel.get());

    minStartTimeLabel =
        createTimeLabel (
            "MinStartTime",
            "00:00:00.000",
            "minimum_start",
            "Minimum playback start",
            "Earliest available time in the recording.");
    minStartTimeLabel->setBounds (0, 115, 100, 10);
    minStartTimeLabel->setAlpha (0.5f);
    addAndMakeVisible (minStartTimeLabel.get());

    fullMiddleTimeLabel =
        createTimeLabel (
            "FullMidTime",
            "",
            "playback_position",
            "Playback position",
            "Current playback position in the recording.");
    fullMiddleTimeLabel->setBounds (0.39 * scrubInterfaceWidth, 108, 100, 10);
    fullMiddleTimeLabel->setAlpha (0.5f);
    addAndMakeVisible (fullMiddleTimeLabel.get());

    fullEndTimeLabel =
        createTimeLabel (
            "FullEndTime",
            "",
            "playback_end",
            "Playback end",
            "Configured end time of playback.");
    fullEndTimeLabel->setBounds (0.75 * scrubInterfaceWidth, 100, 100, 10);
    addAndMakeVisible (fullEndTimeLabel.get());

    maxEndTimeLabel =
        createTimeLabel (
            "MaxEndTime",
            "00:00:00.000",
            "maximum_end",
            "Maximum playback end",
            "Latest available time in the recording.");
    maxEndTimeLabel->setBounds (0.75 * scrubInterfaceWidth, 115, 100, 10);
    maxEndTimeLabel->setAlpha (0.5f);
    addAndMakeVisible (maxEndTimeLabel.get());

    int padding = 30;
    zoomTimeline = std::make_unique<ZoomTimeline> (fileReader);
    applySemanticMetadata (
        *zoomTimeline,
        scrubberId + ".zoom_timeline",
        "Playback position in zoom window",
        "Move the file-reader playhead within the current zoom window.");
    zoomTimeline->setBounds (padding, 46, scrubInterfaceWidth - 2 * padding, 20);
    addAndMakeVisible (zoomTimeline.get());

    fullTimeline = std::make_unique<FullTimeline> (fileReader);
    applySemanticMetadata (
        *fullTimeline,
        scrubberId + ".full_timeline",
        "Zoom window position",
        "Move the 30-second zoom window through the configured playback range.");
    fullTimeline->setBounds (padding, 76, scrubInterfaceWidth - 2 * padding, 20);
    addAndMakeVisible (fullTimeline.get());

    setVisible (false);
}

std::unique_ptr<AccessibilityHandler>
ScrubberInterface::createAccessibilityHandler()
{
    return std::make_unique<AccessibilityHandler> (
        *this,
        AccessibilityRole::group);
}

void ScrubberInterface::setCurrentSample (int zoomTimelinePos)
{
    const auto sampleRate =
        fileReader->getCurrentSampleRate();

    if (sampleRate <= 0.0f
        || fullTimeline->getWidth() <= 0
        || zoomTimeline->getWidth() <= 0)
        return;

    // Compute the new current sample number based on the full timeline and zoom timeline slider positions
    int64 totalSamples = fullTimeline->getIntervalDurationInSeconds() * sampleRate;
    TimeParameter* start = static_cast<TimeParameter*> (fileReader->getParameter ("start_time"));
    int64 newCurrentSample = start->getTimeValue()->getTimeInMilliseconds() / 1000.0f * sampleRate;
    newCurrentSample += float (getFullTimelineStartPosition()) / fullTimeline->getWidth() * totalSamples;
    float totalTimeInSeconds = float (totalSamples) / sampleRate;
    float intervalWidth = totalTimeInSeconds >= MAX_ZOOM_DURATION_IN_SECONDS ? MAX_ZOOM_DURATION_IN_SECONDS : totalTimeInSeconds;
    newCurrentSample += float (zoomTimelinePos) / zoomTimeline->getWidth() * intervalWidth * sampleRate;
    fileReader->setCurrentSample (newCurrentSample);
}

void ScrubberInterface::updatePlaybackTimes()
{
    const auto sampleRate =
        fileReader->getCurrentSampleRate();

    if (sampleRate <= 0.0f
        || fullTimeline->getWidth() <= 0
        || zoomTimeline->getWidth() <= 0)
        return;

    int64 totalSamples = fullTimeline->getIntervalDurationInSeconds() * sampleRate;

    TimeParameter* start = static_cast<TimeParameter*> (fileReader->getParameter ("start_time"));

    int64 newStartSample = start->getTimeValue()->getTimeInMilliseconds() / 1000.0f * sampleRate;
    newStartSample += float (getFullTimelineStartPosition()) / fullTimeline->getWidth() * totalSamples;
    float totalTimeInSeconds = float (totalSamples) / sampleRate;
    float intervalWidth = totalTimeInSeconds >= MAX_ZOOM_DURATION_IN_SECONDS ? MAX_ZOOM_DURATION_IN_SECONDS : totalTimeInSeconds;
    newStartSample += float (getZoomTimelineStartPosition()) / zoomTimeline->getWidth() * intervalWidth;
    fileReader->setPlaybackStart (newStartSample);
}

void ScrubberInterface::paintOverChildren (Graphics& g)
{
    int leftRay = fullTimeline->getStartInterval();
    int rightRay = leftRay + fullTimeline->getIntervalWidth();

    g.drawLine (zoomTimeline->getX(), zoomTimeline->getY() + zoomTimeline->getHeight(), zoomTimeline->getX() + leftRay, fullTimeline->getY());
    g.drawLine (zoomTimeline->getX() + zoomTimeline->getWidth(), zoomTimeline->getY() + zoomTimeline->getHeight(), zoomTimeline->getX() + rightRay, fullTimeline->getY());
}

void ScrubberInterface::buttonClicked (Button* button)
{
    //playbackButton->setState (! playbackButton->getState()); //deprecated
    updatePlaybackTimes();
    fileReader->togglePlayback();
}

int ScrubberInterface::getFullTimelineStartPosition()
{
    return fullTimeline->getStartInterval();
}

int ScrubberInterface::getZoomTimelineStartPosition()
{
    return zoomTimeline->getStartInterval();
}

void ScrubberInterface::update()
{
    TimeParameter* start = static_cast<TimeParameter*> (fileReader->getParameter ("start_time"));
    fullStartTimeLabel->setText (start->getTimeValue()->toString(), juce::sendNotificationAsync);
    int minStartTime = start->getTimeValue()->getMinTimeInMilliseconds();
    minStartTimeLabel->setText (TimeParameter::TimeValue (minStartTime).toString(), juce::sendNotificationAsync);

    TimeParameter* stop = static_cast<TimeParameter*> (fileReader->getParameter ("end_time"));
    fullEndTimeLabel->setText (stop->getTimeValue()->toString(), juce::sendNotificationAsync);
    int maxEndTime = stop->getTimeValue()->getMaxTimeInMilliseconds();
    maxEndTimeLabel->setText (TimeParameter::TimeValue (maxEndTime).toString(), juce::sendNotificationAsync);

    int startMs = start->getTimeValue()->getTimeInMilliseconds();
    int stopMs = stop->getTimeValue()->getTimeInMilliseconds();
    int duration = stopMs - startMs;

    fullTimeline->setStartStopTimes (startMs, stopMs);
    zoomTimeline->setStartStopTimes (startMs, stopMs);

    FileReaderEditor* e = static_cast<FileReaderEditor*> (fileReader->getEditor());


    e->repaint();
}

void ScrubberInterface::updateTimeLabels()
{
    int start = ((TimeParameter*) fileReader->getParameter ("start_time"))->getTimeValue()->getTimeInMilliseconds();
    int stop = ((TimeParameter*) fileReader->getParameter ("end_time"))->getTimeValue()->getTimeInMilliseconds();

    int duration = (stop - start);

    int startPos = fullTimeline->getStartInterval();
    float frac = float (startPos) / float (fullTimeline->getWidth());

    // Update zoom start time label from fullTimeline slider position
    int pos = fullTimeline->getStartInterval();
    float startTime = float(pos)/float(fullTimeline->getWidth()) * duration;
    TimeParameter::TimeValue time = TimeParameter::TimeValue (start + startTime);
    zoomStartTimeLabel->setText (time.toString(), juce::sendNotificationAsync);
    int64 zoomStartTime = time.getTimeInMilliseconds();

    // Update zoom end time label
    int64 zoomEndTime;
    if (duration >= 1000*MAX_ZOOM_DURATION_IN_SECONDS)
    {
        TimeParameter::TimeValue time = TimeParameter::TimeValue (start + frac * duration + 1000*MAX_ZOOM_DURATION_IN_SECONDS);
        zoomEndTime = time.getTimeInMilliseconds();
        zoomEndTimeLabel->setText (time.toString(), juce::sendNotificationAsync);
    }
    else
    {
        TimeParameter::TimeValue time = TimeParameter::TimeValue (start + frac * duration + (stop - start));
        zoomEndTime = time.getTimeInMilliseconds();
        zoomEndTimeLabel->setText (time.toString(), juce::sendNotificationAsync);
    }

    // Update zoom middle time label
    int currentPos = ((ZoomTimeline*)zoomTimeline.get())->getSliderPosition();
    int64 zoomMiddleTime = zoomStartTime + float(currentPos) / zoomTimeline->getWidth() * (zoomEndTime - zoomStartTime);
    TimeParameter::TimeValue middleTime = TimeParameter::TimeValue (zoomMiddleTime);
    zoomMiddleTimeLabel->setText (middleTime.toString(), juce::sendNotificationAsync);

    // Get current playhead time from currentSample
    float sampleRate = fileReader->getCurrentSampleRate();
    int64 currentSample = fileReader->getPlayheadPosition();
    float currentTime =
        sampleRate > 0.0f
            ? currentSample / sampleRate
                  * 1000.0f
            : static_cast<float> (start);
    TimeParameter::TimeValue currentTimeValue = TimeParameter::TimeValue (currentTime);
    fullMiddleTimeLabel->setText (currentTimeValue.toString(), juce::sendNotificationAsync);
}
