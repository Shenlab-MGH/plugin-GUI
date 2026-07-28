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

#include "EventDisplayInterface.h"

#include "LfpDisplay.h"
#include "LfpDisplayCanvas.h"

#include <math.h>
#include <mutex>

using namespace LfpViewer;

struct LfpViewer::EventOverlayAccessibilityState
{
    void attach (
        EventOverlayButton* buttonToUse)
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
        enabled.store (false);
        shown.store (false);
        focused.store (false);
    }

    void synchronise (
        String titleToUse,
        String descriptionToUse,
        String helpToUse,
        bool isShown,
        bool isEnabled,
        bool isFocused)
    {
        {
            const std::lock_guard<std::mutex>
                lock (textMutex);
            title = std::move (
                titleToUse);
            description =
                std::move (
                    descriptionToUse);
            help = std::move (
                helpToUse);
        }
        shown.store (isShown);
        enabled.store (isEnabled);
        focused.store (isFocused);
    }

    String getTitle() const
    {
        const std::lock_guard<std::mutex>
            lock (textMutex);
        return title;
    }

    String getDescription() const
    {
        const std::lock_guard<std::mutex>
            lock (textMutex);
        return description;
    }

    String getHelp() const
    {
        const std::lock_guard<std::mutex>
            lock (textMutex);
        return help;
    }

    bool isShown() const
    {
        return shown.load();
    }

    bool isAvailable() const
    {
        return enabled.load();
    }

    bool isFocused() const
    {
        return focused.load();
    }

    void toggleOnMessageThread();

private:
    mutable std::mutex textMutex;
    String title;
    String description;
    String help;
    std::atomic<bool>
        shown { true };
    std::atomic<bool>
        enabled { true };
    std::atomic<bool>
        focused { false };
    Component::SafePointer<
        EventOverlayButton>
        button;
};

namespace
{
class EventOverlayAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit EventOverlayAccessibilityValue (
        std::shared_ptr<
            EventOverlayAccessibilityState>
            stateToUse)
        : state (
              std::move (
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
        return state->isShown()
                   ? String ("Shown")
                   : String ("Hidden");
    }

private:
    std::shared_ptr<
        EventOverlayAccessibilityState>
        state;
};

class EventOverlayAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    EventOverlayAccessibilityHandler (
        Component& button,
        std::shared_ptr<
            EventOverlayAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              AccessibilityRole::
                  toggleButton,
              createActions (
                  stateToUse),
              AccessibilityHandler::Interfaces {
                  std::make_unique<
                      EventOverlayAccessibilityValue> (
                      stateToUse) }),
          state (
              std::move (
                  stateToUse))
    {
    }

    AccessibleState
    getCurrentState() const override
    {
        auto current =
            AccessibleState()
                .withFocusable()
                .withCheckable();
        if (state->isShown())
            current =
                current.withChecked();
        if (state->isFocused())
            current =
                current.withFocused();
        return current;
    }

    String getTitle() const override
    {
        return state->getTitle();
    }

    String getDescription()
        const override
    {
        return state
            ->getDescription();
    }

    String getHelp() const override
    {
        return state->getHelp();
    }

    bool isEnabled() const override
    {
        return state
            ->isAvailable();
    }

private:
    static AccessibilityActions
    createActions (
        const std::shared_ptr<
            EventOverlayAccessibilityState>&
            state)
    {
        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::
                    toggle,
                [state]
                {
                    if (! state
                              ->isAvailable())
                    {
                        return;
                    }

                    auto* messageManager =
                        MessageManager::
                            getInstanceWithoutCreating();
                    if (messageManager
                        == nullptr)
                    {
                        return;
                    }

                    const auto perform =
                        [state]
                        {
                            state
                                ->toggleOnMessageThread();
                        };
                    if (messageManager
                            ->isThisTheMessageThread())
                    {
                        perform();
                        return;
                    }
                    MessageManager::callSync (
                        perform);
                });
    }

    std::shared_ptr<
        EventOverlayAccessibilityState>
        state;
};
} // namespace

class LfpViewer::EventOverlayButton final
    : public UtilityButton
{
public:
    EventOverlayButton (
        String label,
        EventDisplayInterface&
            ownerToUse)
        : UtilityButton (
              std::move (label)),
          owner (
              ownerToUse),
          accessibilityState (
              std::make_shared<
                  EventOverlayAccessibilityState>())
    {
        accessibilityState->attach (
            this);
        refreshAccessibilityState();
    }

    ~EventOverlayButton() override
    {
        accessibilityState->detach();
    }

    std::unique_ptr<
        AccessibilityHandler>
    createAccessibilityHandler()
        override
    {
        return std::make_unique<
            EventOverlayAccessibilityHandler> (
            *this,
            accessibilityState);
    }

    void refreshAccessibilityState()
    {
        auto title = getTitle();
        if (title.isEmpty())
            title = getName();

        accessibilityState
            ->synchronise (
                std::move (title),
                getDescription(),
                getHelpText(),
                owner
                    .getEventDisplayState(),
                Component::isEnabled(),
                hasKeyboardFocus (
                    false));
    }

    void toggleOverlayOnMessageThread()
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        owner.setEventDisplayState (
            ! owner
                   .getEventDisplayState());
    }

    void enablementChanged() override
    {
        UtilityButton::
            enablementChanged();
        refreshAccessibilityState();
    }

    void focusGained (
        FocusChangeType cause) override
    {
        UtilityButton::focusGained (
            cause);
        refreshAccessibilityState();
    }

    void focusLost (
        FocusChangeType cause) override
    {
        UtilityButton::focusLost (
            cause);
        refreshAccessibilityState();
    }

private:
    EventDisplayInterface& owner;
    std::shared_ptr<
        EventOverlayAccessibilityState>
        accessibilityState;
};

void EventOverlayAccessibilityState::
    toggleOnMessageThread()
{
    jassert (
        MessageManager::getInstance()
            ->isThisTheMessageThread());
    auto* currentButton =
        button.getComponent();
    if (currentButton == nullptr
        || ! currentButton
                 ->Component::
                 isEnabled())
    {
        return;
    }

    Component::SafePointer<
        EventOverlayButton>
        safeButton (
            currentButton);
    currentButton
        ->toggleOverlayOnMessageThread();
    if (safeButton != nullptr)
    {
        safeButton
            ->refreshAccessibilityState();
    }
}

EventDisplayInterface::EventDisplayInterface (
    LfpDisplay* display_,
    LfpDisplaySplitter* split,
    int chNum)
    : channelNumber (chNum),
      overlayShown (true),
      display (display_),
      canvasSplit (split)
{
    chButton =
        std::make_unique<
            EventOverlayButton> (
            String (
                channelNumber + 1),
            *this);
    chButton->setRadius (5.0f);
    chButton->addListener (this);
    addAndMakeVisible (chButton.get());

    checkEnabledState();
}

EventDisplayInterface::~EventDisplayInterface()
{
}

void EventDisplayInterface::checkEnabledState()
{
    overlayShown =
        display
            ->getEventDisplayState (
                channelNumber);
    if (chButton != nullptr)
    {
        chButton
            ->refreshAccessibilityState();
    }
    repaint();
}

void EventDisplayInterface::buttonClicked (Button* button)
{
    if (button == chButton.get())
        setEventDisplayState (
            ! getEventDisplayState());
}

void EventDisplayInterface::
    setEventDisplayState (
        bool state)
{
    jassert (
        MessageManager::getInstance()
            ->isThisTheMessageThread());
    display->setEventDisplayState (
        channelNumber,
        state);
    overlayShown = state;
    if (chButton != nullptr)
    {
        chButton
            ->refreshAccessibilityState();
        if (auto* handler =
                chButton
                    ->getAccessibilityHandler())
        {
            handler
                ->notifyAccessibilityEvent (
                    AccessibilityEvent::
                        valueChanged);
        }
    }
    repaint();
}

bool EventDisplayInterface::
    getEventDisplayState()
        const
{
    return overlayShown;
}

void EventDisplayInterface::
    applyAccessibilityMetadata (
        StringRef id,
        StringRef title,
        StringRef description)
{
    chButton->setComponentID (
        String (id));
    chButton->setTitle (
        String (title));
    chButton->setDescription (
        String (description));
    chButton->setHelpText (
        String (description));
    chButton->setAccessible (
        true);
    chButton
        ->invalidateAccessibilityHandler();
    chButton
        ->refreshAccessibilityState();
}

void EventDisplayInterface::paintOverChildren (Graphics& g)
{
    if (overlayShown)
    {
        g.setColour (display->channelColours[channelNumber * 2]);
        g.drawRoundedRectangle (1, 1, getWidth() - 2, getHeight() - 2, 5.0f, 3.0f);
    }
}

void EventDisplayInterface::resized()
{
    chButton->setBounds (0, 0, getWidth(), getHeight());
}
