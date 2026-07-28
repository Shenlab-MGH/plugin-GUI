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

#include "ShowHideOptionsButton.h"

#include "LfpDisplayOptions.h"

#include <math.h>
#include <mutex>
#include <optional>

using namespace LfpViewer;

struct LfpViewer::
    ShowHideOptionsButtonAccessibilityState
{
    void attach (
        ShowHideOptionsButton*
            buttonToUse)
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
        open.store (false);
        focused.store (false);
    }

    void synchronise (
        String titleToUse,
        String descriptionToUse,
        String helpToUse,
        bool isOpen,
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
        open.store (isOpen);
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

    bool isOpen() const
    {
        return open.load();
    }

    bool isAvailable() const
    {
        return enabled.load();
    }

    bool isFocused() const
    {
        return focused.load();
    }

    void applyOnMessageThread (
        std::optional<bool>
            desiredState)
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        auto* currentButton =
            button.getComponent();
        if (currentButton == nullptr
            || ! currentButton
                     ->isEnabled())
        {
            return;
        }

        const auto targetState =
            desiredState.value_or (
                ! currentButton
                      ->getToggleState());
        if (currentButton
                ->getToggleState()
            == targetState)
        {
            currentButton
                ->refreshAccessibilityState();
            return;
        }

        Component::SafePointer<
            ShowHideOptionsButton>
            safeButton (
                currentButton);
        currentButton
            ->setToggleState (
                targetState,
                sendNotificationSync);
        if (safeButton != nullptr)
        {
            safeButton
                ->refreshAccessibilityState();
        }
    }

private:
    mutable std::mutex textMutex;
    String title;
    String description;
    String help;
    std::atomic<bool>
        open { false };
    std::atomic<bool>
        enabled { true };
    std::atomic<bool>
        focused { false };
    Component::SafePointer<
        ShowHideOptionsButton>
        button;
};

namespace
{
class OptionsDrawerAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit OptionsDrawerAccessibilityValue (
        std::shared_ptr<
            ShowHideOptionsButtonAccessibilityState>
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
        return state->isOpen()
                   ? String ("Open")
                   : String ("Closed");
    }

private:
    std::shared_ptr<
        ShowHideOptionsButtonAccessibilityState>
        state;
};

class OptionsDrawerAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    OptionsDrawerAccessibilityHandler (
        ShowHideOptionsButton& button,
        std::shared_ptr<
            ShowHideOptionsButtonAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              AccessibilityRole::
                  button,
              createActions (
                  stateToUse),
              AccessibilityHandler::Interfaces {
                  std::make_unique<
                      OptionsDrawerAccessibilityValue> (
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
                .withExpandable();
        current =
            state->isOpen()
                ? current
                      .withExpanded()
                : current
                      .withCollapsed();
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
            ShowHideOptionsButtonAccessibilityState>&
            state)
    {
        const auto invoke =
            [state] (
                std::optional<bool>
                    desiredState)
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
                    [state,
                     desiredState]
                    {
                        state
                            ->applyOnMessageThread (
                                desiredState);
                    };
                if (messageManager
                        ->isThisTheMessageThread())
                {
                    perform();
                    return;
                }
                MessageManager::callSync (
                    perform);
            };

        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::
                    press,
                [invoke]
                {
                    invoke (
                        std::nullopt);
                })
            .addAction (
                AccessibilityActionType::
                    expand,
                [invoke]
                {
                    invoke (
                        true);
                })
            .addAction (
                AccessibilityActionType::
                    collapse,
                [invoke]
                {
                    invoke (
                        false);
                });
    }

    std::shared_ptr<
        ShowHideOptionsButtonAccessibilityState>
        state;
};
} // namespace

ShowHideOptionsButton::
    ShowHideOptionsButton (
        LfpDisplayOptions*)
    : Button ("Button"),
      accessibilityState (
          std::make_shared<
              ShowHideOptionsButtonAccessibilityState>())
{
    accessibilityState->attach (
        this);
    setClickingTogglesState (true);
    refreshAccessibilityState();
}

ShowHideOptionsButton::~ShowHideOptionsButton()
{
    accessibilityState->detach();
}

std::unique_ptr<AccessibilityHandler>
ShowHideOptionsButton::
    createAccessibilityHandler()
{
    return std::make_unique<
        OptionsDrawerAccessibilityHandler> (
        *this,
        accessibilityState);
}

void ShowHideOptionsButton::
    refreshAccessibilityState()
{
    auto title = getTitle();
    if (title.isEmpty())
        title = getName();
    auto help = getHelpText();
    if (help.isEmpty())
        help = getDescription();

    accessibilityState
        ->synchronise (
            std::move (title),
            getDescription(),
            std::move (help),
            getToggleState(),
            Component::isEnabled(),
            hasKeyboardFocus (
                false));
}

void ShowHideOptionsButton::
    buttonStateChanged()
{
    Button::buttonStateChanged();
    refreshAccessibilityState();
}

void ShowHideOptionsButton::
    enablementChanged()
{
    Button::enablementChanged();
    refreshAccessibilityState();
}

void ShowHideOptionsButton::
    focusGained (
        FocusChangeType cause)
{
    Button::focusGained (
        cause);
    refreshAccessibilityState();
}

void ShowHideOptionsButton::
    focusLost (
        FocusChangeType cause)
{
    Button::focusLost (
        cause);
    refreshAccessibilityState();
}

void ShowHideOptionsButton::paintButton (Graphics& g, bool, bool)
{
    g.setColour (findColour (ThemeColours::defaultText));

    Path p;

    float h = getHeight();
    float w = getWidth();

    if (getToggleState())
    {
        p.addTriangle (0.5f * w, 0.2f * h, 0.2f * w, 0.8f * h, 0.8f * w, 0.8f * h);
    }
    else
    {
        p.addTriangle (0.8f * w, 0.8f * h, 0.2f * w, 0.5f * h, 0.8f * w, 0.2f * h);
    }

    g.fillPath (p.createPathWithRoundedCorners(3.0f));
}
