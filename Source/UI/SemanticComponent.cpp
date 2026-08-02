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

#include "SemanticComponent.h"
#include <atomic>
#include <mutex>

namespace
{
class ReadOnlyProgressValue final : public AccessibilityRangedNumericValueInterface
{
public:
    explicit ReadOnlyProgressValue (std::function<double()> getValueIn)
        : getValue (std::move (getValueIn))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValue (double) override { jassertfalse; }
    double getCurrentValue() const override
    {
        return jlimit (0.0, 1.0, getValue());
    }
    AccessibleValueRange getRange() const override
    {
        return { { 0.0, 1.0 }, 0.001 };
    }

private:
    std::function<double()> getValue;
};

class ReadOnlyTextValue final : public AccessibilityTextValueInterface
{
public:
    explicit ReadOnlyTextValue (std::function<String()> getValueIn)
        : getValue (std::move (getValueIn))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValueAsString (const String&) override { jassertfalse; }
    String getCurrentValueAsString() const override { return getValue(); }

private:
    std::function<String()> getValue;
};
} // namespace

struct ReadOnlyValueTextButton::AccessibilityState
{
    void attach (
        ReadOnlyValueTextButton* buttonToUse)
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

    ReadOnlyValueTextButton*
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
    ReadOnlyValueTextButton* button = nullptr;
};

class ReadOnlyValueTextButton::
    AccessibilityHandlerImpl final
    : public AccessibilityHandler
{
public:
    AccessibilityHandlerImpl (
        ReadOnlyValueTextButton& button,
        std::shared_ptr<AccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              AccessibilityRole::button,
              createActions (stateToUse),
              Interfaces {
                  std::make_unique<
                      ReadOnlyTextValue> (
                      [stateToUse]
                      {
                          return stateToUse
                              ->getValue();
                      }) }),
          state (std::move (stateToUse))
    {
    }

    AccessibleState
    getCurrentState() const override
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
            AccessibilityState>& state)
    {
        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::press,
                [state]
                {
                    auto* messageManager =
                        MessageManager::
                            getInstanceWithoutCreating();
                    if (messageManager == nullptr)
                        return;

                    messageManager->callSync (
                        [state]
                        {
                            auto* button =
                                state
                                    ->getButtonOnMessageThread();
                            if (button == nullptr
                                || ! button
                                        ->isEnabled())
                            {
                                return;
                            }

                            button->triggerClick();
                        });
                });
    }

    std::shared_ptr<AccessibilityState>
        state;
};

bool isValidSemanticId (StringRef id)
{
    const String value (id);

    if (! value.startsWith ("oe.") || value.endsWithChar ('.'))
        return false;

    bool previousWasDot = false;

    for (int index = 0; index < value.length(); ++index)
    {
        const auto character = value[index];

        if (character == '.')
        {
            if (previousWasDot)
                return false;

            previousWasDot = true;
            continue;
        }

        previousWasDot = false;

        const bool isLowercaseLetter = character >= 'a' && character <= 'z';
        const bool isDigit = character >= '0' && character <= '9';

        if (! isLowercaseLetter && ! isDigit && character != '_')
            return false;
    }

    return true;
}

String sanitiseSemanticSegment (StringRef segment)
{
    const String value (segment);
    String result;

    for (int index = 0; index < value.length(); ++index)
    {
        auto character = value[index];

        if (character >= 'A' && character <= 'Z')
            character = character - 'A' + 'a';

        const bool isLowercaseLetter = character >= 'a' && character <= 'z';
        const bool isDigit = character >= '0' && character <= '9';

        if (isLowercaseLetter || isDigit)
        {
            result += character;
        }
        else if (result.isNotEmpty() && ! result.endsWithChar ('_'))
        {
            result += '_';
        }
    }

    result = result.trimCharactersAtEnd ("_");
    return result.isNotEmpty() ? result : "unnamed";
}

String createProcessorControlSemanticId (int nodeId, StringRef controlName)
{
    return "oe.processor." + String (nodeId) + "."
           + sanitiseSemanticSegment (controlName);
}

String createStreamSemanticSegment (
    int sourceNodeId,
    StringRef identifier,
    StringRef displayNameFallback)
{
    const String identitySegment (
        String (identifier).isNotEmpty()
            ? identifier
            : displayNameFallback);

    return "source_" + String (sourceNodeId)
           + ".stream_"
           + sanitiseSemanticSegment (identitySegment);
}

String createStreamSelectorRowSemanticId (
    StringRef tableSemanticId,
    const StringArray& siblingSemanticSegments,
    int streamIndex)
{
    if (! isPositiveAndBelow (
            streamIndex,
            siblingSemanticSegments.size()))
    {
        return {};
    }

    auto automationId =
        String (tableSemanticId)
        + "."
        + siblingSemanticSegments[streamIndex];
    if (streamSemanticSegmentNeedsIndexSuffix (
            siblingSemanticSegments,
            streamIndex))
    {
        automationId +=
            ".index_"
            + String (streamIndex);
    }

    return automationId;
}

bool streamSemanticSegmentNeedsIndexSuffix (
    const StringArray& siblingSemanticSegments,
    int streamIndex)
{
    if (! isPositiveAndBelow (
            streamIndex,
            siblingSemanticSegments.size()))
    {
        return false;
    }

    const auto& semanticSegment =
        siblingSemanticSegments[streamIndex];
    int matchingSegments = 0;
    for (const auto& siblingSegment :
         siblingSemanticSegments)
    {
        if (siblingSegment == semanticSegment
            && ++matchingSegments > 1)
        {
            return true;
        }
    }

    return false;
}

std::unique_ptr<AccessibilityHandler>
createReadOnlyProgressAccessibilityHandler (
    Component& component,
    std::function<double()> getValue)
{
    return std::make_unique<AccessibilityHandler> (
        component,
        AccessibilityRole::progressBar,
        AccessibilityActions {},
        AccessibilityHandler::Interfaces {
            std::make_unique<ReadOnlyProgressValue> (std::move (getValue)) });
}

std::unique_ptr<AccessibilityHandler>
createReadOnlyTextAccessibilityHandler (
    Component& component,
    std::function<String()> getValue)
{
    return std::make_unique<AccessibilityHandler> (
        component,
        AccessibilityRole::staticText,
        AccessibilityActions {},
        AccessibilityHandler::Interfaces {
            std::make_unique<ReadOnlyTextValue> (std::move (getValue)) });
}

std::unique_ptr<AccessibilityHandler>
ReadOnlyValueTextButton::createAccessibilityHandler()
{
    if (accessibilityState == nullptr)
    {
        accessibilityState =
            std::make_shared<AccessibilityState>();
        accessibilityState->attach (this);
    }

    refreshAccessibilityState();
    return std::make_unique<
        AccessibilityHandlerImpl> (
        *this,
        accessibilityState);
}

ReadOnlyValueTextButton::
    ~ReadOnlyValueTextButton()
{
    if (accessibilityState != nullptr)
        accessibilityState->detach();
}

void ReadOnlyValueTextButton::
    refreshAccessibilityState()
{
    if (accessibilityState == nullptr)
        return;

    auto help = getHelpText();
    if (help.isEmpty())
        help = getDescription();

    accessibilityState->synchronise (
        getButtonText(),
        getTitle(),
        getDescription(),
        std::move (help),
        Component::isEnabled(),
        hasKeyboardFocus (false));
}

void ReadOnlyValueTextButton::
    enablementChanged()
{
    Button::enablementChanged();
    refreshAccessibilityState();
}

void ReadOnlyValueTextButton::focusGained (
    FocusChangeType cause)
{
    Button::focusGained (cause);
    refreshAccessibilityState();
}

void ReadOnlyValueTextButton::focusLost (
    FocusChangeType cause)
{
    Button::focusLost (cause);
    refreshAccessibilityState();
}

void setButtonTextWithAccessibilityValue (
    Button& button,
    StringRef value)
{
    const String newValue (value);

    if (button.getButtonText() == newValue)
    {
        if (auto* readOnlyButton =
                dynamic_cast<
                    ReadOnlyValueTextButton*> (
                    &button))
        {
            readOnlyButton
                ->refreshAccessibilityState();
        }
        return;
    }

    button.setButtonText (newValue);

    if (auto* readOnlyButton =
            dynamic_cast<
                ReadOnlyValueTextButton*> (
                &button))
    {
        readOnlyButton
            ->refreshAccessibilityState();
    }

    if (auto* handler =
            button.getAccessibilityHandler())
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::valueChanged);
}

void addSemanticCommandItem (
    PopupMenu& menu,
    ApplicationCommandManager* commandManager,
    CommandID commandId,
    StringRef semanticId)
{
    menu.addCommandItem (commandManager, commandId);

    if (commandManager == nullptr || ! isValidSemanticId (semanticId))
        return;

    PopupMenu::MenuItemIterator iterator (menu);
    PopupMenu::Item* addedItem = nullptr;

    while (iterator.next())
        addedItem = &iterator.getItem();

    if (addedItem == nullptr || addedItem->itemID != static_cast<int> (commandId))
        return;

    addedItem->accessibilityId = String (semanticId);

    if (auto* commandInfo = commandManager->getCommandForID (commandId))
    {
        addedItem->accessibilityDescription = commandInfo->description;
        addedItem->accessibilityHelp = commandInfo->description;
    }
}

void addSemanticSubMenu (
    PopupMenu& menu,
    StringRef name,
    PopupMenu subMenu,
    StringRef semanticId,
    StringRef description)
{
    menu.addSubMenu (String (name), std::move (subMenu));

    if (! isValidSemanticId (semanticId))
        return;

    PopupMenu::MenuItemIterator iterator (menu);
    PopupMenu::Item* addedItem = nullptr;

    while (iterator.next())
        addedItem = &iterator.getItem();

    if (addedItem == nullptr || addedItem->subMenu == nullptr)
        return;

    addedItem->accessibilityId = String (semanticId);
    addedItem->accessibilityDescription = String (description);
    addedItem->accessibilityHelp = String (description);
}

void applySemanticMetadata (Component& component,
                            StringRef id,
                            StringRef title,
                            StringRef description,
                            StringRef help)
{
    if (! isValidSemanticId (id))
        return;

    component.setComponentID (String (id));
    component.setTitle (String (title));
    component.setDescription (String (description));
    component.setHelpText (String (help));
    component.setAccessible (true);
    component.invalidateAccessibilityHandler();

    if (auto* readOnlyButton =
            dynamic_cast<
                ReadOnlyValueTextButton*> (
                &component))
    {
        readOnlyButton
            ->refreshAccessibilityState();
    }
}
