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

#include "LfpDisplayEditor.h"
#include <atomic>
#include <mutex>

#define MS_FROM_START Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - start) * 1000

using namespace LfpViewer;

struct LfpViewer::
    LfpEditorButtonAccessibilityState
{
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
        enabled.store (false);
        checked.store (false);
        focused.store (false);
    }

    void synchronise (
        String titleToUse,
        String descriptionToUse,
        String helpToUse,
        bool isChecked,
        bool isEnabled,
        bool isFocused)
    {
        {
            const std::lock_guard<std::mutex>
                lock (textMutex);
            title = std::move (titleToUse);
            description =
                std::move (descriptionToUse);
            help = std::move (helpToUse);
        }

        checked.store (isChecked);
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

    void performPressOnMessageThread()
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        auto* currentButton =
            button.getComponent();
        if (currentButton == nullptr
            || ! enabled.load()
            || ! currentButton
                    ->isEnabled())
        {
            return;
        }

        currentButton->triggerClick();
    }

private:
    mutable std::mutex textMutex;
    String title;
    String description;
    String help;
    std::atomic<bool> checked { false };
    std::atomic<bool> enabled { true };
    std::atomic<bool> focused { false };
    Component::SafePointer<Button> button;
};

namespace
{
class LfpEditorButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    LfpEditorButtonAccessibilityHandler (
        Button& button,
        AccessibilityRole role,
        std::shared_ptr<
            LfpEditorButtonAccessibilityState>
            stateToUse,
        bool reportsCheckedState)
        : AccessibilityHandler (
              button,
              role,
              createActions (
                  stateToUse)),
          state (std::move (stateToUse)),
          reportsChecked (
              reportsCheckedState)
    {
    }

    AccessibleState
    getCurrentState() const override
    {
        auto current =
            AccessibleState()
                .withFocusable();
        if (reportsChecked
            && state->isChecked())
        {
            current =
                current.withChecked();
        }
        if (state->isFocused())
            current =
                current.withFocused();
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
    static AccessibilityActions
    createActions (
        const std::shared_ptr<
            LfpEditorButtonAccessibilityState>&
            state)
    {
        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::press,
                [state]
                {
                    if (! state
                              ->isEnabled())
                    {
                        return;
                    }

                    auto* messageManager =
                        MessageManager::
                            getInstanceWithoutCreating();
                    if (messageManager == nullptr)
                        return;

                    MessageManager::callAsync (
                        [state]
                        {
                            state
                                ->performPressOnMessageThread();
                        });
                });
    }

    std::shared_ptr<
        LfpEditorButtonAccessibilityState>
        state;
    const bool reportsChecked;
};

void refreshLfpEditorButtonState (
    Button& button,
    const std::shared_ptr<
        LfpEditorButtonAccessibilityState>&
        state,
    bool actionIsAvailable)
{
    jassert (
        MessageManager::getInstance()
            ->isThisTheMessageThread());

    auto title = button.getTitle();
    if (title.isEmpty())
        title = button.getName();
    auto help = button.getHelpText();
    if (help.isEmpty())
        help = button.getDescription();

    state->synchronise (
        std::move (title),
        button.getDescription(),
        std::move (help),
        button.getToggleState(),
        actionIsAvailable
            && button
                   .Component::isEnabled(),
        button.hasKeyboardFocus (false));
}

void applyLfpEditorMetadata (
    Component& component,
    StringRef id,
    StringRef title,
    StringRef description)
{
    component.setComponentID (
        String (id));
    component.setTitle (
        String (title));
    component.setDescription (
        String (description));
    component.setHelpText (
        String (description));
    component.setAccessible (true);
    component
        .invalidateAccessibilityHandler();
}
} // namespace

LayoutButton::LayoutButton (const String& buttonName)
    : Button (buttonName),
      accessibilityState (
          std::make_shared<
              LfpEditorButtonAccessibilityState>())
{
    accessibilityState->attach (this);
    setClickingTogglesState (true);
    refreshAccessibilityState();
}

LayoutButton::~LayoutButton()
{
    accessibilityState->detach();
}

std::unique_ptr<AccessibilityHandler>
LayoutButton::createAccessibilityHandler()
{
    refreshAccessibilityState();
    return std::make_unique<
        LfpEditorButtonAccessibilityHandler> (
        *this,
        AccessibilityRole::radioButton,
        accessibilityState,
        true);
}

void LayoutButton::refreshAccessibilityState()
{
    refreshLfpEditorButtonState (
        *this,
        accessibilityState,
        true);
}

void LayoutButton::buttonStateChanged()
{
    Button::buttonStateChanged();
    refreshAccessibilityState();
}

void LayoutButton::enablementChanged()
{
    Button::enablementChanged();
    refreshAccessibilityState();
}

void LayoutButton::focusGained (
    FocusChangeType cause)
{
    Button::focusGained (cause);
    refreshAccessibilityState();
}

void LayoutButton::focusLost (
    FocusChangeType cause)
{
    Button::focusLost (cause);
    refreshAccessibilityState();
}

LfpSyncButton::LfpSyncButton (
    String label)
    : UtilityButton (
          std::move (label)),
      accessibilityState (
          std::make_shared<
              LfpEditorButtonAccessibilityState>())
{
    accessibilityState->attach (this);
    refreshAccessibilityState();
}

LfpSyncButton::~LfpSyncButton()
{
    accessibilityState->detach();
}

std::unique_ptr<AccessibilityHandler>
LfpSyncButton::createAccessibilityHandler()
{
    refreshAccessibilityState();
    return std::make_unique<
        LfpEditorButtonAccessibilityHandler> (
        *this,
        AccessibilityRole::button,
        accessibilityState,
        false);
}

void LfpSyncButton::
    setAccessibilityActionAvailable (
        bool isAvailable)
{
    accessibilityActionAvailable =
        isAvailable;
    refreshAccessibilityState();
}

void LfpSyncButton::refreshAccessibilityState()
{
    refreshLfpEditorButtonState (
        *this,
        accessibilityState,
        accessibilityActionAvailable);
}

void LfpSyncButton::buttonStateChanged()
{
    UtilityButton::buttonStateChanged();
    refreshAccessibilityState();
}

void LfpSyncButton::enablementChanged()
{
    UtilityButton::enablementChanged();
    refreshAccessibilityState();
}

void LfpSyncButton::focusGained (
    FocusChangeType cause)
{
    UtilityButton::focusGained (cause);
    refreshAccessibilityState();
}

void LfpSyncButton::focusLost (
    FocusChangeType cause)
{
    UtilityButton::focusLost (cause);
    refreshAccessibilityState();
}

void LayoutButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    if (getToggleState())
        g.setColour (findColour (ThemeColours::highlightedFill));
    else
        g.setColour (findColour (ThemeColours::widgetBackground));

    g.fillRoundedRectangle (0, 0, getWidth(), getHeight(), 3);

    if (getToggleState())
        g.setColour (Colours::black.withAlpha (isMouseOver ? 0.6f : 1.0f));
    else
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (isMouseOver ? 0.6f : 1.0f));

    juce::Path path;

    if (getName().equalsIgnoreCase ("single"))
    {
        g.drawRoundedRectangle (3, 3, getWidth() - 6, getHeight() - 6, 2, 1);
    }
    else if (getName().equalsIgnoreCase ("two-vertical"))
    {
        g.drawRoundedRectangle (3, 3, getWidth() - 6, getHeight() - 6, 2, 1);
        g.fillRect ((float) (getWidth() / 2) - 0.5f, 3.0f, 1.0f, (float) (getHeight() - 6));
    }
    else if (getName().equalsIgnoreCase ("three-vertical"))
    {
        g.drawRoundedRectangle (3, 3, getWidth() - 6, getHeight() - 6, 2, 1);
        g.fillRect ((float) (getWidth() / 3) + 1.0f, 3.0f, 1.0f, (float) (getHeight() - 6));
        g.fillRect ((float) (2 * getWidth() / 3) - 1.0f, 3.0f, 1.0f, (float) (getHeight() - 6));
    }
    else if (getName().equalsIgnoreCase ("two-horizontal"))
    {
        g.drawRoundedRectangle (3, 3, getWidth() - 6, getHeight() - 6, 2, 1);
        g.fillRect (3.0f, (float) (getHeight() / 2) - 0.5f, (float) (getWidth() - 6), 1.0f);
    }
    else
    {
        g.drawRoundedRectangle (3, 3, getWidth() - 6, getHeight() - 6, 2, 1);
        g.fillRect (3.0f, (float) (getHeight() / 3) + 1.0f, (float) (getWidth() - 6), 1.0f);
        g.fillRect (3.0f, (float) (2 * getHeight() / 3) - 1.0f, (float) (getWidth() - 6), 1.0f);
    }
}

LfpDisplayEditor::LfpDisplayEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "LFP", 195),
      hasNoInputs (true),
      signalChainIsLoading (true)
{
    lfpProcessor = (LfpDisplayNode*) parentNode;
    const auto semanticPrefix =
        "oe.processor."
        + String (
            parentNode->getNodeId())
        + ".lfp";

    defaultSubprocessor = 0;

    layoutLabel = std::make_unique<Label> ("layout", "Layout:");
    //addAndMakeVisible(layoutLabel.get());

    singleDisplay = std::make_unique<LayoutButton> ("single");
    applyLfpEditorMetadata (
        *singleDisplay,
        semanticPrefix
            + ".layout.single",
        "Single display",
        "Show one LFP display pane.");
    singleDisplay->setToggleState (true, dontSendNotification);
    singleDisplay->setRadioGroupId (201, dontSendNotification);
    singleDisplay->addListener (this);
    addAndMakeVisible (singleDisplay.get());
    selectedLayout = SplitLayouts::SINGLE;

    twoVertDisplay = std::make_unique<LayoutButton> ("two-vertical");
    applyLfpEditorMetadata (
        *twoVertDisplay,
        semanticPrefix
            + ".layout.two_vertical",
        "Two vertical displays",
        "Show two LFP display panes side by side.");
    twoVertDisplay->setToggleState (false, dontSendNotification);
    twoVertDisplay->setRadioGroupId (201, dontSendNotification);
    twoVertDisplay->addListener (this);
    addAndMakeVisible (twoVertDisplay.get());

    threeVertDisplay = std::make_unique<LayoutButton> ("three-vertical");
    applyLfpEditorMetadata (
        *threeVertDisplay,
        semanticPrefix
            + ".layout.three_vertical",
        "Three vertical displays",
        "Show three LFP display panes side by side.");
    threeVertDisplay->setToggleState (false, dontSendNotification);
    threeVertDisplay->setRadioGroupId (201, dontSendNotification);
    threeVertDisplay->addListener (this);
    addAndMakeVisible (threeVertDisplay.get());

    twoHoriDisplay = std::make_unique<LayoutButton> ("two-horizontal");
    applyLfpEditorMetadata (
        *twoHoriDisplay,
        semanticPrefix
            + ".layout.two_horizontal",
        "Two horizontal displays",
        "Show two stacked LFP display panes.");
    twoHoriDisplay->setToggleState (false, dontSendNotification);
    twoHoriDisplay->setRadioGroupId (201, dontSendNotification);
    twoHoriDisplay->addListener (this);
    addAndMakeVisible (twoHoriDisplay.get());

    threeHoriDisplay = std::make_unique<LayoutButton> ("three-horizontal");
    applyLfpEditorMetadata (
        *threeHoriDisplay,
        semanticPrefix
            + ".layout.three_horizontal",
        "Three horizontal displays",
        "Show three stacked LFP display panes.");
    threeHoriDisplay->setToggleState (false, dontSendNotification);
    threeHoriDisplay->setRadioGroupId (201, dontSendNotification);
    threeHoriDisplay->addListener (this);
    addAndMakeVisible (threeHoriDisplay.get());

    syncButton = std::make_unique<LfpSyncButton> ("SYNC DISPLAYS");
    applyLfpEditorMetadata (
        *syncButton,
        semanticPrefix
            + ".sync_displays",
        "Sync displays",
        "Realign every LFP display pane with the latest data buffer position.");
    syncButton->addListener (this);
    addAndMakeVisible (syncButton.get());
}

Visualizer* LfpDisplayEditor::createNewCanvas()
{
    auto* newCanvas =
        new LfpDisplayCanvas (
            lfpProcessor,
            selectedLayout,
            signalChainIsLoading);
    syncButton
        ->setAccessibilityActionAvailable (
            newCanvas != nullptr);
    return newCanvas;
}

void LfpDisplayEditor::initialize (bool signalChainIsLoading_)
{
    signalChainIsLoading = signalChainIsLoading_;
}

void LfpDisplayEditor::buttonClicked (Button* button)
{
    if (button == singleDisplay.get())
        selectedLayout = SplitLayouts::SINGLE;
    else if (button == twoVertDisplay.get())
        selectedLayout = SplitLayouts::TWO_VERT;
    else if (button == threeVertDisplay.get())
        selectedLayout = SplitLayouts::THREE_VERT;
    else if (button == twoHoriDisplay.get())
        selectedLayout = SplitLayouts::TWO_HORZ;
    else if (button == threeHoriDisplay.get())
        selectedLayout = SplitLayouts::THREE_HORZ;
    else if (button == syncButton.get())
    {
        if (canvas != nullptr)
        {
            LfpDisplayCanvas* c = (LfpDisplayCanvas*) canvas.get();
            c->syncDisplays();
        }
    }

    if (button->getRadioGroupId() == 201 && canvas != nullptr)
        static_cast<LfpDisplayCanvas*> (canvas.get())->setLayout (selectedLayout);
}

void LfpDisplayEditor::resized()
{
    VisualizerEditor::resized();

    int buttonSize = 25;

    //layoutLabel->setBounds(5, 40, 50, 20);
    singleDisplay->setBounds (22, 43, buttonSize, buttonSize);
    twoVertDisplay->setBounds (52, 43, buttonSize, buttonSize);
    threeVertDisplay->setBounds (82, 43, buttonSize, buttonSize);
    twoHoriDisplay->setBounds (112, 43, buttonSize, buttonSize);
    threeHoriDisplay->setBounds (142, 43, buttonSize, buttonSize);

    syncButton->setBounds (40, 84, 110, 30);
}

void LfpDisplayEditor::removeBufferForDisplay (
    int splitID,
    DisplayBuffer* removedBuffer)
{
    if (canvas != nullptr)
    {
        LfpDisplayCanvas* cv = (LfpDisplayCanvas*) canvas.get();

        cv->removeBufferForDisplay (
            splitID,
            removedBuffer);
    }
}

void LfpDisplayEditor::saveVisualizerEditorParameters (XmlElement* xml)
{
    xml->setAttribute ("Type", "LfpDisplayEditor");

    XmlElement* values = xml->createNewChildElement ("VALUES");
    values->setAttribute ("SelectedLayout", static_cast<int> (selectedLayout));
}

void LfpDisplayEditor::loadVisualizerEditorParameters (XmlElement* xml)
{
    for (auto* xmlNode : xml->getChildIterator())
    {
        if (xmlNode->hasTagName ("VALUES"))
        {
            int64 start = Time::getHighResolutionTicks();

            selectedLayout = static_cast<SplitLayouts> (xmlNode->getIntAttribute ("SelectedLayout"));

            if (canvas != nullptr)
                static_cast<LfpDisplayCanvas*> (canvas.get())->setLayout (selectedLayout);

            if (selectedLayout == SplitLayouts::SINGLE)
                singleDisplay->setToggleState (true, dontSendNotification);
            else if (selectedLayout == SplitLayouts::TWO_VERT)
                twoVertDisplay->setToggleState (true, dontSendNotification);
            else if (selectedLayout == SplitLayouts::THREE_VERT)
                threeVertDisplay->setToggleState (true, dontSendNotification);
            else if (selectedLayout == SplitLayouts::TWO_HORZ)
                twoHoriDisplay->setToggleState (true, dontSendNotification);
            else
                threeHoriDisplay->setToggleState (true, dontSendNotification);

            LOGDD ("    Loaded layout in ", MS_FROM_START, " milliseconds");
        }
    }
}
