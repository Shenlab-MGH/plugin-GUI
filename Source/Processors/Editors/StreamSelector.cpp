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

#include "StreamSelector.h"

#include "../GenericProcessor/GenericProcessor.h"
#include "../RecordNode/RecordNodeEditor.h"
#include "DelayMonitor.h"
#include "GenericEditor.h"
#include "TTLMonitor.h"
#include "VisualizerEditor.h"

#include "../Settings/DataStream.h"
#include "../../UI/SemanticComponent.h"

#include <atomic>
#include <mutex>

class StreamSelectorAccessibilityValueState
{
public:
    explicit StreamSelectorAccessibilityValueState (
        String initialValue)
        : value (std::move (
              initialValue))
    {
    }

    String get() const
    {
        const std::lock_guard<std::mutex>
            lock (mutex);
        return value;
    }

    bool set (
        String newValue)
    {
        const std::lock_guard<std::mutex>
            lock (mutex);
        if (value == newValue)
            return false;

        value = std::move (
            newValue);
        return true;
    }

private:
    mutable std::mutex mutex;
    String value;
};

namespace
{
class StreamSelectorAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit StreamSelectorAccessibilityValue (
        std::shared_ptr<
            StreamSelectorAccessibilityValueState>
            stateToUse)
        : state (std::move (
              stateToUse))
    {
    }

    bool isReadOnly() const override
    {
        return true;
    }

    void setValueAsString (
        const String&) override
    {
        jassertfalse;
    }

    String getCurrentValueAsString()
        const override
    {
        return state->get();
    }

private:
    std::shared_ptr<
        StreamSelectorAccessibilityValueState>
        state;
};

class StreamSelectorAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    StreamSelectorAccessibilityHandler (
        StreamSelectorTable& selector,
        std::shared_ptr<
            StreamSelectorAccessibilityValueState>
            stateToUse)
        : AccessibilityHandler (
              selector,
              AccessibilityRole::group,
              AccessibilityActions {},
              AccessibilityHandler::Interfaces {
                  std::make_unique<
                      StreamSelectorAccessibilityValue> (
                      stateToUse) }),
          state (std::move (
              stateToUse))
    {
    }

    void setCurrentValue (
        String value)
    {
        if (! state->set (
                std::move (
                    value)))
        {
            return;
        }

        notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
    }

private:
    std::shared_ptr<
        StreamSelectorAccessibilityValueState>
        state;
};

String getCurrentStreamAccessibilityValue (
    StreamSelectorTable& selector)
{
    const auto* stream =
        selector.getCurrentStream();
    if (stream == nullptr)
        return "No data streams";

    return stream->getName()
           + " (source "
           + String (
               stream->getSourceNodeId())
           + ")";
}

String getStreamSelectorSemanticId (const GenericEditor& editor)
{
    return createProcessorControlSemanticId (
        editor.getProcessor()->getNodeId(),
        "streams");
}

class StreamEnableButton;

class StreamEnableButtonAccessibilityState
{
public:
    bool synchronise (
        bool enabled)
    {
        return publishedState.exchange (
                   enabled)
               != enabled;
    }

    bool getPublishedState() const
    {
        return publishedState.load();
    }

    uint64 getBindingGeneration() const
    {
        return bindingGeneration.load();
    }

    void advanceBinding()
    {
        bindingGeneration.fetch_add (1);
    }

    void attach (
        StreamEnableButton* buttonToUse)
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        button = buttonToUse;
    }

    void detach()
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        button = nullptr;
    }

    StreamEnableButton*
        getButtonOnMessageThread (
            uint64 expectedBindingGeneration)
        const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return bindingGeneration.load()
                       == expectedBindingGeneration
                   ? button
                   : nullptr;
    }

private:
    std::atomic<bool> publishedState { true };
    std::atomic<uint64>
        bindingGeneration { 1 };
    StreamEnableButton* button = nullptr;
};

class StreamEnableButtonAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit StreamEnableButtonAccessibilityValue (
        std::shared_ptr<
            StreamEnableButtonAccessibilityState>
            stateToUse)
        : state (std::move (
              stateToUse))
    {
    }

    bool isReadOnly() const override
    {
        return true;
    }

    void setValueAsString (
        const String&) override
    {
        jassertfalse;
    }

    String getCurrentValueAsString()
        const override
    {
        return state->getPublishedState()
                   ? String ("On")
                   : String ("Off");
    }

private:
    std::shared_ptr<
        StreamEnableButtonAccessibilityState>
        state;
};

class StreamEnableButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    StreamEnableButtonAccessibilityHandler (
        StreamEnableButton& button,
        std::shared_ptr<
            StreamEnableButtonAccessibilityState>
            stateToUse);

    AccessibleState getCurrentState()
        const override
    {
        auto accessibleState =
            AccessibilityHandler::getCurrentState()
                .withCheckable();

        return state->getPublishedState()
                   ? accessibleState.withChecked()
                   : accessibleState;
    }

private:
    static AccessibilityActions createActions (
        const std::shared_ptr<
            StreamEnableButtonAccessibilityState>&
            state);

    std::shared_ptr<
        StreamEnableButtonAccessibilityState>
        state;
};

class StreamEnableButton final : public Button
{
public:
    StreamEnableButton()
        : Button (
              "Stream processing enabled"),
          accessibilityState (
              std::make_shared<
                  StreamEnableButtonAccessibilityState>())
    {
        accessibilityState->attach (
            this);
        onClick =
            [this]
            {
                toggleProcessingOnMessageThread();
            };
    }

    ~StreamEnableButton() override
    {
        accessibilityState->detach();
    }

    void configure (
        StreamSelectorTable& selectorToUse,
        const DataStream& stream,
        const String& rowSemanticId,
        bool processingEnabled)
    {
        if (selector
                != &selectorToUse
            || streamId
                   != stream.getStreamId())
        {
            accessibilityState
                ->advanceBinding();
        }

        selector = &selectorToUse;
        streamId = stream.getStreamId();
        applySemanticMetadata (
            *this,
            rowSemanticId
                + ".processing_enabled",
            stream.getName()
                + " processing enabled",
            "Enable or disable processing for "
                + stream.getName()
                + " from source node "
                + String (
                    stream.getSourceNodeId())
                + ".");
        const auto* parameter =
            stream.getParameter (
                "enable_stream");
        setEnabled (
            parameter != nullptr
            && parameter->isEnabled());
        setPublishedState (
            processingEnabled,
            false);
    }

    void setPublishedState (
        bool enabled,
        bool notify)
    {
        const auto changed =
            accessibilityState->synchronise (
                enabled);
        repaint();

        if (changed
            && notify)
        {
            if (auto* handler =
                    getAccessibilityHandler())
            {
                handler->notifyAccessibilityEvent (
                    AccessibilityEvent::
                        valueChanged);
            }
        }
    }

    std::unique_ptr<AccessibilityHandler>
        createAccessibilityHandler() override
    {
        return std::make_unique<
            StreamEnableButtonAccessibilityHandler> (
            *this,
            accessibilityState);
    }

    void toggleProcessingOnMessageThread()
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        auto* selectorToUse = selector;
        const auto streamIdToUse =
            streamId;
        const auto desiredState =
            ! accessibilityState
                   ->getPublishedState();

        if (selectorToUse != nullptr)
        {
            selectorToUse
                ->setStreamProcessingEnabled (
                    streamIdToUse,
                    desiredState);
        }
    }

private:
    void paintButton (
        Graphics& g,
        bool isMouseOver,
        bool isButtonDown) override
    {
        const auto bounds =
            getLocalBounds()
                .toFloat()
                .reduced (2.0f);
        auto colour =
            findColour (
                ThemeColours::defaultText);
        if (isMouseOver
            || isButtonDown
            || ! isEnabled())
        {
            colour =
                colour.withAlpha (
                    0.6f);
        }

        g.setColour (
            colour);
        g.drawRect (
            bounds,
            1.0f);

        if (accessibilityState
                ->getPublishedState())
        {
            Path tick;
            tick.startNewSubPath (
                bounds.getX()
                    + bounds.getWidth()
                          * 0.2f,
                bounds.getCentreY());
            tick.lineTo (
                bounds.getX()
                    + bounds.getWidth()
                          * 0.43f,
                bounds.getBottom()
                    - bounds.getHeight()
                          * 0.2f);
            tick.lineTo (
                bounds.getRight()
                    - bounds.getWidth()
                          * 0.15f,
                bounds.getY()
                    + bounds.getHeight()
                          * 0.2f);
            g.strokePath (
                tick,
                PathStrokeType (
                    1.5f));
        }
    }

    StreamSelectorTable* selector = nullptr;
    uint16 streamId = 0;
    std::shared_ptr<
        StreamEnableButtonAccessibilityState>
        accessibilityState;
};

StreamEnableButtonAccessibilityHandler::
    StreamEnableButtonAccessibilityHandler (
        StreamEnableButton& button,
        std::shared_ptr<
            StreamEnableButtonAccessibilityState>
            stateToUse)
    : AccessibilityHandler (
          button,
          AccessibilityRole::toggleButton,
          createActions (
              stateToUse),
          AccessibilityHandler::Interfaces {
              std::make_unique<
                  StreamEnableButtonAccessibilityValue> (
                  stateToUse) }),
      state (std::move (
          stateToUse))
{
}

AccessibilityActions
StreamEnableButtonAccessibilityHandler::
    createActions (
        const std::shared_ptr<
            StreamEnableButtonAccessibilityState>&
            state)
{
    const auto bindingGeneration =
        state->getBindingGeneration();
    return AccessibilityActions()
        .addAction (
            AccessibilityActionType::toggle,
            [state,
             bindingGeneration]
            {
                MessageManager::callSync (
                    [state,
                     bindingGeneration]
                    {
                        if (auto* button =
                                state
                                    ->getButtonOnMessageThread (
                                        bindingGeneration))
                        {
                            button
                                ->toggleProcessingOnMessageThread();
                        }
                    });
            });
}

class AccessibleStreamTableListBox final : public TableListBox
{
public:
    AccessibleStreamTableListBox (
        const String& name,
        StreamTableModel& model,
        String semanticId)
        : TableListBox (name, &model),
          streamModel (model),
          tableSemanticId (std::move (semanticId))
    {
    }

    Component* refreshComponentForRow (
        int rowNumber,
        bool isRowSelected,
        Component* existingComponentToUpdate) override
    {
        auto* row = TableListBox::refreshComponentForRow (
            rowNumber,
            isRowSelected,
            existingComponentToUpdate);

        if (row != nullptr)
        {
            row->setComponentID (
                streamModel.getSemanticIdForRow (
                    rowNumber,
                    tableSemanticId));
            row->setTitle (
                streamModel.getNameForRow (rowNumber));
            row->setDescription (
                streamModel.getDescriptionForRow (rowNumber));
        }

        return row;
    }

    String getNameForRow (int rowNumber) override
    {
        return streamModel.getNameForRow (rowNumber);
    }

private:
    StreamTableModel& streamModel;
    String tableSemanticId;
};
} // namespace

StreamTableModel::StreamTableModel (StreamSelectorTable* owner_)
    : owner (owner_)
{
}

void StreamTableModel::cellClicked (int rowNumber, int columnId, const MouseEvent& event)
{
    if (event.mods.isLeftButtonDown())
    {
        owner->selectStreamFromRow (rowNumber);

        return;
    }
    else if (event.mods.isRightButtonDown() && owner->editor->getProcessor()->isFilter())
    {
        PopupMenu m;
        bool streamState = owner->checkStream (streams[rowNumber]);

        String enableText = (streamState ? "Disable" : "Enable") + String (" stream");
        m.addItem (1, enableText);

        int result = m.showMenu (PopupMenu::Options().withStandardItemHeight (20));

        if (result == 1)
        {
            owner->setStreamProcessingEnabled (
                streams[rowNumber]->getStreamId(),
                ! streamState);
        }

        return;
    }
}

Component* StreamTableModel::refreshComponentForCell (int rowNumber,
                                                      int columnId,
                                                      bool isRowSelected,
                                                      Component* existingComponentToUpdate)
{
    const auto rowSemanticId =
        getSemanticIdForRow (
            rowNumber,
            table->getComponentID());

    if (columnId == StreamTableModel::Columns::DELAY)
    {
        auto* delayMonitor = dynamic_cast<DelayMonitor*> (existingComponentToUpdate);

        if (delayMonitor == nullptr)
        {
            delayMonitor = new DelayMonitor();
        }

        const auto* stream = streams[rowNumber];
        delayMonitor->setAccessibilityContext (
            rowSemanticId
                + ".processing_delay",
            stream->getName() + " processing delay",
            "Processing delay for " + stream->getName() + ".");

        return delayMonitor;
    }
    else if (columnId == StreamTableModel::Columns::TTL_LINE_STATES)
    {
        auto* ttlMonitor = dynamic_cast<TTLMonitor*> (existingComponentToUpdate);

        if (ttlMonitor == nullptr)
        {
            ttlMonitor = new TTLMonitor (8, 8);
        }

        const auto* stream = streams[rowNumber];
        const auto streamId = rowSemanticId
                              + ".ttl_lines";
        ttlMonitor->setAccessibilityContext (
            streamId,
            stream->getName() + " TTL line states",
            "Current digital event state for " + stream->getName() + ".");

        return ttlMonitor;
    }
    else if (columnId == StreamTableModel::Columns::ENABLED)
    {
        auto* toggle =
            dynamic_cast<
                StreamEnableButton*> (
                existingComponentToUpdate);

        if (toggle == nullptr)
            toggle = new StreamEnableButton();

        const auto* stream =
            streams[rowNumber];
        toggle->configure (
            *owner,
            *stream,
            rowSemanticId,
            owner->checkStream (
                stream));
        return toggle;
    }
    else if (columnId == StreamTableModel::Columns::START_TIME)
    {
        auto* syncStartTimeMonitor = dynamic_cast<SyncStartTimeMonitor*> (existingComponentToUpdate);

        if (syncStartTimeMonitor == nullptr)
        {
            syncStartTimeMonitor = new SyncStartTimeMonitor();
        }

        const auto* stream = streams[rowNumber];
        syncStartTimeMonitor->setAccessibilityContext (
            rowSemanticId
                + ".sync_start_offset",
            stream->getName() + " synchronization start offset",
            "Synchronization start offset for " + stream->getName() + ".");

        return syncStartTimeMonitor;
    }
    else if (columnId == StreamTableModel::Columns::LATEST_SYNC)
    {
        auto* lastSyncEventMonitor = dynamic_cast<LastSyncEventMonitor*> (existingComponentToUpdate);

        if (lastSyncEventMonitor == nullptr)
        {
            lastSyncEventMonitor = new LastSyncEventMonitor();
        }

        const auto* stream = streams[rowNumber];
        lastSyncEventMonitor->setAccessibilityContext (
            rowSemanticId
                + ".last_sync_event",
            stream->getName() + " last synchronization event",
            "Approximate time since the last synchronization event for "
                + stream->getName() + ".");

        return lastSyncEventMonitor;
    }
    else if (columnId == StreamTableModel::Columns::SYNC_ACCURACY)
    {
        auto* syncAccuracyMonitor = dynamic_cast<SyncAccuracyMonitor*> (existingComponentToUpdate);

        if (syncAccuracyMonitor == nullptr)
        {
            syncAccuracyMonitor = new SyncAccuracyMonitor();
        }

        const auto* stream = streams[rowNumber];
        syncAccuracyMonitor->setAccessibilityContext (
            rowSemanticId
                + ".sync_accuracy",
            stream->getName() + " synchronization accuracy",
            "Synchronization accuracy for " + stream->getName() + ".");

        return syncAccuracyMonitor;
    }

    jassert (existingComponentToUpdate == nullptr);

    return nullptr;
}

int StreamTableModel::getNumRows()
{
    return streams.size(); // dataStreams.size();
}

void StreamTableModel::update (Array<const DataStream*> dataStreams_)
{
    streams = dataStreams_;
    streamSemanticSegments.clear();
    for (const auto* stream : streams)
    {
        streamSemanticSegments.add (
            createStreamSemanticSegment (
                stream->getSourceNodeId(),
                stream->getIdentifier(),
                stream->getName()));
    }
    table->updateContent();
}

void StreamTableModel::paintRowBackground (Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    if (rowNumber >= streams.size())
        return;

    if (owner->checkStream (streams[rowNumber]))
    {
        if (rowNumber % 2 == 0)
            g.fillAll (owner->findColour (ThemeColours::componentBackground));
        else
            g.fillAll (owner->findColour (ThemeColours::componentBackground).darker (0.25f));
    }
    else
    {
        g.fillAll (Colours::red.withAlpha (0.5f));
    }

    if (owner->getViewedIndex() == rowNumber)
    {
        g.setColour (Colours::yellow);
        g.drawRect (0, 0, width, height, 2);
        g.fillEllipse (5, 7, 6, 6);
    }
}

void StreamTableModel::paintCell (Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    if (rowNumber >= streams.size())
        return;

    g.setColour (owner->editor->findColour (ThemeColours::defaultText));
    g.setFont (FontOptions (12.0f));

    if (columnId == StreamTableModel::Columns::PROCESSOR_ID)
    {
        g.drawText (String (streams[rowNumber]->getSourceNodeId()), 2, 0, width - 4, height, Justification::centredLeft);
    }
    else if (columnId == StreamTableModel::Columns::NAME)
    {
        g.drawText (String (streams[rowNumber]->getName()), 2, 0, width - 5, height, Justification::centredLeft);
    }
    else if (columnId == StreamTableModel::Columns::NUM_CHANNELS)
    {
        g.drawText (String (streams[rowNumber]->getChannelCount()), 2, 0, width - 4, height, Justification::centredLeft);
    }
    else if (columnId == StreamTableModel::Columns::SAMPLE_RATE)
    {
        g.drawText (String (streams[rowNumber]->getSampleRate()), 2, 0, width - 4, height, Justification::centredLeft);
    }
}

String StreamTableModel::getCellTooltip (int rowNumber, int columnId)
{
    if (rowNumber >= streams.size())
        return String();

    if (columnId == StreamTableModel::Columns::NAME)
    {
        return streams[rowNumber]->getName();
    }

    return String();
}

String StreamTableModel::getNameForRow (int rowNumber)
{
    if (! isPositiveAndBelow (rowNumber, streams.size()))
        return {};

    const auto* stream = streams[rowNumber];
    return stream->getName()
           + " (source "
           + String (stream->getSourceNodeId())
           + ")";
}

void StreamTableModel::selectedRowsChanged (int lastRowSelected)
{
    owner->selectStreamFromRow (lastRowSelected);
}

String StreamTableModel::getSemanticIdForRow (
    int rowNumber,
    const String& tableSemanticId) const
{
    if (! isPositiveAndBelow (rowNumber, streams.size()))
        return {};

    return createStreamSelectorRowSemanticId (
        tableSemanticId,
        streamSemanticSegments,
        rowNumber);
}

String StreamTableModel::getDescriptionForRow (int rowNumber) const
{
    if (! isPositiveAndBelow (rowNumber, streams.size()))
        return {};

    const auto* stream = streams[rowNumber];
    return "Select "
           + stream->getName()
           + " from source node "
           + String (stream->getSourceNodeId())
           + ".";
}

void StreamTableModel::listWasScrolled()
{
    owner->editor->updateDelayAndTTLMonitors();

    //if (owner->isRecordNode)
    //{
    //    RecordNodeEditor* recNodeEditor = (RecordNodeEditor*) owner->editor;
    //    recNodeEditor->updateSyncMonitors();
    //}
}

StreamSelectorTable::StreamSelectorTable (GenericEditor* ed_) : editor (ed_),
                                                                viewedStreamIndex (0),
                                                                accessibilityValueState (
                                                                    std::make_shared<
                                                                        StreamSelectorAccessibilityValueState> (
                                                                        "No data streams")),
                                                                streamInfoViewWidth (130),
                                                                streamInfoViewHeight (80)
{
    isRecordNode = editor->getProcessor()->isRecordNode();
    const auto semanticId = getStreamSelectorSemanticId (*editor);
    const auto processorName = editor->getProcessor()->getName();
    const auto processorId = editor->getProcessor()->getNodeId();

    applySemanticMetadata (
        *this,
        semanticId,
        processorName + " data streams",
        "Select and inspect data streams for " + processorName
            + " (node " + String (processorId) + ").");

    tableModel = std::make_unique<StreamTableModel> (this);
    streamTable.reset (createTableView());
    tableModel->table = streamTable.get();

    addAndMakeVisible (streamTable.get());
    streamTable->setBounds (2, 20, 250, 70);
    streamTable->getViewport()->setScrollBarsShown (true, false, true, false);
    streamTable->getViewport()->setScrollBarThickness (10);

    expanderButton = std::make_unique<ExpanderButton>();
    applySemanticMetadata (
        *expanderButton,
        semanticId + ".expand",
        "Expand data streams",
        "Open the full data stream table for " + processorName
            + " (node " + String (processorId) + ").");
    addAndMakeVisible (expanderButton.get());
    expanderButton->setBounds (240, 4, 15, 15);
    expanderButton->addListener (this);
}

TableListBox* StreamSelectorTable::createTableView (bool expanded)
{
    const auto processorName = editor->getProcessor()->getName();
    const auto processorId = editor->getProcessor()->getNodeId();
    const auto semanticId = getStreamSelectorSemanticId (*editor)
                            + (expanded ? ".expanded_table" : ".table");
    TableListBox* table = new AccessibleStreamTableListBox (
        "Stream Table",
        *tableModel,
        semanticId);

    applySemanticMetadata (
        *table,
        semanticId,
        expanded ? "Expanded data streams" : "Available data streams",
        "Select the data stream shown by " + processorName
            + " (node " + String (processorId) + ").");

    table->setHeader (std::make_unique<TableHeaderComponent>());

    table->getHeader().addColumn (" ", StreamTableModel::Columns::SELECTED, 12, 12, 12, TableHeaderComponent::notResizableOrSortable);
    if (editor->getProcessor()->isFilter())
        table->getHeader().addColumn ("ON", StreamTableModel::Columns::ENABLED, 18, 18, 18, TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("NAME", StreamTableModel::Columns::NAME, 94, 94, 94, TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("DELAY", StreamTableModel::Columns::DELAY, 50, 50, 50, TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("TTL", StreamTableModel::Columns::TTL_LINE_STATES, 60, 60, 60, TableHeaderComponent::notResizableOrSortable);

    if (expanded)
    {
        table->getHeader().addColumn ("ID", StreamTableModel::Columns::PROCESSOR_ID, 30, 30, 30, TableHeaderComponent::notResizableOrSortable);
        table->getHeader().addColumn ("# CH", StreamTableModel::Columns::NUM_CHANNELS, 30, 30, 30, TableHeaderComponent::notResizableOrSortable);
        table->getHeader().addColumn ("Hz", StreamTableModel::Columns::SAMPLE_RATE, 40, 40, 40, TableHeaderComponent::notResizableOrSortable);
        //table->getHeader().addColumn ("", StreamTableModel::Columns::ENABLED, 15, 15, 15, TableHeaderComponent::notResizableOrSortable);

        if (isRecordNode)
        {
            table->getHeader().addColumn ("Start", StreamTableModel::Columns::START_TIME, 50, 50, 50, TableHeaderComponent::notResizableOrSortable);
            table->getHeader().addColumn ("Tolerance", StreamTableModel::Columns::SYNC_ACCURACY, 55, 50, 50, TableHeaderComponent::notResizableOrSortable);
            table->getHeader().addColumn ("Latest Sync", StreamTableModel::Columns::LATEST_SYNC, 55, 60, 60, TableHeaderComponent::notResizableOrSortable);
        }
    }

    if (expanded)
        table->setHeaderHeight (24);
    else
        table->setHeaderHeight (0);

    table->setRowHeight (20);
    table->setMultipleSelectionEnabled (false);

    return table;
}

StreamSelectorTable::~StreamSelectorTable()
{
    if (expandedTableComponent != nullptr)
    {
        expandedTableComponent->removeComponentListener (this);
    }
}

void StreamSelectorTable::buttonClicked (Button* button)
{
    if (button == expanderButton.get())
    {
        auto* table = createTableView (true);

        int width = editor->getProcessor()->isFilter()
                        ? 334
                        : 316;

        if (isRecordNode)
            width += 160;

        table->setBounds (0, 0, width, streams.size() * 20 + 24);
        table->selectRow (viewedStreamIndex);
        tableModel->table = table;

        expandedTableComponent = new ExpandedTableComponent (table, this);

        CoreServices::getPopupManager()->showPopup (std::unique_ptr<PopupComponent> (expandedTableComponent), button);

        expandedTableComponent->addComponentListener (this);

        editor->updateDelayAndTTLMonitors();

        if (isRecordNode)
        {
            RecordNodeEditor* recNodeEditor = dynamic_cast<RecordNodeEditor*> (editor);
            recNodeEditor->updateSyncMonitors();
        }
    }
}

void StreamSelectorTable::componentBeingDeleted (Component& component)
{
    expandedTableComponent = nullptr;
    tableModel->table = streamTable.get();
    streamTable->selectRow (viewedStreamIndex);

    Array<const DataStream*> newStreams;

    for (auto stream : streams)
    {
        newStreams.add (stream);
    }

    tableModel->update (newStreams);

    editor->updateDelayAndTTLMonitors();

    if (isRecordNode)
    {
        RecordNodeEditor* recNodeEditor = dynamic_cast<RecordNodeEditor*> (editor);
        recNodeEditor->updateSyncMonitors();
    }
}

int StreamSelectorTable::getDesiredWidth()
{
    return editor->getProcessor()->isFilter()
               ? 258
               : 240;
}

bool StreamSelectorTable::checkStream (const DataStream* streamToCheck)
{
    //StreamInfoView* siv = getStreamInfoView(streamToCheck);

    std::map<uint16, bool>::iterator it = streamStates.begin();

    while (it != streamStates.end())
    {
        // Accessing KEY from element pointed by it.
        uint16 streamId = it->first;
        // Accessing VALUE from element pointed by it.
        bool state = it->second;
        //LOGD(streamId, " :: ", state);

        // Increment the Iterator to point to next entry
        it++;
    }

    if (streamStates.count (streamToCheck->getStreamId()) > 0)
    {
        //LOGD(" Stream Selector returning ", streamStates[streamToCheck->getStreamId()]);
        return streamStates[streamToCheck->getStreamId()];
    }

    else
    {
        //LOGD(" Stream not found, returning 1.");
        return true;
    }
}

SyncStartTimeMonitor* StreamSelectorTable::getSyncStartTimeMonitor (const DataStream* stream)
{
    TableListBox* currentTable = tableModel->table;
    return dynamic_cast<SyncStartTimeMonitor*> (currentTable->getCellComponent (StreamTableModel::Columns::START_TIME, streams.indexOf (stream)));
}

LastSyncEventMonitor* StreamSelectorTable::getlastSyncEventMonitor (const DataStream* stream)
{
    TableListBox* currentTable = tableModel->table;
    return dynamic_cast<LastSyncEventMonitor*> (currentTable->getCellComponent (StreamTableModel::Columns::LATEST_SYNC, streams.indexOf (stream)));
}

SyncAccuracyMonitor* StreamSelectorTable::getSyncAccuracyMonitor (const DataStream* stream)
{
    TableListBox* currentTable = tableModel->table;
    return dynamic_cast<SyncAccuracyMonitor*> (currentTable->getCellComponent (StreamTableModel::Columns::SYNC_ACCURACY, streams.indexOf (stream)));
}

TTLMonitor* StreamSelectorTable::getTTLMonitor (const DataStream* stream)
{
    TableListBox* currentTable = tableModel->table;
    return dynamic_cast<TTLMonitor*> (currentTable->getCellComponent (StreamTableModel::Columns::TTL_LINE_STATES, streams.indexOf (stream)));
}

DelayMonitor* StreamSelectorTable::getDelayMonitor (const DataStream* stream)
{
    TableListBox* currentTable = tableModel->table;
    return dynamic_cast<DelayMonitor*> (currentTable->getCellComponent (StreamTableModel::Columns::DELAY, streams.indexOf (stream)));
}

void StreamSelectorTable::startAcquisition()
{
    // Reset TTL Monitor states
    for (auto stream : streams)
    {
        TTLMonitor* ttlMonitor = getTTLMonitor (stream);

        if (ttlMonitor != nullptr)
        {
            for (int bit = 0; bit < 8; ++bit)
            {
                ttlMonitor->setState (bit, false);
            }
        }
    }

    startTimer (20);
}

void StreamSelectorTable::stopAcquisition()
{
    stopTimer();
}

void StreamSelectorTable::timerCallback()
{
    for (auto stream : streams)
    {
        TTLMonitor* ttlMonitor = getTTLMonitor (stream);

        if (ttlMonitor != nullptr)
            ttlMonitor->repaint();
    }

    counter++;

    if (counter % 10 == 0)
    {
        for (auto stream : streams)
        {
            DelayMonitor* delayMonitor = getDelayMonitor (stream);

            if (delayMonitor != nullptr)
                delayMonitor->repaint();

            if (isRecordNode && counter % 20 == 0)
            {
                SyncStartTimeMonitor* syncStartTimeMonitor = getSyncStartTimeMonitor (stream);

                if (syncStartTimeMonitor != nullptr)
                    syncStartTimeMonitor->repaint();

                LastSyncEventMonitor* lastSyncEventMonitor = getlastSyncEventMonitor (stream);

                if (lastSyncEventMonitor != nullptr)
                    lastSyncEventMonitor->repaint();

                SyncAccuracyMonitor* syncAccuracyMonitor = getSyncAccuracyMonitor (stream);

                if (syncAccuracyMonitor != nullptr)
                    syncAccuracyMonitor->repaint();
            }
        }
    }

    if (counter > 20)
        counter = 0;
}

void StreamSelectorTable::setStreamEnabledState (uint16 streamId, bool isEnabled)
{
    if (! MessageManager::getInstance()
              ->isThisTheMessageThread())
    {
        MessageManager::callAsync (
            [safeSelector =
                 Component::SafePointer<
                     StreamSelectorTable> (
                     this),
             streamId,
             isEnabled]
            {
                if (safeSelector
                    != nullptr)
                {
                    safeSelector
                        ->setStreamEnabledState (
                            streamId,
                            isEnabled);
                }
            });
        return;
    }

    streamStates[streamId] = isEnabled;

    auto updateToggle =
        [this,
         streamId,
         isEnabled] (
            TableListBox* table)
        {
            if (table == nullptr)
                return;

            for (int row = 0;
                 row < streams.size();
                 ++row)
            {
                if (streams[row]
                        ->getStreamId()
                    != streamId)
                {
                    continue;
                }

                if (auto* toggle =
                        dynamic_cast<
                            StreamEnableButton*> (
                            table->getCellComponent (
                                StreamTableModel::
                                    Columns::ENABLED,
                                row)))
                {
                    toggle->setPublishedState (
                        isEnabled,
                        true);
                }
                break;
            }
        };

    updateToggle (
        streamTable.get());
    if (tableModel->table
        != streamTable.get())
    {
        updateToggle (
            tableModel->table);
    }

    tableModel->table->repaint();
    streamTable->repaint();
}

bool StreamSelectorTable::
    setStreamProcessingEnabled (
        uint16 streamId,
        bool isEnabled)
{
    jassert (
        MessageManager::getInstance()
            ->isThisTheMessageThread());

    if (! editor->getProcessor()
              ->isFilter())
    {
        return false;
    }

    for (auto* stream : streams)
    {
        if (stream->getStreamId()
            != streamId)
        {
            continue;
        }

        auto* parameter =
            stream->getParameter (
                "enable_stream");
        if (parameter == nullptr
            || ! parameter->isEnabled())
        {
            return false;
        }

        parameter->setNextValue (
            isEnabled);
        return true;
    }

    return false;
}

void StreamSelectorTable::resized()
{
    //viewport->setBounds(5, 5, 300, getHeight() - 10);
}

int StreamSelectorTable::getViewedIndex()
{
    return viewedStreamIndex;
}

void StreamSelectorTable::selectStreamFromRow (int rowNumber)
{
    if (! isPositiveAndBelow (rowNumber, streams.size()))
        return;

    if (! MessageManager::getInstance()->isThisTheMessageThread())
    {
        MessageManager::callAsync (
            [safeSelector =
                 Component::SafePointer<StreamSelectorTable> (this),
             rowNumber]
            {
                if (safeSelector != nullptr)
                    safeSelector->selectStreamFromRow (rowNumber);
            });
        return;
    }

    if (viewedStreamIndex == rowNumber)
        return;

    viewedStreamIndex = rowNumber;
    bool foundSelectedStreamParam = false;

    for (auto* param : editor->getProcessor()->getParameters())
    {
        if (param->getType()
                == Parameter::ParameterType::SELECTED_STREAM_PARAM
            && static_cast<SelectedStreamParameter*> (param)
                   ->shouldSyncWithStreamSelector())
        {
            param->setNextValue (rowNumber);
            foundSelectedStreamParam = true;
            break;
        }
    }

    if (editor->isVisualizerEditor())
    {
        auto* visualizerEditor =
            dynamic_cast<VisualizerEditor*> (editor);

        if (visualizerEditor != nullptr
            && visualizerEditor->canvas != nullptr)
        {
            for (auto* param :
                 visualizerEditor->canvas->getParameters())
            {
                if (param->getType()
                        == Parameter::ParameterType::SELECTED_STREAM_PARAM
                    && static_cast<SelectedStreamParameter*> (param)
                           ->shouldSyncWithStreamSelector())
                {
                    param->setNextValue (rowNumber);
                    foundSelectedStreamParam = true;
                    break;
                }
            }
        }
    }

    if (! foundSelectedStreamParam)
        editor->updateSelectedStream (
            streams[rowNumber]->getStreamId());

    tableModel->table->repaint();
    publishCurrentStreamAccessibilityValue();
}

void StreamSelectorTable::setViewedIndex (int i)
{
    if (i >= 0 && i < streams.size())
    {
        viewedStreamIndex = i;
        streamTable->selectRow (viewedStreamIndex);
        publishCurrentStreamAccessibilityValue();
    }
}

void StreamSelectorTable::
    publishCurrentStreamAccessibilityValue()
{
    const auto value =
        getCurrentStreamAccessibilityValue (
            *this);

    if (auto* handler =
            dynamic_cast<
                StreamSelectorAccessibilityHandler*> (
                getAccessibilityHandler()))
    {
        handler->setCurrentValue (
            value);
        return;
    }

    accessibilityValueState->set (
        value);
}

void StreamSelectorTable::paint (Graphics& g)
{
    g.setColour (findColour (ThemeColours::widgetBackground));
    g.fillRoundedRectangle (1.0f, 1.0f, (float) getWidth() - 6.0f, (float) getHeight() - 2.0f, 5.0f);
    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions ("Inter", "Medium", 13));
    g.drawText ("   Available data streams: ", Rectangle<float> (150.0f, 20.0f), Justification::left);

    g.setColour (findColour (ThemeColours::outline).withAlpha (0.8f));
    g.drawRoundedRectangle (1.0f, 1.0f, (float) getWidth() - 6.0f, (float) getHeight() - 2.0f, 5.0f, 1.0f);
    g.fillRect (1.0f, 19.0f, (float) getWidth() - 6.0f, 1.0f);
}

const DataStream* StreamSelectorTable::getCurrentStream()
{
    if (isPositiveAndBelow (
            viewedStreamIndex,
            streams.size()))
        return streams[viewedStreamIndex];

    return nullptr;
}

std::unique_ptr<AccessibilityHandler>
StreamSelectorTable::createAccessibilityHandler()
{
    return std::make_unique<
        StreamSelectorAccessibilityHandler> (
        *this,
        accessibilityValueState);
}

void StreamSelectorTable::add (const DataStream* stream)
{
    streams.add (stream);

    if (editor->getProcessor()->isFilter())
    {
        if (auto* param = stream->getParameter ("enable_stream"))
        {
            streamStates[stream->getStreamId()] = param->getValue();
            return;
        }
    }

    streamStates[stream->getStreamId()] = true;
}

void StreamSelectorTable::beginUpdate()
{
    streams.clear();
    streamStates.clear();
}

uint16 StreamSelectorTable::finishedUpdate()
{
    Array<const DataStream*> newStreams;

    for (auto stream : streams)
    {
        newStreams.add (stream);
    }

    if (streams.size() == 0)
    {
        expanderButton->setEnabled (false);
        tableModel->table = streamTable.get();
        tableModel->update (newStreams);
        publishCurrentStreamAccessibilityValue();
        return 0;
    }
    else
    {
        expanderButton->setEnabled (true);

        tableModel->table = streamTable.get();
        tableModel->update (newStreams);

        if (viewedStreamIndex < streams.size())
        {
            streamTable->selectRow (viewedStreamIndex);
            publishCurrentStreamAccessibilityValue();
            return streams[viewedStreamIndex]->getStreamId();
        }
        else
        {
            viewedStreamIndex = streams.size() - 1;
            streamTable->selectRow (viewedStreamIndex);
            publishCurrentStreamAccessibilityValue();
            return streams[viewedStreamIndex]->getStreamId();
        }
    }
}

void StreamSelectorTable::remove (const DataStream* stream)
{
    if (streams.contains (stream))
        streams.remove (streams.indexOf (stream));

    if (streamStates.count (stream->getStreamId()) > 0)
        streamStates.erase (stream->getStreamId());

    publishCurrentStreamAccessibilityValue();
}

ExpanderButton::ExpanderButton() : Button ("Expander")
{
    const float height = 9.0f;
    const float width = 9.0f;

    // Draw the lower left arrow
    iconPath.startNewSubPath (0.0f, height / 2);
    iconPath.lineTo (0.0f, height);
    iconPath.lineTo (width / 2, height);

    // Draw the upper right arrow
    iconPath.startNewSubPath (width / 2, 0.0f);
    iconPath.lineTo (width, 0.0f);
    iconPath.lineTo (width, height / 2);

    // Draw the diagonal line
    iconPath.startNewSubPath (0.0f, height);
    iconPath.lineTo (width, 0.0f);
}

void ExpanderButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    if (isMouseOver || ! isEnabled())
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
    else
        g.setColour (findColour (ThemeColours::defaultText));

    g.strokePath (iconPath, PathStrokeType (1.5f));
}

ExpandedTableComponent::ExpandedTableComponent (TableListBox* table, Component* parent)
    : PopupComponent (parent)
{
    expandedTable.reset (table);
    addAndMakeVisible (expandedTable.get());
    setSize (expandedTable->getWidth(), expandedTable->getHeight());
}

void ExpandedTableComponent::updatePopup()
{
    if (expandedTable->getNumRows() == 0)
    {
        findParentComponentOfClass<CallOutBox>()->exitModalState (0);
        return;
    }

    if (auto* tableModel = dynamic_cast<StreamTableModel*> (expandedTable->getTableListBoxModel()))
    {
        tableModel->table = expandedTable.get();
    }

    expandedTable->setSize (expandedTable->getWidth(), expandedTable->getNumRows() * 20 + 24);
    setSize (expandedTable->getWidth(), expandedTable->getHeight());
    expandedTable->repaint();
}
