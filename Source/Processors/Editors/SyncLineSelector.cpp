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

#include "SyncLineSelector.h"
#include "../../UI/LookAndFeel/CustomLookAndFeel.h"
#include "../../UI/SemanticComponent.h"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SyncChannelButtonAccessibilityState
{
    explicit SyncChannelButtonAccessibilityState (
        bool canSelectNoneToUse)
        : canSelectNone (canSelectNoneToUse)
    {
    }

    void attach (
        SyncChannelButton* buttonToUse,
        SyncLineSelector* selectorToUse)
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        button = buttonToUse;
        selector = selectorToUse;
    }

    void detach()
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        button = nullptr;
        selector = nullptr;
    }

    void synchronise (
        bool isSelected,
        bool isEnabled,
        bool isFocused,
        String titleToUse,
        String descriptionToUse,
        String helpToUse)
    {
        selected.store (isSelected);
        enabled.store (isEnabled);
        focused.store (isFocused);

        const std::lock_guard<std::mutex>
            lock (textMutex);
        title = std::move (titleToUse);
        description = std::move (descriptionToUse);
        help = std::move (helpToUse);
    }

    SyncChannelButton* getButtonOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return button;
    }

    SyncLineSelector* getSelectorOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return selector;
    }

    bool isSelected() const { return selected.load(); }
    bool isEnabled() const { return enabled.load(); }
    bool isFocused() const { return focused.load(); }
    bool allowsNoSelection() const { return canSelectNone; }

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

private:
    mutable std::mutex textMutex;
    String title;
    String description;
    String help;
    std::atomic<bool> selected { false };
    std::atomic<bool> enabled { true };
    std::atomic<bool> focused { false };
    const bool canSelectNone;
    SyncChannelButton* button = nullptr;
    SyncLineSelector* selector = nullptr;
};

struct SetPrimaryButtonAccessibilityState
{
    void attach (
        SetPrimaryButton* buttonToUse,
        SyncLineSelector* selectorToUse)
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        button = buttonToUse;
        selector = selectorToUse;
    }

    void detach()
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        button = nullptr;
        selector = nullptr;
    }

    void synchronise (
        bool isEnabled,
        bool isFocused,
        String titleToUse,
        String descriptionToUse,
        String helpToUse)
    {
        enabled.store (isEnabled);
        focused.store (isFocused);

        const std::lock_guard<std::mutex>
            lock (textMutex);
        title = std::move (titleToUse);
        description = std::move (descriptionToUse);
        help = std::move (helpToUse);
    }

    SetPrimaryButton* getButtonOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return button;
    }

    SyncLineSelector* getSelectorOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return selector;
    }

    bool isEnabled() const { return enabled.load(); }
    bool isFocused() const { return focused.load(); }

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

private:
    mutable std::mutex textMutex;
    String title;
    String description;
    String help;
    std::atomic<bool> enabled { true };
    std::atomic<bool> focused { false };
    SetPrimaryButton* button = nullptr;
    SyncLineSelector* selector = nullptr;
};

namespace
{
struct SyncButtonAccessibilityRegistry
{
    std::mutex mutex;
    std::unordered_map<
        SyncChannelButton*,
        std::shared_ptr<
            SyncChannelButtonAccessibilityState>>
        syncChannelButtonStates;
    std::unordered_map<
        SetPrimaryButton*,
        std::shared_ptr<
            SetPrimaryButtonAccessibilityState>>
        setPrimaryButtonStates;
};

SyncButtonAccessibilityRegistry&
getSyncButtonAccessibilityRegistry()
{
    static auto* registry =
        new SyncButtonAccessibilityRegistry();
    return *registry;
}

std::shared_ptr<
    SyncChannelButtonAccessibilityState>
registerSyncChannelButton (
    SyncChannelButton* button,
    SyncLineSelector* selector)
{
    auto state =
        std::make_shared<
            SyncChannelButtonAccessibilityState> (
            selector->allowsNoSelection());
    state->attach (button, selector);

    auto& registry =
        getSyncButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    registry.syncChannelButtonStates[button] =
        state;
    return state;
}

std::shared_ptr<
    SyncChannelButtonAccessibilityState>
getSyncChannelButtonState (
    SyncChannelButton* button)
{
    auto& registry =
        getSyncButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    const auto found =
        registry.syncChannelButtonStates.find (
            button);
    return found
                   != registry.syncChannelButtonStates.end()
               ? found->second
               : nullptr;
}

void unregisterSyncChannelButton (
    SyncChannelButton* button)
{
    std::shared_ptr<
        SyncChannelButtonAccessibilityState>
        state;
    {
        auto& registry =
            getSyncButtonAccessibilityRegistry();
        const std::lock_guard<std::mutex>
            lock (registry.mutex);
        const auto found =
            registry.syncChannelButtonStates.find (
                button);
        if (found
            == registry.syncChannelButtonStates.end())
            return;

        state = found->second;
        registry.syncChannelButtonStates.erase (
            found);
    }
    state->detach();
}

std::shared_ptr<
    SetPrimaryButtonAccessibilityState>
registerSetPrimaryButton (
    SetPrimaryButton* button)
{
    auto state =
        std::make_shared<
            SetPrimaryButtonAccessibilityState>();
    state->attach (button, nullptr);

    auto& registry =
        getSyncButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    registry.setPrimaryButtonStates[button] =
        state;
    return state;
}

std::shared_ptr<
    SetPrimaryButtonAccessibilityState>
getSetPrimaryButtonState (
    SetPrimaryButton* button)
{
    auto& registry =
        getSyncButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    const auto found =
        registry.setPrimaryButtonStates.find (
            button);
    return found
                   != registry.setPrimaryButtonStates.end()
               ? found->second
               : nullptr;
}

void attachSetPrimaryButton (
    SetPrimaryButton* button,
    SyncLineSelector* selector)
{
    if (auto state =
            getSetPrimaryButtonState (
                button))
    {
        state->attach (
            button,
            selector);
    }
}

void unregisterSetPrimaryButton (
    SetPrimaryButton* button)
{
    std::shared_ptr<
        SetPrimaryButtonAccessibilityState>
        state;
    {
        auto& registry =
            getSyncButtonAccessibilityRegistry();
        const std::lock_guard<std::mutex>
            lock (registry.mutex);
        const auto found =
            registry.setPrimaryButtonStates.find (
                button);
        if (found
            == registry.setPrimaryButtonStates.end())
            return;

        state = found->second;
        registry.setPrimaryButtonStates.erase (
            found);
    }
    state->detach();
}

class SyncChannelButtonAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit SyncChannelButtonAccessibilityValue (
        std::shared_ptr<SyncChannelButtonAccessibilityState>
            stateToUse)
        : state (std::move (stateToUse))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValueAsString (const String&) override { jassertfalse; }
    String getCurrentValueAsString() const override
    {
        return state->isSelected()
                   ? String ("Selected")
                   : String ("Not selected");
    }

private:
    std::shared_ptr<SyncChannelButtonAccessibilityState>
        state;
};

class SyncChannelButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    SyncChannelButtonAccessibilityHandler (
        SyncChannelButton& button,
        std::shared_ptr<SyncChannelButtonAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              stateToUse->allowsNoSelection()
                  ? AccessibilityRole::toggleButton
                  : AccessibilityRole::radioButton,
              createActions (stateToUse),
              AccessibilityHandler::Interfaces {
                  std::make_unique<
                      SyncChannelButtonAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto current =
            AccessibleState()
                .withFocusable()
                .withCheckable()
                .withSelectable();
        if (state->isFocused())
            current = current.withFocused();
        if (state->isSelected())
            current = current.withChecked()
                          .withSelected();
        return current;
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
        const std::shared_ptr<
            SyncChannelButtonAccessibilityState>& state)
    {
        const auto actionType =
            state->allowsNoSelection()
                ? AccessibilityActionType::toggle
                : AccessibilityActionType::press;

        return AccessibilityActions().addAction (
            actionType,
            [state]
            {
                auto* messageManager =
                    MessageManager::getInstanceWithoutCreating();
                if (messageManager == nullptr)
                    return;

                messageManager->callSync (
                    [state]
                    {
                        auto* button =
                            state->getButtonOnMessageThread();
                        auto* selector =
                            state->getSelectorOnMessageThread();
                        if (button == nullptr
                            || selector == nullptr
                            || ! button->isEnabled())
                        {
                            return;
                        }

                        selector->buttonClicked (button);
                    });
            });
    }

    std::shared_ptr<SyncChannelButtonAccessibilityState>
        state;
};

class SetPrimaryButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    SetPrimaryButtonAccessibilityHandler (
        SetPrimaryButton& button,
        std::shared_ptr<SetPrimaryButtonAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              AccessibilityRole::button,
              createActions (stateToUse)),
          state (std::move (stateToUse))
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto current =
            AccessibleState().withFocusable();
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
        const std::shared_ptr<
            SetPrimaryButtonAccessibilityState>& state)
    {
        return AccessibilityActions().addAction (
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
                        auto* button =
                            state->getButtonOnMessageThread();
                        auto* selector =
                            state->getSelectorOnMessageThread();
                        if (button == nullptr
                            || ! button->isEnabled())
                        {
                            return;
                        }

                        if (selector != nullptr)
                            selector->buttonClicked (button);
                        else
                            button->triggerClick();
                    });
            });
    }

    std::shared_ptr<SetPrimaryButtonAccessibilityState>
        state;
};
} // namespace

SyncChannelButton::SyncChannelButton (
    int _id,
    SyncLineSelector* _parent)
    : Button (String (_id)),
      id (_id),
      parent (_parent)
{
    registerSyncChannelButton (
        this,
        parent);
    btnColour = parent->lineColours[(id - 1) % parent->lineColours.size()];
}

SyncChannelButton::~SyncChannelButton()
{
    unregisterSyncChannelButton (
        this);
}

std::unique_ptr<AccessibilityHandler>
SyncChannelButton::createAccessibilityHandler()
{
    auto state =
        getSyncChannelButtonState (
            this);
    if (state == nullptr)
        return Button::
            createAccessibilityHandler();

    return std::make_unique<
        SyncChannelButtonAccessibilityHandler> (
        *this,
        std::move (state));
}

void SyncChannelButton::buttonStateChanged()
{
    Button::buttonStateChanged();
    synchroniseAccessibilityState();
}

void SyncChannelButton::enablementChanged()
{
    Button::enablementChanged();
    synchroniseAccessibilityState();
}

void SyncChannelButton::focusGained (
    Component::FocusChangeType cause)
{
    Button::focusGained (cause);
    synchroniseAccessibilityState();
}

void SyncChannelButton::focusLost (
    Component::FocusChangeType cause)
{
    Button::focusLost (cause);
    synchroniseAccessibilityState();
}

void SyncChannelButton::synchroniseAccessibilityState()
{
    auto state =
        getSyncChannelButtonState (
            this);
    if (state == nullptr)
        return;

    const auto description =
        getToggleState()
            ? "Selected line "
                  + String (id)
                  + "."
            : "Select line "
                  + String (id)
                  + ".";
    setDescription (description);
    setHelpText (description);

    state->synchronise (
        getToggleState(),
        isEnabled(),
        hasKeyboardFocus (false),
        getTitle(),
        description,
        description);
}

void SyncChannelButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    g.setColour (findColour (ThemeColours::outline));
    g.fillRoundedRectangle (0.0f, 0.0f, getWidth(), getHeight(), 0.001 * getWidth());

    if (isMouseOver)
    {
        if (getToggleState())
            g.setColour (btnColour.brighter());
        else
            g.setColour (findColour (ThemeColours::widgetBackground).contrasting (0.3f));
    }
    else
    {
        if (getToggleState())
            g.setColour (btnColour);
        else
            g.setColour (findColour (ThemeColours::widgetBackground));
    }
    g.fillRoundedRectangle (1, 1, getWidth() - 2, getHeight() - 2, 0.001 * getWidth());

    //Draw text string in middle of button
    if (getToggleState())
        g.setColour (btnColour.contrasting());
    else
        g.setColour (findColour (ThemeColours::defaultText));

    g.setFont (FontOptions ("Inter", "Regular", 10.0f));
    g.drawText (String (id), 0, 0, getWidth(), getHeight(), Justification::centred);
}

SetPrimaryButton::SetPrimaryButton (
    const String& name)
    : Button (name)
{
    registerSetPrimaryButton (
        this);
    synchroniseAccessibilityState();
}

SetPrimaryButton::~SetPrimaryButton()
{
    unregisterSetPrimaryButton (
        this);
}

std::unique_ptr<AccessibilityHandler>
SetPrimaryButton::createAccessibilityHandler()
{
    auto state =
        getSetPrimaryButtonState (
            this);
    if (state == nullptr)
        return Button::
            createAccessibilityHandler();

    return std::make_unique<
        SetPrimaryButtonAccessibilityHandler> (
        *this,
        std::move (state));
}

void SetPrimaryButton::enablementChanged()
{
    Button::enablementChanged();
    synchroniseAccessibilityState();
}

void SetPrimaryButton::focusGained (
    Component::FocusChangeType cause)
{
    Button::focusGained (cause);
    synchroniseAccessibilityState();
}

void SetPrimaryButton::focusLost (
    Component::FocusChangeType cause)
{
    Button::focusLost (cause);
    synchroniseAccessibilityState();
}

void SetPrimaryButton::synchroniseAccessibilityState()
{
    auto state =
        getSetPrimaryButtonState (
            this);
    if (state == nullptr)
        return;

    state->synchronise (
        isEnabled(),
        hasKeyboardFocus (false),
        getTitle().isNotEmpty()
            ? getTitle()
            : getButtonText(),
        getDescription(),
        getHelpText());
}

void SetPrimaryButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    Colour backgroundColour = findColour (ThemeColours::widgetBackground);

    if (getToggleState())
        backgroundColour = findColour (ThemeColours::highlightedFill);

    if (isMouseOver)
        backgroundColour = backgroundColour.contrasting (0.1f);

    backgroundColour.withAlpha (isEnabled() ? 1.0f : 0.5f);

    g.setColour (backgroundColour);
    g.fillRoundedRectangle (1.0f, 1.0f, (float) getWidth() - 2.0f, (float) getHeight() - 2.0f, 2.0f);

    g.setColour (findColour (ThemeColours::outline).withAlpha (isEnabled() ? 1.0f : 0.5f));
    g.drawRoundedRectangle (0.0f, 0.0f, (float) getWidth(), (float) getHeight(), 2.0f, 1.0f);

    g.setColour (findColour (ThemeColours::defaultText).withAlpha (isEnabled() ? 1.0f : 0.5f));
    g.setFont (FontOptions ("Inter", "Regular", 12.0f));
    g.drawText (String (getName()), 0, 0, getWidth(), getHeight(), Justification::centred);
}

//SyncLineSelector::SyncLineSelector(int nChans, int selectedIdx, bool isPrimary_)
SyncLineSelector::SyncLineSelector (Component* parent, SyncLineSelector::Listener* listener_, int numChans, int selectedLine_, bool isPrimary_, bool canSelectNone_)
    : PopupComponent (parent),
      listener (listener_),
      isPrimary (isPrimary_),
      nChannels (numChans),
      detectedChange (false),
      selectedLine (selectedLine_),
      canSelectNone (canSelectNone_)
{
    lineColours.add (Colour (224, 185, 36));
    lineColours.add (Colour (243, 119, 33));
    lineColours.add (Colour (237, 37, 36));
    lineColours.add (Colour (217, 46, 171));
    lineColours.add (Colour (101, 31, 255));
    lineColours.add (Colour (48, 117, 255));
    lineColours.add (Colour (116, 227, 156));
    lineColours.add (Colour (82, 173, 0));

    width = 368; //can use any multiples of 16 here for dynamic resizing

    int nColumns = 16;
    nRows = nChannels / nColumns + (int) (! (nChannels % nColumns == 0));
    buttonSize = width / 16;
    height = buttonSize * nRows;

    for (int i = 0; i < nRows; i++)
    {
        for (int j = 0; j < nColumns; j++)
        {
            if (nColumns * i + j < nChannels)
            {
                int buttonIdx = nColumns * i + j;
                buttons.add (
                    new SyncChannelButton (
                        nColumns * i + j + 1,
                        this));
                buttons.getLast()->setBounds (width / nColumns * j, height / nRows * i, buttonSize, buttonSize);
                buttons.getLast()->setToggleState ((buttonIdx == selectedLine ? true : false), NotificationType::dontSendNotification);
                buttons.getLast()->addListener (this);
                addChildAndSetID (buttons.getLast(), String (buttonIdx));
            }
        }
    }

    if (! isPrimary)
    {
        setPrimaryStreamButton =
            new SetPrimaryButton (
                "Set as main clock");
        setPrimaryStreamButton->setBounds (0, height, 0.5 * width, width / nColumns);
        setPrimaryStreamButton->addListener (this);
        addChildAndSetID (setPrimaryStreamButton, "SETPRIMARY");
        configurePrimaryButtonAccessibility();

        if (canSelectNone && selectedLine == -1)
            setPrimaryStreamButton->setEnabled (false);
    }
    else
    {
        height = buttonSize * (nRows - 1);
    }

    if (nChannels <= 8)
        width /= 2;

    setSize (width, height + buttonSize);
    setColour (ColourSelector::backgroundColourId, Colours::transparentBlack);
    updateAccessibilityMetadata();
}

SyncLineSelector::~SyncLineSelector() {}

void SyncLineSelector::mouseMove (const MouseEvent& event) {}

void SyncLineSelector::mouseDown (const MouseEvent& event) {}

void SyncLineSelector::mouseUp (const MouseEvent& event) {}

void SyncLineSelector::buttonClicked (Button* button)
{
    if (button == setPrimaryStreamButton.get())
    {
        setSize (width, buttonSize * nRows);
        height = buttonSize * (nRows);
        isPrimary = true;
        setPrimaryStreamButton->setVisible (false);
        detectedChange = true;
        updateAccessibilityMetadata();

        listener->primaryStreamChanged();
        return;
    }

    auto* channelButton =
        dynamic_cast<SyncChannelButton*> (
            button);
    if (channelButton == nullptr)
        return;

    bool sameButton = false;

    for (int i = 0; i < buttons.size(); i++)
    {
        if (buttons[i]->getToggleState()
            && buttons[i] == button)
        {
            sameButton = true;
        }

        buttons[i]->setToggleState (
            false,
            dontSendNotification);
    }

    if (canSelectNone && sameButton)
    {
        selectedLine = -1;

        if (! isPrimary
            && setPrimaryStreamButton != nullptr)
        {
            setPrimaryStreamButton
                ->setEnabled (false);
        }
    }
    else
    {
        button->setToggleState (
            true,
            dontSendNotification);
        selectedLine =
            channelButton->getId() - 1;

        if (! isPrimary
            && setPrimaryStreamButton != nullptr)
        {
            setPrimaryStreamButton
                ->setEnabled (true);
        }
    }

    detectedChange = true;
    updateAccessibilityMetadata();

    const auto publishedLine =
        selectedLine;
    Component::SafePointer<
        SyncLineSelector>
        safeThis (this);
    listener->selectedLineChanged (
        publishedLine);
    if (safeThis == nullptr)
        return;

    safeThis->updatePopup();
}

void SyncLineSelector::updatePopup()
{
    selectedLine = listener->getSelectedLine();
    for (int i = 0; i < buttons.size(); i++)
    {
        if (buttons[i]->getId() - 1 == selectedLine)
            buttons[i]->setToggleState (true, NotificationType::dontSendNotification);
        else
            buttons[i]->setToggleState (false, NotificationType::dontSendNotification);
    }

    if (listener->isPrimaryStream() && setPrimaryStreamButton != nullptr)
    {
        setPrimaryStreamButton->setVisible (false);
        isPrimary = true;
        setSize (width, buttonSize * nRows);
        height = buttonSize * (nRows);
    }
    else if (! listener->isPrimaryStream())
    {
        height = buttonSize * (nRows);
        if (setPrimaryStreamButton == nullptr)
        {
            setPrimaryStreamButton =
                new SetPrimaryButton (
                    "Set as main clock");
            setPrimaryStreamButton->setBounds (0, height, 0.5 * width, buttonSize);
            setPrimaryStreamButton->addListener (this);
            addChildAndSetID (setPrimaryStreamButton, "SETPRIMARY");
            configurePrimaryButtonAccessibility();
        }

        setPrimaryStreamButton->setVisible (true);
        setSize (width, height + buttonSize);
        isPrimary = false;

        setPrimaryStreamButton->setEnabled (
            ! canSelectNone
            || selectedLine != -1);
    }

    updateAccessibilityMetadata();
}

void SyncLineSelector::updateAccessibilityMetadata()
{
    const auto semanticId =
        getAccessibilitySemanticId();
    if (getComponentID() != semanticId)
    {
        applySemanticMetadata (
            *this,
            semanticId,
            "Line selector",
            "Choose a TTL or synchronization line.");
    }

    for (auto* button : buttons)
    {
        const auto lineNumber = button->getId();
        const auto selected = button->getToggleState();
        const auto buttonId =
            semanticId + ".line_"
            + String (lineNumber);
        const auto title =
            "Line " + String (lineNumber);
        const auto description =
            selected
                ? "Selected line "
                      + String (lineNumber)
                      + "."
                : "Select line "
                      + String (lineNumber)
                      + ".";

        if (button->getComponentID()
            != buttonId)
        {
            applySemanticMetadata (
                *button,
                buttonId,
                title,
                description,
                description);
        }
        else
        {
            button->setTitle (title);
            button->setDescription (
                description);
            button->setHelpText (
                description);
        }

        button
            ->synchroniseAccessibilityState();
    }

    if (setPrimaryStreamButton != nullptr)
        configurePrimaryButtonAccessibility();
}

void SyncLineSelector::
    configurePrimaryButtonAccessibility()
{
    if (setPrimaryStreamButton == nullptr)
        return;

    attachSetPrimaryButton (
        setPrimaryStreamButton.get(),
        this);

    const auto semanticId =
        getAccessibilitySemanticId();
    const auto buttonId =
        semanticId + ".set_primary";
    const String title =
        "Set as main clock";
    const String description =
        "Use this stream as the main synchronization clock.";
    if (setPrimaryStreamButton
            ->getComponentID()
        != buttonId)
    {
        applySemanticMetadata (
            *setPrimaryStreamButton,
            buttonId,
            title,
            description,
            description);
    }
    else
    {
        setPrimaryStreamButton
            ->setTitle (title);
        setPrimaryStreamButton
            ->setDescription (
                description);
        setPrimaryStreamButton
            ->setHelpText (
                description);
    }

    setPrimaryStreamButton
        ->synchroniseAccessibilityState();
}

String SyncLineSelector::
    getAccessibilitySemanticId()
{
    auto* anchor = getParent();
    return anchor != nullptr
                   && isValidSemanticId (
                       anchor
                           ->getComponentID())
               ? anchor->getComponentID()
                     + ".popup"
               : "oe.popup.sync_line";
}
