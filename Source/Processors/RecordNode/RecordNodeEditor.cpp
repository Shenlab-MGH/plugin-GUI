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
#include <atomic>
#include <mutex>
#include <stdio.h>
#include <unordered_map>

struct RecordToggleButtonAccessibilityState
{
    explicit RecordToggleButtonAccessibilityState (
        bool initialState)
        : publishedState (initialState)
    {
    }

    void synchronise (bool actualState)
    {
        publishedState.store (
            actualState);
    }

    bool getPublishedState() const
    {
        return publishedState.load();
    }

    void attach (Button* buttonToUse)
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

    Button* getButtonOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return button;
    }

private:
    std::atomic<bool> publishedState;
    Button* button = nullptr;
};

struct DiskMonitorAccessibilityState
{
    void setFill (double newFill)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        fill = newFill;
    }

    void setStatus (String newStatus)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        status = std::move (newStatus);
    }

    void setFillAndStatus (double newFill, String newStatus)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        fill = newFill;
        status = std::move (newStatus);
    }

    double getFill() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return fill;
    }

    String getStatus() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return status;
    }

private:
    mutable std::mutex mutex;
    double fill = 0.0;
    String status = "Status: idle; Available: 0.00 GB";
};

struct SyncMonitorAccessibilityState
{
    void synchronise (String valueToUse,
                      String titleToUse,
                      String descriptionToUse,
                      String helpToUse,
                      bool isEnabled)
    {
        {
            const std::lock_guard<std::mutex> lock (textMutex);
            value = std::move (valueToUse);
            title = std::move (titleToUse);
            description = std::move (descriptionToUse);
            help = std::move (helpToUse);
        }

        enabled.store (isEnabled);
    }

    void detach()
    {
        enabled.store (false);
    }

    String getValue() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return value;
    }

    String getTitle() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return title;
    }

    String getDescription() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return description;
    }

    String getHelp() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return help;
    }

    bool isEnabled() const
    {
        return enabled.load();
    }

private:
    mutable std::mutex textMutex;
    String value = "--";
    String title;
    String description;
    String help;
    std::atomic<bool> enabled { true };
};

struct StreamMonitorAccessibilityState
{
    void attach (StreamMonitor* monitorToUse)
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        monitor = monitorToUse;
    }

    void detach()
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        monitor = nullptr;
        enabled.store (false);
        focused.store (false);
    }

    StreamMonitor* getMonitorOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return monitor;
    }

    void synchronise (String valueToUse,
                      String titleToUse,
                      String descriptionToUse,
                      String helpToUse,
                      bool isEnabled,
                      bool isFocused)
    {
        {
            const std::lock_guard<std::mutex> lock (textMutex);
            value = std::move (valueToUse);
            title = std::move (titleToUse);
            description = std::move (descriptionToUse);
            help = std::move (helpToUse);
        }

        enabled.store (isEnabled);
        focused.store (isFocused);
    }

    String getValue() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return value;
    }

    String getTitle() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return title;
    }

    String getDescription() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return description;
    }

    String getHelp() const
    {
        const std::lock_guard<std::mutex> lock (textMutex);
        return help;
    }

    bool isEnabled() const
    {
        return enabled.load();
    }

    bool isFocused() const
    {
        return focused.load();
    }

private:
    mutable std::mutex textMutex;
    String value;
    String title;
    String description;
    String help;
    std::atomic<bool> enabled { true };
    std::atomic<bool> focused { false };
    StreamMonitor* monitor = nullptr;
};

namespace
{
struct SyncMonitorAccessibilityRegistry
{
    std::mutex mutex;
    std::unordered_map<
        SyncMonitor*,
        std::shared_ptr<SyncMonitorAccessibilityState>>
        states;
};

SyncMonitorAccessibilityRegistry&
getSyncMonitorAccessibilityRegistry()
{
    static auto* registry =
        new SyncMonitorAccessibilityRegistry();
    return *registry;
}

std::shared_ptr<SyncMonitorAccessibilityState>
registerSyncMonitor (SyncMonitor* monitor)
{
    auto state =
        std::make_shared<SyncMonitorAccessibilityState>();
    auto& registry =
        getSyncMonitorAccessibilityRegistry();
    const std::lock_guard<std::mutex> lock (registry.mutex);
    registry.states[monitor] = state;
    return state;
}

std::shared_ptr<SyncMonitorAccessibilityState>
getSyncMonitorState (SyncMonitor* monitor)
{
    auto& registry =
        getSyncMonitorAccessibilityRegistry();
    const std::lock_guard<std::mutex> lock (registry.mutex);
    const auto found = registry.states.find (monitor);
    return found != registry.states.end()
               ? found->second
               : nullptr;
}

void unregisterSyncMonitor (SyncMonitor* monitor)
{
    std::shared_ptr<SyncMonitorAccessibilityState> state;
    {
        auto& registry =
            getSyncMonitorAccessibilityRegistry();
        const std::lock_guard<std::mutex> lock (registry.mutex);
        const auto found = registry.states.find (monitor);
        if (found == registry.states.end())
            return;

        state = found->second;
        registry.states.erase (found);
    }

    state->detach();
}

struct StreamMonitorAccessibilityRegistry
{
    std::mutex mutex;
    std::unordered_map<
        StreamMonitor*,
        std::shared_ptr<StreamMonitorAccessibilityState>>
        states;
};

StreamMonitorAccessibilityRegistry&
getStreamMonitorAccessibilityRegistry()
{
    static auto* registry =
        new StreamMonitorAccessibilityRegistry();
    return *registry;
}

std::shared_ptr<StreamMonitorAccessibilityState>
registerStreamMonitor (StreamMonitor* monitor)
{
    auto state =
        std::make_shared<StreamMonitorAccessibilityState>();
    state->attach (monitor);

    auto& registry =
        getStreamMonitorAccessibilityRegistry();
    const std::lock_guard<std::mutex> lock (registry.mutex);
    registry.states[monitor] = state;
    return state;
}

std::shared_ptr<StreamMonitorAccessibilityState>
getStreamMonitorState (StreamMonitor* monitor)
{
    auto& registry =
        getStreamMonitorAccessibilityRegistry();
    const std::lock_guard<std::mutex> lock (registry.mutex);
    const auto found = registry.states.find (monitor);
    return found != registry.states.end()
               ? found->second
               : nullptr;
}

void unregisterStreamMonitor (StreamMonitor* monitor)
{
    std::shared_ptr<StreamMonitorAccessibilityState> state;
    {
        auto& registry =
            getStreamMonitorAccessibilityRegistry();
        const std::lock_guard<std::mutex> lock (registry.mutex);
        const auto found = registry.states.find (monitor);
        if (found == registry.states.end())
            return;

        state = found->second;
        registry.states.erase (found);
    }

    state->detach();
}

class SyncMonitorAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit SyncMonitorAccessibilityValue (
        std::shared_ptr<SyncMonitorAccessibilityState> stateToUse)
        : state (std::move (stateToUse))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValueAsString (const String&) override { jassertfalse; }
    String getCurrentValueAsString() const override
    {
        return state->getValue();
    }

private:
    std::shared_ptr<SyncMonitorAccessibilityState> state;
};

class SyncMonitorAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    SyncMonitorAccessibilityHandler (
        SyncMonitor& monitor,
        std::shared_ptr<SyncMonitorAccessibilityState> stateToUse)
        : AccessibilityHandler (
              monitor,
              AccessibilityRole::staticText,
              AccessibilityActions {},
              AccessibilityHandler::Interfaces {
                  std::make_unique<SyncMonitorAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

    AccessibleState getCurrentState() const override
    {
        return AccessibleState().withFocusable();
    }

    String getTitle() const override
    {
        return state->getTitle();
    }

    String getDescription() const override
    {
        return state->getDescription();
    }

    String getHelp() const override
    {
        return state->getHelp();
    }

    bool isEnabled() const override
    {
        return state->isEnabled();
    }

private:
    std::shared_ptr<SyncMonitorAccessibilityState> state;
};

class StreamMonitorAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit StreamMonitorAccessibilityValue (
        std::shared_ptr<StreamMonitorAccessibilityState> stateToUse)
        : state (std::move (stateToUse))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValueAsString (const String&) override { jassertfalse; }
    String getCurrentValueAsString() const override
    {
        return state->getValue();
    }

private:
    std::shared_ptr<StreamMonitorAccessibilityState> state;
};

class StreamMonitorAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    StreamMonitorAccessibilityHandler (
        StreamMonitor& monitor,
        std::shared_ptr<StreamMonitorAccessibilityState> stateToUse)
        : AccessibilityHandler (
              monitor,
              AccessibilityRole::button,
              createActions (stateToUse),
              AccessibilityHandler::Interfaces {
                  std::make_unique<StreamMonitorAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto current = AccessibleState().withFocusable();
        return state->isFocused()
                   ? current.withFocused()
                   : current;
    }

    String getTitle() const override
    {
        return state->getTitle();
    }

    String getDescription() const override
    {
        return state->getDescription();
    }

    String getHelp() const override
    {
        return state->getHelp();
    }

    bool isEnabled() const override
    {
        return state->isEnabled();
    }

private:
    static AccessibilityActions createActions (
        const std::shared_ptr<StreamMonitorAccessibilityState>& state)
    {
        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::press,
                [state]
                {
                    auto* messageManager =
                        MessageManager::getInstanceWithoutCreating();
                    if (messageManager == nullptr)
                        return;

                    messageManager->callSync (
                        [state]
                        {
                            auto* monitor =
                                state->getMonitorOnMessageThread();
                            if (monitor == nullptr
                                || ! monitor->isEnabled())
                            {
                                return;
                            }

                            monitor->triggerClick();
                        });
                });
    }

    std::shared_ptr<StreamMonitorAccessibilityState> state;
};

class DiskMonitorAccessibilityValue final
    : public AccessibilityRangedNumericValueInterface
{
public:
    explicit DiskMonitorAccessibilityValue (
        std::shared_ptr<DiskMonitorAccessibilityState> stateToUse)
        : state (std::move (stateToUse))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValue (double) override { jassertfalse; }
    double getCurrentValue() const override
    {
        return jlimit (0.0, 1.0, state->getFill());
    }
    AccessibleValueRange getRange() const override
    {
        return { { 0.0, 1.0 }, 0.001 };
    }

private:
    std::shared_ptr<DiskMonitorAccessibilityState> state;
};

class DiskMonitorAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    DiskMonitorAccessibilityHandler (
        DiskMonitor& monitor,
        std::shared_ptr<DiskMonitorAccessibilityState> stateToUse)
        : AccessibilityHandler (
              monitor,
              AccessibilityRole::progressBar,
              AccessibilityActions {},
              AccessibilityHandler::Interfaces {
                  std::make_unique<DiskMonitorAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

    String getHelp() const override
    {
        return state->getStatus();
    }

private:
    std::shared_ptr<DiskMonitorAccessibilityState> state;
};

String createDiskMonitorStatus (
    float bytesPerMillisecond,
    int64 bytesFree,
    float timeLeftInSeconds)
{
    constexpr double bytesPerGigabyte =
        static_cast<double> (int64 (1) << 30);
    constexpr double bytesPerMegabyte =
        static_cast<double> (int64 (1) << 20);

    String status = bytesPerMillisecond > 0.0f
                        ? "Status: recording"
                        : "Status: idle";
    status += "; Available: "
              + String (
                  static_cast<double> (bytesFree)
                      / bytesPerGigabyte,
                  2)
              + " GB";

    if (bytesPerMillisecond > 0.0f)
    {
        status += "; Remaining: "
                  + String (jmax (
                      0,
                      static_cast<int> (
                          timeLeftInSeconds / 60.0f)))
                  + " min";
        status += "; Data rate: "
                  + String (
                      static_cast<double> (
                          bytesPerMillisecond)
                          * 1000.0
                          / bytesPerMegabyte,
                      2)
                  + " MB/s";
    }

    return status;
}

class RecordToggleButtonAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit RecordToggleButtonAccessibilityValue (
        std::shared_ptr<
            RecordToggleButtonAccessibilityState>
            stateToUse)
        : state (std::move (stateToUse))
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
        RecordToggleButtonAccessibilityState>
        state;
};

class RecordToggleButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    RecordToggleButtonAccessibilityHandler (
        Button& button,
        std::shared_ptr<
            RecordToggleButtonAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              AccessibilityRole::toggleButton,
              createActions (stateToUse),
              AccessibilityHandler::Interfaces {
                  std::make_unique<
                      RecordToggleButtonAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

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
            RecordToggleButtonAccessibilityState>&
            state)
    {
        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::toggle,
                [state]
                {
                    MessageManager::callSync (
                        [state]
                        {
                            auto* button =
                                state
                                    ->getButtonOnMessageThread();
                            if (button == nullptr
                                || ! button->isEnabled())
                            {
                                return;
                            }

                            button->setToggleState (
                                ! button->getToggleState(),
                                sendNotification);

                            if (auto* survivingButton =
                                    state
                                        ->getButtonOnMessageThread())
                            {
                                state->synchronise (
                                    survivingButton
                                        ->getToggleState());
                            }
                        });
                });
    }

    std::shared_ptr<
        RecordToggleButtonAccessibilityState>
        state;
};

String getParameterSemanticId (Parameter& parameter)
{
    return parameter.getAutomationId();
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
    registerSyncMonitor (this);
    setInterceptsMouseClicks (false, false);
}

SyncMonitor::~SyncMonitor()
{
    unregisterSyncMonitor (this);
}

void SyncMonitor::setSyncMetric (bool isSynchronized_, float syncMetric_)
{
    isSynchronized = isSynchronized_;
    metric = syncMetric_;

    if (auto state = getSyncMonitorState (this))
    {
        state->synchronise (
            getAccessibleValue(),
            getTitle(),
            getDescription(),
            getHelpText(),
            Component::isEnabled());
    }

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (AccessibilityEvent::valueChanged);
}

void SyncMonitor::setEnabled (bool state)
{
    isEnabled = state;

    Component::setEnabled (state);
    repaint();
}

void SyncMonitor::enablementChanged()
{
    if (auto state = getSyncMonitorState (this))
    {
        state->synchronise (
            getAccessibleValue(),
            getTitle(),
            getDescription(),
            getHelpText(),
            Component::isEnabled());
    }
}

void SyncMonitor::setAccessibilityContext (StringRef semanticId,
                                           StringRef title,
                                           StringRef description)
{
    applySemanticMetadata (
        *this,
        semanticId,
        title,
        description,
        description);

    if (auto state = getSyncMonitorState (this))
    {
        state->synchronise (
            getAccessibleValue(),
            getTitle(),
            getDescription(),
            getHelpText(),
            Component::isEnabled());
    }
}

std::unique_ptr<AccessibilityHandler>
SyncMonitor::createAccessibilityHandler()
{
    auto state = getSyncMonitorState (this);
    if (state == nullptr)
        state = registerSyncMonitor (this);

    return std::make_unique<
        SyncMonitorAccessibilityHandler> (
        *this,
        std::move (state));
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

    registerStreamMonitor (this);
    startTimerHz (10);
}

StreamMonitor::~StreamMonitor()
{
    stopTimer();
    unregisterStreamMonitor (this);
}

std::unique_ptr<AccessibilityHandler>
StreamMonitor::createAccessibilityHandler()
{
    synchroniseAccessibilityState();

    auto state = getStreamMonitorState (this);
    if (state == nullptr)
    {
        state = registerStreamMonitor (this);
        synchroniseAccessibilityState();
    }

    return std::make_unique<
        StreamMonitorAccessibilityHandler> (
        *this,
        std::move (state));
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

void StreamMonitor::synchroniseAccessibilityState()
{
    if (auto state = getStreamMonitorState (this))
    {
        const auto description = getDescription();
        const auto help = getHelpText().isNotEmpty()
                              ? getHelpText()
                              : description;
        state->synchronise (
            getAccessibleValue(),
            getTitle(),
            description,
            help,
            Component::isEnabled(),
            hasKeyboardFocus (true));
    }
}

void StreamMonitor::setFillPercentage (float fill)
{
    fillPercentage = fill;
    repaint();
    synchroniseAccessibilityState();

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
}

void StreamMonitor::timerCallback()
{
    if (((RecordNode*) processor)->recordThread->isThreadRunning())
        setFillPercentage (((RecordNode*) processor)->fifoUsage[streamId]);
    else
        setFillPercentage (0.0);
}

void StreamMonitor::enablementChanged()
{
    Button::enablementChanged();
    synchroniseAccessibilityState();
}

void StreamMonitor::focusGained (
    FocusChangeType cause)
{
    Button::focusGained (cause);
    synchroniseAccessibilityState();
}

void StreamMonitor::focusLost (
    FocusChangeType cause)
{
    Button::focusLost (cause);
    synchroniseAccessibilityState();
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
    synchroniseAccessibilityState();

    if (auto* handler =
            getAccessibilityHandler())
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
}

DiskMonitor::DiskMonitor (RecordNode* rn)
    : LevelMonitor (rn),
      lastFreeSpace (0.0),
      recordingTimeLeftInSeconds (0),
      dataRate (0.0),
      accessibilityState (
          std::make_shared<DiskMonitorAccessibilityState>())
{
    rn->getDiskSpaceChecker()->addListener (this);
    startTimerHz (1);
}

std::unique_ptr<AccessibilityHandler> DiskMonitor::createAccessibilityHandler()
{
    return std::make_unique<DiskMonitorAccessibilityHandler> (
        *this,
        accessibilityState);
}

DiskMonitor::~DiskMonitor()
{
    if (((RecordNode*) processor)->getDiskSpaceChecker() != nullptr)
        ((RecordNode*) processor)->getDiskSpaceChecker()->removeListener (this);
}

void DiskMonitor::update (float dataRate, int64 bytesFree, float timeLeft)
{
    this->dataRate = dataRate;
    lastFreeSpace = bytesFree;
    recordingTimeLeftInSeconds = timeLeft;

    if (lowDiskSpaceStatusActive
        && ! ((RecordNode*) processor)->getRecordingStatus())
    {
        return;
    }

    lowDiskSpaceStatusActive = false;

    const auto status =
        createDiskMonitorStatus (
            dataRate,
            bytesFree,
            timeLeft);
    accessibilityState->setStatus (status);
    setTooltip (status);
}

void DiskMonitor::updateDiskSpace (float percentage)
{
    const auto usedPercentage =
        1.0f - percentage;
    accessibilityState->setFill (
        usedPercentage);
    setFillPercentage (usedPercentage);
}

void DiskMonitor::directoryInvalid (bool recordingStopped)
{
    lowDiskSpaceStatusActive = false;

    const String status =
        "Status: invalid directory";
    accessibilityState->setFillAndStatus (
        1.0,
        status);
    setTooltip (status);
    setFillPercentage (1.0);

    if (recordingStopped)
    {
        String msg = "Record Node (" + String (processor->getNodeId()) + ") - The selected recording path doesn't exist anymore.";
        msg += "\n\nPlease select a valid directory and restart the recording.";
        AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Recording Stopped", msg);
    }
}

void DiskMonitor::lowDiskSpace()
{
    lowDiskSpaceStatusActive = true;

    const String status =
        "Status: recording stopped; Reason: less than 5 minutes of disk space remaining";
    accessibilityState->setStatus (status);
    setTooltip (status);

    String msg = "Recording stopped. Less than 5 minutes of disk space remaining.";
    if (JUCEApplicationBase::getInstance() != nullptr)
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
        param->getAutomationId(),
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

RecordToggleButton::RecordToggleButton (
    const String& name)
    : CustomToggleButton(),
      accessibilityState (
          std::make_shared<
              RecordToggleButtonAccessibilityState> (
              false))
{
    accessibilityState->attach (
        this);
}

RecordToggleButton::~RecordToggleButton()
{
    accessibilityState->detach();
}

std::unique_ptr<AccessibilityHandler>
RecordToggleButton::
    createAccessibilityHandler()
{
    return std::make_unique<
        RecordToggleButtonAccessibilityHandler> (
        *this,
        accessibilityState);
}

void RecordToggleButton::
    buttonStateChanged()
{
    CustomToggleButton::buttonStateChanged();
    accessibilityState
        ->synchronise (
            getToggleState());
}

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

ClearButton::ClearButton()
    : ReadOnlyValueTextButton (
          "Revert Dir")
{
    setButtonTextWithAccessibilityValue (
        *this,
        "default");
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
        const auto isValid =
            ((PathParameter*) param)->isValid();
        if (! isValid)
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
        String statusHelp;
        if (value == "None")
        {
            statusHelp =
                "Using the default recording directory. Press to choose an override.";
        }
        else if (isValid)
        {
            statusHelp =
                "Valid recording directory: "
                + value;
        }
        else
        {
            statusHelp =
                "Invalid recording directory: "
                + value;
        }

        button->setTooltip (
            statusHelp);
        if (button->getHelpText()
            != statusHelp)
        {
            button->setHelpText (
                statusHelp);
        }

        if (auto* readOnlyButton =
                dynamic_cast<
                    ReadOnlyValueTextButton*> (
                    button.get()))
        {
            readOnlyButton
                ->refreshAccessibilityState();
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
