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

#include "RecordNodeEditor.h"
#include "../../UI/SemanticComponent.h"
#include "../../CoreServices.h"
#include "RecordNode.h"
#include <stdio.h>

namespace
{
String getParameterSemanticId (Parameter& parameter)
{
    return "oe.parameter."
           + sanitiseSemanticSegment (
               String (parameter.getKey()));
}

String getParameterSemanticTitle (
    Parameter& parameter)
{
    return parameter.getDisplayName().isNotEmpty()
               ? parameter.getDisplayName()
               : parameter.getName().replace ("_", " ");
}
} // namespace

SyncMonitor::SyncMonitor()
{
    setInterceptsMouseClicks (false, false);
}

SyncMonitor::~SyncMonitor()
{
}

void SyncMonitor::setSyncMetric (bool isSynchronized_, float syncMetric_)
{
    isSynchronized = isSynchronized_;
    metric = syncMetric_;

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (AccessibilityEvent::valueChanged);
}

void SyncMonitor::setEnabled (bool state)
{
    isEnabled = state;

    repaint();
}

void SyncMonitor::setAccessibilityContext (StringRef semanticId,
                                           StringRef title,
                                           StringRef description)
{
    applySemanticMetadata (*this, semanticId, title, description);
}

std::unique_ptr<AccessibilityHandler>
SyncMonitor::createAccessibilityHandler()
{
    return createReadOnlyTextAccessibilityHandler (
        *this,
        [this]
        { return getAccessibleValue(); });
}

LastSyncEventMonitor::LastSyncEventMonitor()
{
    setAccessibilityContext (
        "oe.status.last_sync_event",
        "Last synchronization event",
        "Approximate time since the last synchronization event.");
}

String LastSyncEventMonitor::getAccessibleValue() const
{
    if (! isSynchronized || metric == -1.0f)
        return "--";

    if (metric <= 60.0f)
        return "<1 min";

    if (metric < 60.0f * 5.0f)
        return ">1 min";

    return ">5 min";
}

void LastSyncEventMonitor::paint (Graphics& g)
{
    g.setFont (FontOptions ("Fira Sans", "SemiBold", 12));

    if (isSynchronized && metric != -1.0)
    {
        if (metric <= 60)
        {
            g.setColour (Colours::green);
            g.drawText ("<1 min", 0, 0, 55, 20, Justification::centred);
        }

        else if (metric > 60 && metric < 60 * 5)
        {
            g.setColour (Colours::orange);
            g.drawText (">1 min", 0, 0, 55, 20, Justification::centred);
        }

        else
        {
            g.setColour (Colours::red);
            g.drawText (">5 min", 0, 0, 55, 20, Justification::centred);
        }
    }
    else
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.5f));
        g.drawText ("--", 0, 0, 55, 20, Justification::centred);
    }
}

SyncStartTimeMonitor::SyncStartTimeMonitor()
{
    setAccessibilityContext (
        "oe.status.sync_start_offset",
        "Synchronization start offset",
        "Offset between this stream and the main synchronization stream.");
}

String SyncStartTimeMonitor::getAccessibleValue() const
{
    return isSynchronized
               ? String (metric, 2) + " ms"
               : String ("--");
}

void SyncStartTimeMonitor::paint (Graphics& g)
{
    g.setFont (FontOptions ("Fira Sans", "SemiBold", 12));

    if (isSynchronized)
    {
        g.setColour (findColour (ThemeColours::defaultText));
        g.drawText (String (metric, 2) + " ms", 0, 0, 50, 20, Justification::centred);
    }

    else
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.5f));
        g.drawText ("--", 0, 0, 50, 20, Justification::centred);
    }
}

SyncAccuracyMonitor::SyncAccuracyMonitor()
{
    setAccessibilityContext (
        "oe.status.sync_accuracy",
        "Synchronization accuracy",
        "Discrepancy between actual and expected synchronization event times.");
}

String SyncAccuracyMonitor::getAccessibleValue() const
{
    return isSynchronized
               ? String (std::abs (metric), 3) + " ms"
               : String ("--");
}

void SyncAccuracyMonitor::paint (Graphics& g)
{
    g.setFont (FontOptions ("Fira Sans", "SemiBold", 12));

    if (isSynchronized)
    {
        g.setColour (findColour (ThemeColours::defaultText));
        g.drawText (String (std::abs (metric), 3) + " ms", 0, 0, 55, 20, Justification::centred);
    }

    else
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.5f));
        g.drawText ("--", 0, 0, 55, 20, Justification::centred);
    }
}

StreamMonitor::StreamMonitor (RecordNode* rn, uint64 id)
    : LevelMonitor (rn),
      streamId (id)
{
    selectedChannels = 0;
    totalChannels = rn->getDataStream (streamId)->getChannelCount();

    startTimerHz (10);
}

StreamMonitor::~StreamMonitor() {}

std::unique_ptr<AccessibilityHandler>
StreamMonitor::createAccessibilityHandler()
{
    return createReadOnlyButtonTextAccessibilityHandler (
        *this,
        [safeMonitor =
             Component::SafePointer<StreamMonitor> (
                 this)]
        {
            return safeMonitor != nullptr
                       ? safeMonitor->getAccessibleValue()
                       : String();
        });
}

String StreamMonitor::getAccessibleValue() const
{
    const auto fifoPercent =
        roundToInt (
            jlimit (0.0f,
                    1.0f,
                    fillPercentage)
            * 100.0f);

    return String (selectedChannels)
           + " of " + String (totalChannels)
           + " channels selected; FIFO "
           + String (fifoPercent) + "%";
}

void StreamMonitor::timerCallback()
{
    if (((RecordNode*) processor)->recordThread->isThreadRunning())
        setFillPercentage (((RecordNode*) processor)->fifoUsage[streamId]);
    else
        setFillPercentage (0.0);
}

void StreamMonitor::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    // Draw the monitor
    LevelMonitor::paintButton (g, isMouseOver, isButtonDown);

    // Draw the channel count text
    auto textArea = getLocalBounds().reduced (2);

    auto t = AffineTransform::rotation (-MathConstants<float>::halfPi,
                                        textArea.toFloat().getCentreX(),
                                        textArea.toFloat().getCentreY());

    auto newTextArea = textArea.toFloat().transformedBy (t);

    g.addTransform (t);

    if (selectedChannels == 0)
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.5f));
    else
        g.setColour (findColour (ThemeColours::defaultText));

    g.setFont (FontOptions ("Inter", "Regular", 12.0f));
    g.drawText (String (selectedChannels) + "/" + String (totalChannels), newTextArea, Justification::centred);
}

void StreamMonitor::updateChannelCount (int selected)
{
    if (selectedChannels == selected)
        return;

    selectedChannels = selected;
    repaint();

    if (auto* handler =
            getAccessibilityHandler())
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
}

DiskMonitor::DiskMonitor (RecordNode* rn)
    : LevelMonitor (rn),
      lastFreeSpace (0.0),
      recordingTimeLeftInSeconds (0),
      dataRate (0.0)
{
    rn->getDiskSpaceChecker()->addListener (this);
    startTimerHz (1);
}

std::unique_ptr<AccessibilityHandler> DiskMonitor::createAccessibilityHandler()
{
    return createReadOnlyProgressAccessibilityHandler (
        *this,
        [this] { return static_cast<double> (fillPercentage); });
}

DiskMonitor::~DiskMonitor()
{
    if (((RecordNode*) processor)->getDiskSpaceChecker() != nullptr)
        ((RecordNode*) processor)->getDiskSpaceChecker()->removeListener (this);
}

void DiskMonitor::update (float dataRate, int64 bytesFree, float timeLeft)
{
    if (((RecordNode*) processor)->recordThread->isThreadRunning())
    {
        String msg = String (bytesFree / pow (2, 30)) + " GB available\n";
        msg += String (int (timeLeft / 60.0f)) + " minutes remaining\n";
        msg += "Data rate: " + String (dataRate * 1000 / pow (2, 20), 2) + " MB/s";
        setTooltip (msg);
    }
    else
    {
        setTooltip (String (bytesFree / pow (2, 30)) + " GB available");
    }
}

void DiskMonitor::updateDiskSpace (float percentage)
{
    setFillPercentage (1.0f - percentage);
}

void DiskMonitor::directoryInvalid (bool recordingStopped)
{
    setFillPercentage (1.0);
    setTooltip ("Invalid directory");

    if (recordingStopped)
    {
        String msg = "Record Node (" + String (processor->getNodeId()) + ") - The selected recording path doesn't exist anymore.";
        msg += "\n\nPlease select a valid directory and restart the recording.";
        AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Recording Stopped", msg);
    }
}

void DiskMonitor::lowDiskSpace()
{
    String msg = "Recording stopped. Less than 5 minutes of disk space remaining.";
    AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "WARNING", msg);
}

void DiskMonitor::timerCallback()
{
    repaint();
}

RecordChannelsParameterEditor::RecordChannelsParameterEditor (RecordNode* rn, Parameter* param, int rowHeightPixels, int rowWidthPixels)
    : ParameterEditor (param),
      recordNode (rn)
{
    int numChannels = int (((MaskChannelsParameter*) param)->getChannelStates().size());
    int selected = 0;
    for (auto chan : ((MaskChannelsParameter*) param)->getChannelStates())
        if (chan)
            selected++;

    uint64 streamId = ((DataStream*) param->getOwner())->getStreamId();
    String streamName = ((DataStream*) param->getOwner())->getName();
    String sourceNodeId = String (((DataStream*) param->getOwner())->getSourceNodeId());

    monitor = std::make_unique<StreamMonitor> (recordNode, streamId);
    monitor->setTooltip (sourceNodeId + " | " + streamName);
    applySemanticMetadata (
        *monitor,
        "oe.parameter." + sanitiseSemanticSegment (String (param->getKey())),
        "Recording channels for " + streamName,
        param->getDescription());
    monitor->addListener (this);
    monitor->setBounds (0, 0, 15, 73);
    addAndMakeVisible (monitor.get());

    setBounds (0, 0, 15, 73);

    editor = monitor.get();

    updateView();
};

void RecordChannelsParameterEditor::channelStateChanged (Array<int> newChannels)
{
    Array<var> newArray;

    for (int i = 0; i < newChannels.size(); i++)
        newArray.add (newChannels[i]);

    param->setNextValue (newArray);

    updateView();
}

int RecordChannelsParameterEditor::getChannelCount()
{
    return ((MaskChannelsParameter*) param)->getChannelCount();
}

Array<int> RecordChannelsParameterEditor::getSelectedChannels()
{
    MaskChannelsParameter* p = (MaskChannelsParameter*) param;
    std::vector<bool> channelStates = p->getChannelStates();

    Array<int> selectedChannels;

    for (int i = 0; i < channelStates.size(); i++)
        if (channelStates[i])
            selectedChannels.add (i);

    return selectedChannels;
}

void RecordChannelsParameterEditor::buttonClicked (Button* label)
{
    if (param == nullptr)
        return;

    MaskChannelsParameter* p = (MaskChannelsParameter*) param;

    Array<String> channelNames;
    String popupTitle;

    if (p->getOwner()->getType() == ParameterOwner::DATASTREAM)
    {
        DataStream* stream = (DataStream*) p->getOwner();
        popupTitle = String (stream->getSourceNodeId()) + " " + stream->getSourceNodeName() + " - " + stream->getName();

        for (auto channel : stream->getContinuousChannels())
            channelNames.add (channel->getName());
    }

    std::vector<bool> channelStates = p->getChannelStates();

    auto* channelSelector = new PopupChannelSelector (label, this, channelStates, channelNames, popupTitle);

    channelSelector->setChannelButtonColour (param->getColour());

    CoreServices::getPopupManager()->showPopup (std::unique_ptr<PopupComponent> (channelSelector), monitor.get());
}

void RecordChannelsParameterEditor::updateView()
{
    /* Update channel count in StreamMonitor */

    if (param != nullptr)
    {
        monitor->updateChannelCount (param->currentValue.getArray()->size());
    }
}

void RecordChannelsParameterEditor::resized()
{
    updateBounds();
}

RecordToggleButton::RecordToggleButton (const String& name) : CustomToggleButton() {}

RecordToggleButton::~RecordToggleButton() {}

void RecordToggleButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    float alpha = 1.0f;

    if (! isEnabled())
    {
        alpha = 0.5f;
    }

    g.setColour (Colour (0, 0, 0).withAlpha (alpha));
    g.fillRoundedRectangle (0, 0, getWidth(), getHeight(), 4);

    if (! getToggleState())
    {
        g.setColour (findColour (ThemeColours::widgetBackground).withAlpha (alpha));
    }
    else
    {
        g.setColour (Colour (255, 0, 0).withAlpha (alpha));
    }

    g.fillRoundedRectangle (1, 1, getWidth() - 2, getHeight() - 2, 3);

    g.setColour (Colour (0, 0, 0).withAlpha (alpha));
    g.fillEllipse (0.35 * getWidth(), 0.35 * getHeight(), 0.3 * getWidth(), 0.3 * getHeight());
}

RecordToggleParameterEditor::RecordToggleParameterEditor (Parameter* param) : ParameterEditor (param)
{
    label = std::make_unique<Label> ("Parameter name", param->getDisplayName());
    label->setFont (FontOptions ("Inter", "Regular", 13.0f));
    label->setAccessible (false);
    addAndMakeVisible (label.get());

    toggleButton = std::make_unique<RecordToggleButton> (param->getDisplayName()); //param->getDisplayName());
    toggleButton->setClickingTogglesState (true);
    toggleButton->setTooltip ("Toggle recording state on/off");
    toggleButton->addListener (this);
    toggleButton->setToggleState (true, dontSendNotification);
    toggleButton->setBounds (50, 0, 15, 15);
    applySemanticMetadata (
        *toggleButton,
        getParameterSemanticId (*param),
        getParameterSemanticTitle (*param),
        param->getDescription());
    addAndMakeVisible (toggleButton.get());

    label->setBounds (0, 0, 100, 15);
    toggleButton->setBounds (95, 0, 15, 15);

    setBounds (0, 0, 150, 20);

    editor = toggleButton.get();
}

void RecordToggleParameterEditor::buttonClicked (Button*)
{
    param->setNextValue (toggleButton->getToggleState());
}

void RecordToggleParameterEditor::updateView()
{
    if (param != nullptr)
        toggleButton->setToggleState (param->getValue(), dontSendNotification);
}

void RecordToggleParameterEditor::resized()
{
}

void ClearButton::paintButton (Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    g.fillAll (findColour (ThemeColours::widgetBackground).contrasting (0.05f));

    int xoffset = (getWidth() - 14) / 2;
    int yoffset = (getHeight() - 14) / 2;

    if (isMouseOverButton)
    {
        g.setColour (findColour (ThemeColours::defaultFill).withAlpha (0.5f));
        g.fillRoundedRectangle (xoffset, yoffset, 14, 14, 4.0f);
    }

    g.setColour (findColour (ThemeColours::defaultText));

    Path path;
    path.addLineSegment (Line<float> (2, 2, 8, 8), 0.0f);
    path.addLineSegment (Line<float> (2, 8, 8, 2), 0.0f);

    path.applyTransform (AffineTransform::translation (xoffset + 2, yoffset + 2));

    g.strokePath (path, PathStrokeType (1.0f));
}

std::unique_ptr<AccessibilityHandler>
ClearButton::createAccessibilityHandler()
{
    return createReadOnlyButtonTextAccessibilityHandler (
        *this,
        [] { return String ("default"); });
}

RecordPathParameterEditor::RecordPathParameterEditor (Parameter* param, int rowHeightPixels, int rowWidthPixels) : ParameterEditor (param)
{
    jassert (param->getType() == Parameter::PATH_PARAM);

    setBounds (0, 0, rowWidthPixels, rowHeightPixels);

    button =
        std::make_unique<ReadOnlyValueTextButton> (
            "Browse");
    button->setName (param->getKey());
    button->addListener (this);
    button->setClickingTogglesState (false);
    button->setTooltip (param->getValueAsString());
    button->addMouseListener (this, true);
    applySemanticMetadata (
        *button,
        getParameterSemanticId (*param),
        getParameterSemanticTitle (*param),
        param->getDescription());
    addAndMakeVisible (button.get());

    label = std::make_unique<Label> ("Parameter name", param->getDisplayName()); // == "" ? param->getName().replace("_", " ") : param->getDisplayName());
    Font labelFont = FontOptions ("Inter", "Regular", int (0.75 * rowHeightPixels));
    label->setFont (labelFont);
    label->setJustificationType (Justification::left);
    label->setAccessible (false);
    addAndMakeVisible (label.get());

    clearButton = std::make_unique<ClearButton>();
    clearButton->addListener (this);
    clearButton->addMouseListener (this, true);
    clearButton->setTooltip ("Set to default");
    applySemanticMetadata (
        *clearButton,
        getParameterSemanticId (*param)
            + ".use_default",
        "Use default recording directory",
        "Remove this Record Node directory override.");
    addChildComponent (clearButton.get());

    int width = rowWidthPixels;

    label->setBounds (width / 2 + 4, 0, width / 2, rowHeightPixels);
    button->setBounds (0, 0, width / 2, rowHeightPixels);
    clearButton->setBounds ((width) / 2 - rowHeightPixels - 1, 1, rowHeightPixels - 2, rowHeightPixels - 2);

    editor = (Component*) button.get();
}

void RecordPathParameterEditor::buttonClicked (Button* button_)
{
    if (button_ == button.get())
    {
        bool isDirectory = ((PathParameter*) param)->getIsDirectory();
        String dialogBoxTitle;
        String validFilePatterns;

        if (param->getDescription().isEmpty())
            dialogBoxTitle = "Select a " + String (isDirectory ? "directory" : "file");
        else
            dialogBoxTitle = param->getDescription();

        if (! isDirectory)
            validFilePatterns = "*." + ((PathParameter*) param)->getValidFilePatterns().joinIntoString (";*.");

        FileChooser chooser (dialogBoxTitle, File(), validFilePatterns);

        bool success = isDirectory ? chooser.browseForDirectory() : chooser.browseForFileToOpen();
        if (success)
        {
            File file = chooser.getResult();
            param->setNextValue (file.getFullPathName());
            updateView();
        }
    }
    else if (button_ == clearButton.get())
    {
        param->setNextValue ("None");
        clearButton->setVisible (false);
        updateView();
    }
}

void RecordPathParameterEditor::updateView()
{
    if (param == nullptr)
        button->setEnabled (false);
    else
        button->setEnabled (true);

    if (param)
    {
        String value = param->getValueAsString();
        const String accessibleValue =
            value == "None" ? "default" : value;
        setButtonTextWithAccessibilityValue (
            *button,
            accessibleValue);
        clearButton->setVisible (
            value != "None" && button->isEnabled());
        if (! ((PathParameter*) param)->isValid())
        {
            button->setColour (TextButton::textColourOnId, Colours::red);
            button->setColour (TextButton::textColourOffId, Colours::red);
        }
        else
        {
            // Remove the red colour if it was previously set
            button->removeColour (TextButton::textColourOnId);
            button->removeColour (TextButton::textColourOffId);
        }
        //Alternatively:
        //button->setButtonText(File(param->getValueAsString()).getFileName());
        if (value == "None")
        {
            button->setTooltip ("Override default path");
        }
        else
        {
            button->setTooltip (value);
        }
    }
}

void RecordPathParameterEditor::resized()
{
    updateBounds();

    if (button != nullptr)
    {
        clearButton->setBounds (button->getRight() - button->getHeight(),
                                button->getY() + 1,
                                button->getHeight() - 2,
                                button->getHeight() - 2);
    }
}

void RecordPathParameterEditor::mouseEnter (const MouseEvent& event)
{
    if (param->getValueAsString() != "None" && button->isEnabled())
    {
        clearButton->setVisible (true);
    }
}

void RecordPathParameterEditor::mouseExit (const MouseEvent& event)
{
    updateView();
}

RecordNodeEditor::RecordNodeEditor (RecordNode* parentNode)
    : GenericEditor (parentNode)
{
    desiredWidth = 165;

    recordNode = parentNode;

    fifoDrawerButton = std::make_unique<FifoDrawerButton> (
        getNameAndId() + " Fifo Drawer Button",
        createProcessorControlSemanticId (parentNode->getNodeId(), "fifo_drawer"),
        "Record node FIFO monitors",
        "Show or hide recording FIFO usage monitors.");
    fifoDrawerButton->setBounds (4, 40, 10, 78);
    fifoDrawerButton->addListener (this);
    addAndMakeVisible (fifoDrawerButton.get());

    diskMonitor = std::make_unique<DiskMonitor> (recordNode);
    applySemanticMetadata (
        *diskMonitor,
        createProcessorControlSemanticId (parentNode->getNodeId(), "disk_usage"),
        "Record node disk usage",
        "Fraction of recording-volume space currently used by this record node.");
    diskMonitor->setBounds (18, 33, 15, 92);
    addAndMakeVisible (diskMonitor.get());

    addCustomParameterEditor (new RecordPathParameterEditor (parentNode->getParameter ("directory")), 42, 32);
    addComboBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "engine", 42, 57);

    for (auto& p : { "directory", "engine" })
    {
        auto* ed = getParameterEditor (p);
        ed->setLayout (ParameterEditor::Layout::nameHidden);
        ed->setBounds (ed->getX(), ed->getY(), 110, ed->getHeight());
    }

    addCustomParameterEditor (new RecordToggleParameterEditor (parentNode->getParameter ("events")), 40, 85);
    addCustomParameterEditor (new RecordToggleParameterEditor (parentNode->getParameter ("spikes")), 40, 107);

    // Show the fifo monitors by default
    fifoDrawerButton->triggerClick();
}

void RecordNodeEditor::timerCallback()
{
    fifoDrawerButton->triggerClick();
    stopTimer();
}

void RecordNodeEditor::updateSyncMonitors()
{
    for (auto stream : getProcessor()->getDataStreams())
    {
        syncStartTimeMonitors[stream->getStreamId()] = streamSelector->getSyncStartTimeMonitor (stream);
        syncAccuracyMonitors[stream->getStreamId()] = streamSelector->getSyncAccuracyMonitor (stream);
        lastSyncEventMonitors[stream->getStreamId()] = streamSelector->getlastSyncEventMonitor (stream);
    }

    if (recordNode != nullptr)
        recordNode->updateSyncMonitors();
}

void RecordNodeEditor::updateFifoMonitors()
{
    auto removeExistingMonitors = [this] (std::vector<ParameterEditor*>& monitors)
    {
        for (auto& monitor : monitors)
        {
            for (auto& ed : this->parameterEditors)
            {
                if (ed == monitor)
                {
                    removeChildComponent (monitor);
                    parameterEditors.removeObject (monitor);
                }
            }
        }
        monitors.clear();
    };

    removeExistingMonitors (streamMonitors);
    removeExistingMonitors (syncMonitors);

    int streamCount = 0;
    for (auto& stream : recordNode->getDataStreams())
    {
        uint64 streamId = stream->getStreamId();

        // Add a recording channel selector for each stream
        Parameter* channels = recordNode->getDataStream (streamId)->getParameter ("channels");
        recordNode->getDataStream (streamId)->setColour ("channels", getLookAndFeel().findColour (ProcessorColour::IDs::RECORD_COLOUR));
        addCustomParameterEditor (new RecordChannelsParameterEditor (recordNode, channels), 18 + streamCount * 20, 32);
        parameterEditors.getLast()->setVisible (! getCollapsedState());
        parameterEditors.getLast()->disableUpdateOnSelectedStreamChanged();
        streamMonitors.push_back (parameterEditors.getLast());

        // Add a sync line selector for each stream
        Parameter* syncLineParam = recordNode->getDataStream (streamId)->getParameter ("sync_line");
        Parameter* mainSyncParam = recordNode->getParameter ("main_sync");
        addSyncLineParameterEditor ((TtlLineParameter*) syncLineParam, (SelectedStreamParameter*) mainSyncParam, 18 + streamCount * 20, 110);
        parameterEditors.getLast()->setVisible (! getCollapsedState());
        parameterEditors.getLast()->disableUpdateOnSelectedStreamChanged();
        syncMonitors.push_back (parameterEditors.getLast());

        streamCount++;
    }
}

void RecordNodeEditor::collapsedStateChanged()
{
    if (getCollapsedState())
    {
        for (auto monitor : streamMonitors)
            monitor->setVisible (false);
        for (auto monitor : syncMonitors)
            monitor->setVisible (false);
    }
    else
    {
        for (auto monitor : streamMonitors)
            monitor->setVisible (fifoDrawerButton.get()->getToggleState());
        for (auto monitor : syncMonitors)
            monitor->setVisible (fifoDrawerButton.get()->getToggleState());
    }
}

void RecordNodeEditor::updateSettings()
{
    updateSyncMonitors();
    updateFifoMonitors();
    showFifoMonitors (fifoDrawerButton->getToggleState());
}

void RecordNodeEditor::buttonClicked (Button* button)
{
    if (button == fifoDrawerButton.get())
    {
        showFifoMonitors (button->getToggleState());
    }
}

void RecordNodeEditor::setStreamStartTime (uint16 streamId, bool isSynchronized, float offsetMs)
{
    if (syncStartTimeMonitors.find (streamId) != syncStartTimeMonitors.end())
    {
        if (syncStartTimeMonitors[streamId] != nullptr)
            syncStartTimeMonitors[streamId]->setSyncMetric (isSynchronized, offsetMs);
    }
}

void RecordNodeEditor::setLastSyncEvent (uint16 streamId, bool isSynchronized, float syncTimeSeconds)
{
    if (lastSyncEventMonitors.find (streamId) != lastSyncEventMonitors.end())
    {
        if (lastSyncEventMonitors[streamId] != nullptr)
            lastSyncEventMonitors[streamId]->setSyncMetric (isSynchronized, syncTimeSeconds);
    }
}

void RecordNodeEditor::setSyncAccuracy (uint16 streamId, bool isSynchronized, float syncAccuracy)
{
    if (syncAccuracyMonitors.find (streamId) != syncAccuracyMonitors.end())
    {
        if (syncAccuracyMonitors[streamId] != nullptr)
            syncAccuracyMonitors[streamId]->setSyncMetric (isSynchronized, syncAccuracy);
    }
}

void RecordNodeEditor::showFifoMonitors (bool show)
{
    int dX = 20 * (recordNode->getNumDataStreams() + 1);
    dX = show ? dX : 0;

    fifoDrawerButton->setBounds (
        4 + dX, fifoDrawerButton->getY(), fifoDrawerButton->getWidth(), fifoDrawerButton->getHeight());

    diskMonitor->setBounds (
        18 + dX, diskMonitor->getY(), diskMonitor->getWidth(), diskMonitor->getHeight());

    for (auto& p : { "directory", "engine", "events", "spikes" })
    {
        auto* ed = getParameterEditor (p);
        ed->setBounds (42 + dX, ed->getY(), ed->getWidth(), ed->getHeight());
    }

    desiredWidth = 165 + dX;

    if (getCollapsedState())
        return;

    for (auto& monitor : streamMonitors)
        monitor->setVisible (show);
    for (auto monitor : syncMonitors)
        monitor->setVisible (show);

    CoreServices::highlightEditor (this);
    deselect();
}

void FifoDrawerButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    g.setColour (Colour (110, 110, 110));
    if (isMouseOver)
        g.setColour (Colour (210, 210, 210));

    g.drawVerticalLine (5, 0.0f, getHeight());
}
