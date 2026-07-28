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

#include "VisualizerEditor.h"
#include "../../AccessClass.h"
#include "../../UI/DataViewport.h"
#include "../../UI/SemanticComponent.h"
#include "../../UI/UIComponent.h"

#include "../../Utils/Utils.h"
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace
{
struct SelectorButtonAccessibilityState
{
    void attach (SelectorButton* buttonToUse)
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

    SelectorButton*
    getButtonOnMessageThread() const
    {
        jassert (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        return button.getComponent();
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

private:
    mutable std::mutex textMutex;
    String title;
    String description;
    String help;
    std::atomic<bool> checked { false };
    std::atomic<bool> enabled { true };
    std::atomic<bool> focused { false };
    Component::SafePointer<
        SelectorButton>
        button;
};

struct SelectorButtonAccessibilityRegistry
{
    std::mutex mutex;
    std::unordered_map<
        SelectorButton*,
        std::shared_ptr<
            SelectorButtonAccessibilityState>>
        states;
};

SelectorButtonAccessibilityRegistry&
getSelectorButtonAccessibilityRegistry()
{
    static auto* registry =
        new SelectorButtonAccessibilityRegistry();
    return *registry;
}

std::shared_ptr<
    SelectorButtonAccessibilityState>
registerSelectorButton (
    SelectorButton* button)
{
    auto state =
        std::make_shared<
            SelectorButtonAccessibilityState>();
    state->attach (button);

    auto& registry =
        getSelectorButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    for (auto iterator =
             registry.states.begin();
         iterator
         != registry.states.end();)
    {
        if (iterator->second
                ->getButtonOnMessageThread()
            == nullptr)
        {
            iterator =
                registry.states.erase (
                    iterator);
        }
        else
        {
            ++iterator;
        }
    }
    registry.states[button] = state;
    return state;
}

std::shared_ptr<
    SelectorButtonAccessibilityState>
getSelectorButtonState (
    SelectorButton* button)
{
    auto& registry =
        getSelectorButtonAccessibilityRegistry();
    const std::lock_guard<std::mutex>
        lock (registry.mutex);
    const auto found =
        registry.states.find (button);
    return found != registry.states.end()
               ? found->second
               : nullptr;
}

void unregisterSelectorButton (
    SelectorButton* button)
{
    std::shared_ptr<
        SelectorButtonAccessibilityState>
        state;
    {
        auto& registry =
            getSelectorButtonAccessibilityRegistry();
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

class SelectorButtonAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit SelectorButtonAccessibilityValue (
        std::shared_ptr<
            SelectorButtonAccessibilityState>
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
        return state->isChecked()
                   ? "On"
                   : "Off";
    }

private:
    std::shared_ptr<
        SelectorButtonAccessibilityState>
        state;
};

class SelectorButtonAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    SelectorButtonAccessibilityHandler (
        SelectorButton& button,
        std::shared_ptr<
            SelectorButtonAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              button,
              AccessibilityRole::toggleButton,
              createActions (stateToUse),
              AccessibilityHandler::Interfaces {
                  std::make_unique<
                      SelectorButtonAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

    AccessibleState
    getCurrentState() const override
    {
        auto current =
            AccessibleState()
                .withFocusable()
                .withCheckable();
        if (state->isChecked())
            current = current.withChecked();
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
    static AccessibilityActions
    createActions (
        const std::shared_ptr<
            SelectorButtonAccessibilityState>&
            state)
    {
        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::toggle,
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
                                        ->isEnabled()
                                || ! button
                                        ->isToggleable()
                                || button
                                       ->getRadioGroupId()
                                   != 0)
                            {
                                return;
                            }

                            button->setToggleState (
                                ! button
                                       ->getToggleState(),
                                sendNotification);
                        });
                });
    }

    std::shared_ptr<
        SelectorButtonAccessibilityState>
        state;
};
} // namespace

SelectorButton::SelectorButton (const String& buttonName)
    : Button (buttonName)
{
    registerSelectorButton (this);
    setClickingTogglesState (true);

    if (getName().contains ("Window"))
        setTooltip ("Open visualizer in its own window");
    else
        setTooltip ("Open visualizer in a tab");

    refreshAccessibilityState();
}

SelectorButton::~SelectorButton()
{
    unregisterSelectorButton (this);
}

std::unique_ptr<AccessibilityHandler>
SelectorButton::createAccessibilityHandler()
{
    auto state =
        getSelectorButtonState (this);
    if (state == nullptr)
        state =
            registerSelectorButton (this);

    refreshAccessibilityState();
    return std::make_unique<
        SelectorButtonAccessibilityHandler> (
        *this,
        std::move (state));
}

void SelectorButton::
    refreshAccessibilityState()
{
    jassert (
        MessageManager::getInstance()
            ->isThisTheMessageThread());
    auto state =
        getSelectorButtonState (this);
    if (state == nullptr)
        return;

    auto title = getTitle();
    if (title.isEmpty())
        title = getName();
    auto help = getTooltip();
    if (help.isEmpty())
        help = getHelpText();
    if (help.isEmpty())
        help = getDescription();

    state->synchronise (
        std::move (title),
        getDescription(),
        std::move (help),
        getToggleState(),
        Component::isEnabled(),
        hasKeyboardFocus (false));
}

void SelectorButton::buttonStateChanged()
{
    Button::buttonStateChanged();
    refreshAccessibilityState();
}

void SelectorButton::enablementChanged()
{
    Button::enablementChanged();
    refreshAccessibilityState();
}

void SelectorButton::focusGained (
    FocusChangeType cause)
{
    Button::focusGained (cause);
    refreshAccessibilityState();
}

void SelectorButton::focusLost (
    FocusChangeType cause)
{
    Button::focusLost (cause);
    refreshAccessibilityState();
}

void SelectorButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    if (getToggleState() == true)
        g.setColour (Colours::white);
    else
        g.setColour (Colours::darkgrey);

    if (isMouseOver)
        g.setColour (Colours::yellow);

    if (getName().contains ("Window"))
    {
        // window icon
        g.drawRect (0, 0, getWidth(), getHeight(), 1.0);
        g.fillRect (0, 0, getWidth(), 3.0);
    }
    else
    {
        // tab icon
        g.drawVerticalLine (5, 0, getHeight());
        g.fillRoundedRectangle (5, 2, 4, getHeight() - 4, 4.0f);
        g.fillRect (5, 2, 4, getHeight() - 4);
    }
}

bool SelectorButton::isOpenWindowButton() const
{
    return getName().contains ("Window");
}

bool SelectorButton::isOpenTabButton() const
{
    return ! isOpenWindowButton();
}

VisualizerEditor::VisualizerEditor (GenericProcessor* parentNode, String tabText, int desiredWidth_)
    : GenericEditor (parentNode), dataWindow (nullptr), canvas (nullptr), tabText (tabText), dataWindowButtonListener (this)
{
    desiredWidth = desiredWidth_;

    initializeSelectors();
}

void VisualizerEditor::initializeSelectors()
{
    const auto semanticPrefix =
        createProcessorControlSemanticId (
            getProcessor()->getNodeId(),
            "visualizer");
    const auto processorName =
        getName();

    windowSelector = std::make_unique<SelectorButton> (getNameAndId() + " Visualizer Window Button");
    const auto windowDescription =
        "Show the " + processorName
        + " visualizer in a separate window.";
    applySemanticMetadata (
        *windowSelector,
        semanticPrefix + ".open_in_window",
        "Open " + processorName
            + " visualizer in window",
        windowDescription,
        windowDescription);
    windowSelector
        ->refreshAccessibilityState();
    windowSelector->setBounds (desiredWidth - 40, 7, 14, 10);
    windowSelector->setToggleState (false, dontSendNotification);
    windowSelector->addListener (&dataWindowButtonListener);
    addAndMakeVisible (windowSelector.get());

    tabSelector = std::make_unique<SelectorButton> (getNameAndId() + " Visualizer Tab Button");
    const auto tabDescription =
        "Show the " + processorName
        + " visualizer in a view tab.";
    applySemanticMetadata (
        *tabSelector,
        semanticPrefix + ".open_in_tab",
        "Open " + processorName
            + " visualizer in tab",
        tabDescription,
        tabDescription);
    tabSelector
        ->refreshAccessibilityState();
    tabSelector->setToggleState (false, dontSendNotification);
    tabSelector->setBounds (desiredWidth - 20, 7, 15, 10);
    tabSelector->addListener (&dataWindowButtonListener);
    addAndMakeVisible (tabSelector.get());
}

VisualizerEditor::~VisualizerEditor()
{
    if (isOpenInTab)
    {
        AccessClass::getDataViewport()->removeTab (nodeId, false);
    }

    if (dataWindow != nullptr)
        dataWindow->removeListener (this);
}

void VisualizerEditor::resized()
{
    GenericEditor::resized();

    windowSelector->setBounds (getTotalWidth() - 40, 7, 14, 10);
    tabSelector->setBounds (getTotalWidth() - 20, 7, 15, 10);
}

void VisualizerEditor::enable()
{
    if (canvas != nullptr)
        canvas->beginAnimation();
}

void VisualizerEditor::disable()
{
    if (canvas != nullptr)
        canvas->endAnimation();
}

void VisualizerEditor::updateVisualizer()
{
    if (canvas != nullptr)
        canvas->update();
}

void VisualizerEditor::editorWasClicked()
{
    if (isOpenInTab)
    {
        LOGD ("Setting tab index to ", nodeId);
        AccessClass::getDataViewport()->selectTab (nodeId);
    }

    if (dataWindow && windowSelector->getToggleState())
        dataWindow->toFront (true);
}

void VisualizerEditor::ButtonResponder::buttonClicked (Button* button)
{
    // Handle the buttons to open the canvas in a tab or window
    editor->checkForCanvas();

    if (button == editor->windowSelector.get())
    {
        if (editor->tabSelector->getToggleState())
        {
            editor->removeTab();
        }

        if (editor->dataWindow == nullptr) // have we created a window already?
        {
            editor->makeNewWindow();

            editor->dataWindow->setContentNonOwned (editor->canvas.get(), true);
            editor->dataWindow->setVisible (true);
            editor->dataWindow->toFront (true);
            editor->dataWindow->addListener (editor);
        }
        else
        {
            if (editor->windowSelector->getToggleState())
            {
                editor->dataWindow->setContentNonOwned (editor->canvas.get(), true);
                editor->dataWindow->setVisible (true);
                editor->dataWindow->toFront (true);
                // editor->canvas->setBounds (0, 0, editor->canvas->getParentWidth(), editor->canvas->getParentHeight());
            }
            else
            {
                editor->dataWindow->setContentNonOwned (0, false);
                editor->dataWindow->setVisible (false);
            }
        }

#if JUCE_WINDOWS
        // Set the rendering engine for the window
        if (editor->dataWindow->isVisible())
        {
            auto editorPeer = editor->getPeer();
            if (auto peer = editor->dataWindow->getPeer())
            {
                if (editorPeer)
                    peer->setCurrentRenderingEngine (editorPeer->getCurrentRenderingEngine());
            }
        }
#endif
    }
    else if (button == editor->tabSelector.get())
    {
        LOGD ("VisualizerEditor tab button clicked.");

        if (! editor->isOpenInTab)
        {
            if (editor->windowSelector->getToggleState())
            {
                LOGD ("VisualizerEditor closing window.");
                editor->dataWindow->setContentNonOwned (0, false);
                editor->windowSelector->setToggleState (false, dontSendNotification);
                editor->dataWindow->setVisible (false);
            }

            LOGD ("VisualizerEditor adding tab.");
            editor->addTab();
        }
        else
        {
            editor->removeTab();
        }
    }
}

void VisualizerEditor::checkForCanvas()
{
    if (canvas == nullptr)
    {
        canvas.reset (createNewCanvas());

        // Prevents canvas-less interface from crashing GUI on button clicks...
        if (canvas == nullptr)
        {
            LOGD ("Unable to create ", getName(), " canvas.");
            return;
        }

        canvas->update();

        if (acquisitionIsActive)
            canvas->beginAnimation();
    }
}

void VisualizerEditor::saveCustomParametersToXml (XmlElement* xml)
{
    xml->setAttribute ("Type", "Visualizer");
    xml->setAttribute ("tabText", tabText);

    XmlElement* tabButtonState = xml->createNewChildElement (EDITOR_TAG_TAB);
    tabButtonState->setAttribute ("Active", tabSelector->getToggleState());

    XmlElement* windowButtonState = xml->createNewChildElement (EDITOR_TAG_WINDOW);
    windowButtonState->setAttribute ("Active", windowSelector->getToggleState());

    if (dataWindow != nullptr)
    {
        windowButtonState->setAttribute ("x", dataWindow->getX());
        windowButtonState->setAttribute ("y", dataWindow->getY());
        windowButtonState->setAttribute ("width", dataWindow->getWidth());
        windowButtonState->setAttribute ("height", dataWindow->getHeight());
    }

    saveVisualizerEditorParameters (xml);

    if (canvas != nullptr)
    {
        canvas->saveToXml (xml);
    }
    else
    {
        // if canvas was never created, we don't need to save custom parameters
    }
}

void VisualizerEditor::loadCustomParametersFromXml (XmlElement* xml)
{
    bool canvasHidden = false;

    tabText = xml->getStringAttribute ("tabText", tabText);

    for (auto* xmlNode : xml->getChildIterator())
    {
        if (xmlNode->hasTagName (EDITOR_TAG_TAB))
        {
            bool tabState = xmlNode->getBoolAttribute ("Active");

            if (tabState)
            {
                /* NB: DataViewport::loadStateFromXml() will call addTab() for us 
                   to maintain tab configuration
                */
                checkForCanvas();

                // If tab is already open, remove and re-add the tab to update its text
                if (isOpenInTab)
                {
                    removeTab();
                    addTab();
                }

                break;
            }
        }
        else if (xmlNode->hasTagName (EDITOR_TAG_WINDOW))
        {
            bool windowState = xmlNode->getBoolAttribute ("Active");

            if (windowState)
            {
                windowSelector->setToggleState (true, sendNotification);
                if (dataWindow != nullptr)
                {
                    dataWindow->setBounds (xmlNode->getIntAttribute ("x"),
                                           xmlNode->getIntAttribute ("y"),
                                           xmlNode->getIntAttribute ("width"),
                                           xmlNode->getIntAttribute ("height"));
                }
                break;
            }
        }
        else
        {
            canvasHidden = true;
        }
    }

    loadVisualizerEditorParameters (xml);

    if (canvasHidden)
    {
        //Canvas is created on button callback, so open/close tab to simulate a hidden canvas
        tabSelector->setToggleState (true, sendNotification);
        if (canvas != nullptr)
            canvas->loadFromXml (xml);
        tabSelector->setToggleState (false, sendNotification);
    }
    else if (canvas != nullptr)
    {
        canvas->loadFromXml (xml);
    }
}

void VisualizerEditor::makeNewWindow()
{
    dataWindow = std::make_unique<DataWindow> (windowSelector.get(), tabText);
    dataWindow->setLookAndFeel (&getLookAndFeel());
    dataWindow->setBackgroundColour (findColour (ThemeColours::windowBackground));
}

/* static method */
void VisualizerEditor::addWindowListener (DataWindow* dataWindowToUse, DataWindow::Listener* newListener)
{
    if (dataWindowToUse != nullptr && newListener != nullptr)
        dataWindowToUse->addListener (newListener);
}

/* static method */
void VisualizerEditor::removeWindowListener (DataWindow* dataWindowToUse, DataWindow::Listener* oldListener)
{
    if (dataWindowToUse != nullptr && oldListener != nullptr)
        dataWindowToUse->removeListener (oldListener);
}

Component* VisualizerEditor::getActiveTabContentComponent() const
{
    return AccessClass::getDataViewport()->getActiveTabContentComponent();
}

void VisualizerEditor::removeTab()
{
    //std::cout << "Removing tab for " << nodeId << std::endl;
    AccessClass::getDataViewport()->removeTab (nodeId);
}

void VisualizerEditor::tabWasClosed()
{
    tabSelector->setToggleState (false, dontSendNotification);

    isOpenInTab = false;
}

void VisualizerEditor::addTab()
{
    if (isOpenInTab)
        return;

    LOGD ("CREATING CANVAS");
    checkForCanvas();

    LOGD ("ADDING TAB");
    AccessClass::getDataViewport()->addTab (tabText, canvas.get(), nodeId);

    if (! tabSelector->getToggleState())
        tabSelector->setToggleState (true, dontSendNotification);

    isOpenInTab = true;
}
