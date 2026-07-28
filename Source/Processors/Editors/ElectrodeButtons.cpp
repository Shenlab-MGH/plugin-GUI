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

#include "ElectrodeButtons.h"
#include "../../UI/LookAndFeel/CustomLookAndFeel.h"
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace
{
struct ElectrodeButtonAccessibilityState
{
    void attach (
        ElectrodeButton* buttonToUse)
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
        focused.store (false);
    }

    ElectrodeButton*
    getButtonOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return button;
    }

    void synchronise (
        String valueToUse,
        String titleToUse,
        String descriptionToUse,
        String helpToUse,
        bool isToggleable,
        bool isChecked,
        bool isEnabled,
        bool isFocused)
    {
        {
            const std::lock_guard<std::mutex>
                lock (textMutex);
            value = std::move (valueToUse);
            title = std::move (titleToUse);
            description =
                std::move (descriptionToUse);
            help = std::move (helpToUse);
        }

        toggleable.store (isToggleable);
        checked.store (isChecked);
        enabled.store (isEnabled);
        focused.store (isFocused);
    }

    String getValue() const
    {
        const std::lock_guard<std::mutex>
            lock (textMutex);
        return value;
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

    bool isToggleable() const
    {
        return toggleable.load();
    }

    bool isChecked() const
    {
        return checked.load();
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
    std::atomic<bool> toggleable { true };
    std::atomic<bool> checked { true };
    std::atomic<bool> enabled { true };
    std::atomic<bool> focused { false };
    ElectrodeButton* button = nullptr;
};

struct ElectrodeButtonAccessibilityRegistry
{
    std::mutex mutex;
    std::unordered_map<
        ElectrodeButton*,
        std::shared_ptr<
            ElectrodeButtonAccessibilityState>>
        states;
};

ElectrodeButtonAccessibilityRegistry&
getElectrodeButtonAccessibilityRegistry()
{
    static auto* registry =
        new ElectrodeButtonAccessibilityRegistry();
    return *registry;
}

std::shared_ptr<
    ElectrodeButtonAccessibilityState>
registerElectrodeButton (
    ElectrodeButton* button)
{
    auto state =
        std::make_shared<
            ElectrodeButtonAccessibilityState>();
    state->attach (button);

    auto& registry =
        getElectrodeButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    registry.states[button] = state;
    return state;
}

std::shared_ptr<
    ElectrodeButtonAccessibilityState>
getElectrodeButtonState (
    ElectrodeButton* button)
{
    auto& registry =
        getElectrodeButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    const auto found =
        registry.states.find (button);
    return found != registry.states.end()
               ? found->second
               : nullptr;
}

void unregisterElectrodeButton (
    ElectrodeButton* button)
{
    std::shared_ptr<
        ElectrodeButtonAccessibilityState>
        state;
    {
        auto& registry =
            getElectrodeButtonAccessibilityRegistry();
        const std::lock_guard<std::mutex>
            lock (registry.mutex);
        const auto found =
            registry.states.find (button);
        if (found == registry.states.end())
            return;

        state = found->second;
        registry.states.erase (found);
    }

    state->detach();
}

class ElectrodeButtonAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit ElectrodeButtonAccessibilityValue (
        std::shared_ptr<
            ElectrodeButtonAccessibilityState>
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
        return state->getValue();
    }

private:
    std::shared_ptr<
        ElectrodeButtonAccessibilityState>
        state;
};

class ElectrodeButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    ElectrodeButtonAccessibilityHandler (
        ElectrodeButton& button,
        std::shared_ptr<
            ElectrodeButtonAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              button.getRadioGroupId() != 0
                  ? AccessibilityRole::radioButton
                  : AccessibilityRole::button,
              createActions (
                  stateToUse,
                  button
                          .getRadioGroupId()
                      != 0),
              AccessibilityHandler::Interfaces {
                  std::make_unique<
                      ElectrodeButtonAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

    AccessibleState
    getCurrentState() const override
    {
        auto current =
            AccessibleState().withFocusable();

        if (state->isToggleable())
        {
            current =
                current.withCheckable();
            if (state->isChecked())
                current =
                    current.withChecked();
        }

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
    static void runOnMessageThread (
        const std::shared_ptr<
            ElectrodeButtonAccessibilityState>&
            state,
        bool toggle)
    {
        auto* messageManager =
            MessageManager::
                getInstanceWithoutCreating();
        if (messageManager == nullptr)
            return;

        messageManager->callSync (
            [state, toggle]
            {
                auto* button =
                    state
                        ->getButtonOnMessageThread();
                if (button == nullptr
                    || ! button->isEnabled()
                    || (toggle
                        && ! button
                                ->isToggleable())
                    || (toggle
                        && button
                               ->getRadioGroupId()
                           != 0))
                {
                    return;
                }

                if (toggle)
                {
                    button->setToggleState (
                        ! button
                               ->getToggleState(),
                        sendNotification);
                }
                else
                {
                    button->triggerClick();
                }
            });
    }

    static AccessibilityActions
    createActions (
        const std::shared_ptr<
            ElectrodeButtonAccessibilityState>&
            state,
        bool isRadioButton)
    {
        auto actions =
            AccessibilityActions()
                .addAction (
                    AccessibilityActionType::
                        press,
                    [state]
                    {
                        runOnMessageThread (
                            state,
                            false);
                    });

        if (state->isToggleable()
            && ! isRadioButton)
        {
            actions = actions.addAction (
                AccessibilityActionType::
                    toggle,
                [state]
                {
                    runOnMessageThread (
                        state,
                        true);
                });
        }

        return actions;
    }

    std::shared_ptr<
        ElectrodeButtonAccessibilityState>
        state;
};
} // namespace

ElectrodeButton::ElectrodeButton (
    int chan_,
    Colour defaultColour_)
    : Button ("Electrode"),
      chan (chan_),
      defaultColour (defaultColour_)
{
    registerElectrodeButton (this);
    setClickingTogglesState (true);
    setToggleState (
        true,
        dontSendNotification);
    setButtonText (String (chan_));
    refreshAccessibilityState();
}

ElectrodeButton::~ElectrodeButton()
{
    unregisterElectrodeButton (this);
}

std::unique_ptr<AccessibilityHandler>
ElectrodeButton::createAccessibilityHandler()
{
    auto state =
        getElectrodeButtonState (this);
    if (state == nullptr)
        state =
            registerElectrodeButton (this);

    refreshAccessibilityState();
    return std::make_unique<
        ElectrodeButtonAccessibilityHandler> (
        *this,
        std::move (state));
}

int ElectrodeButton::getChannelNum()
{
    return chan;
}

void ElectrodeButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    Colour bgColour = findColour (ThemeColours::widgetBackground);

    if (getToggleState())
        bgColour = findColour (ThemeColours::highlightedFill);

    auto baseColour = bgColour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f);

    if (isButtonDown || isMouseOver)
        baseColour = baseColour.contrasting (isButtonDown ? 0.2f : 0.1f);

    g.setColour (baseColour);
    g.fillRect (0, 0, getWidth(), getHeight());

    g.setColour (findColour (ThemeColours::outline).withAlpha (isEnabled() ? 1.0f : 0.5f));
    g.drawRect (0, 0, getWidth(), getHeight(), 1.0);

    g.setColour (baseColour.contrasting().withAlpha (isEnabled() ? 1.0f : 0.5f));

    if (chan < 100)
        g.setFont (10.f);
    else
        g.setFont (8.f);

    if (chan >= 0)
        g.drawText (getButtonText(),
                    0,
                    0,
                    getWidth(),
                    getHeight(),
                    Justification::centred,
                    true);
}

void ElectrodeButton::setChannelNum (int i)
{
    chan = i;

    setButtonText (String (chan));
    refreshAccessibilityState();

    if (auto* handler = getAccessibilityHandler())
    {
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::titleChanged);
    }
}

void ElectrodeButton::
    refreshAccessibilityState()
{
    jassert (
        MessageManager::getInstance()
            ->isThisTheMessageThread());
    auto state =
        getElectrodeButtonState (this);
    if (state == nullptr)
        return;

    const auto value =
        chan >= 0 ? String (chan) : "None";
    auto title = getTitle();
    if (title.isEmpty())
        title = getButtonText();
    auto help = getTooltip();
    if (help.isEmpty())
        help = getHelpText();
    if (help.isEmpty())
        help = getDescription();

    state->synchronise (
        value,
        std::move (title),
        getDescription(),
        std::move (help),
        isToggleable(),
        getToggleState(),
        Component::isEnabled(),
        hasKeyboardFocus (false));
}

void ElectrodeButton::buttonStateChanged()
{
    Button::buttonStateChanged();
    refreshAccessibilityState();
}

void ElectrodeButton::enablementChanged()
{
    Button::enablementChanged();
    refreshAccessibilityState();
}

void ElectrodeButton::focusGained (
    FocusChangeType cause)
{
    Button::focusGained (cause);
    refreshAccessibilityState();
}

void ElectrodeButton::focusLost (
    FocusChangeType cause)
{
    Button::focusLost (cause);
    refreshAccessibilityState();
}

ElectrodeEditorButton::ElectrodeEditorButton (const String& name_) : Button ("Electrode Editor"),
                                                                     name (name_)
{
    if (name.equalsIgnoreCase ("edit") || name.equalsIgnoreCase ("monitor"))
        setClickingTogglesState (true);
}

ElectrodeEditorButton::~ElectrodeEditorButton() {}

void ElectrodeEditorButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    g.setFont (FontOptions ("Silkscreen", "Regular", 14));

    if (getToggleState() == true)
        g.setColour (Colours::darkgrey);
    else
        g.setColour (Colours::lightgrey);

    g.drawText (name, 0, 0, getWidth(), getHeight(), Justification::left, true);
}
