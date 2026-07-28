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

#include "MergerEditor.h"
#include "../../AccessClass.h"
#include "../../UI/EditorViewport.h"
#include "../../UI/GraphViewer.h"
#include "../../UI/SemanticComponent.h"
#include "../MessageCenter/MessageCenterEditor.h"
#include "../ProcessorGraph/ProcessorGraph.h"
#include "Merger.h"

#include "../Editors/StreamSelector.h"
#include "../Settings/DataStream.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

struct MergerInputMenuState
{
    struct Request
    {
        bool shouldBeOpen;
        uint64 generation;
    };

    Request toggle()
    {
        const std::lock_guard<std::mutex>
            lock (requestMutex);
        const bool nextState =
            ! shouldBeOpen.load();
        const auto nextGeneration =
            generation.load() + 1;
        shouldBeOpen.store (nextState);
        generation.store (nextGeneration);
        return {
            nextState,
            nextGeneration
        };
    }

    bool finish (
        uint64 expectedGeneration)
    {
        const std::lock_guard<std::mutex>
            lock (requestMutex);
        if (generation.load()
            != expectedGeneration)
        {
            return false;
        }

        generation.store (
            expectedGeneration + 1);
        shouldBeOpen.store (false);
        return true;
    }

    std::atomic<bool> shouldBeOpen { false };
    std::atomic<uint64> generation { 0 };
    std::atomic<uint64> activeMenuGeneration { 0 };

private:
    std::mutex requestMutex;
};

namespace
{
struct MergerInputMenuChoice
{
    int resultId;
    int inputIndex;
    int sourceNodeId;
    GenericProcessor* expectedInputSource;
};

struct MergerInputMenuModel
{
    PopupMenu menu;
    std::vector<
        MergerInputMenuChoice>
        choices;
};

void addSemanticMergerMenuItem (
    PopupMenu& menu,
    int resultId,
    StringRef text,
    bool isEnabled,
    StringRef semanticId,
    StringRef description)
{
    PopupMenu::Item item {
        String (text)
    };
    item.itemID =
        resultId;
    item.isEnabled =
        isEnabled;
    item.accessibilityId =
        String (semanticId);
    item.accessibilityDescription =
        String (description);
    item.accessibilityHelp =
        String (description);
    menu.addItem (
        std::move (item));
}

MergerInputMenuModel
    createMergerInputMenuModel (
        Merger& merger,
        const Array<
            GenericProcessor*>&
            selectableProcessors)
{
    MergerInputMenuModel model;
    int nextResultId =
        0;
    const auto editorId =
        "oe.processor."
        + String (
            merger.getNodeId())
        + ".route.input.";

    const auto addInput =
        [&] (
            int inputIndex,
            GenericProcessor* currentSource)
    {
        const auto inputName =
            inputIndex == 0
                ? String ("A")
                : String ("B");
        const auto inputId =
            editorId
            + inputName
                  .toLowerCase();

        if (currentSource
                != nullptr
            && ! currentSource
                     ->isEmpty())
        {
            addSemanticMergerMenuItem (
                model.menu,
                ++nextResultId,
                "Input "
                    + inputName
                    + ":",
                false,
                inputId
                    + ".heading",
                "Current source for merger input "
                    + inputName
                    + ".");
            addSemanticMergerMenuItem (
                model.menu,
                ++nextResultId,
                currentSource
                        ->getName()
                    + " ("
                    + String (
                        currentSource
                            ->getNodeId())
                    + ")",
                false,
                inputId
                    + ".current."
                    + String (
                        currentSource
                            ->getNodeId()),
                currentSource
                        ->getName()
                    + " node "
                    + String (
                        currentSource
                            ->getNodeId())
                    + " is connected to merger input "
                    + inputName
                    + ".");
            return;
        }

        addSemanticMergerMenuItem (
            model.menu,
            ++nextResultId,
            "Choose input "
                + inputName
                + ":",
            false,
            inputId
                + ".heading",
            "Choose a processor for merger input "
                + inputName
                + ".");

        if (selectableProcessors
                .isEmpty())
        {
            addSemanticMergerMenuItem (
                model.menu,
                ++nextResultId,
                "No input sources available",
                false,
                inputId
                    + ".none",
                "No processor is available to connect to merger input "
                    + inputName
                    + ".");
            return;
        }

        for (auto* source :
             selectableProcessors)
        {
            const auto resultId =
                ++nextResultId;
            addSemanticMergerMenuItem (
                model.menu,
                resultId,
                source->getName()
                    + " ("
                    + String (
                        source
                            ->getNodeId())
                    + ")",
                true,
                inputId
                    + ".source."
                    + String (
                        source
                            ->getNodeId()),
                "Connect "
                    + source
                          ->getName()
                    + " node "
                    + String (
                        source
                            ->getNodeId())
                    + " to merger input "
                    + inputName
                    + ".");
            model.choices
                .push_back ({ resultId,
                              inputIndex,
                              source
                                  ->getNodeId(),
                              currentSource });
        }
    };

    addInput (
        0,
        merger.sourceNodeA);
    model.menu.addItem (
        ++nextResultId,
        " ",
        false);
    addInput (
        1,
        merger.sourceNodeB);

    return model;
}

} // namespace

class MergerEditorAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    explicit MergerEditorAccessibilityHandler (
        MergerEditor& editorToWrap)
        : AccessibilityHandler (
              editorToWrap,
              AccessibilityRole::group,
              createActions (editorToWrap)),
          state (editorToWrap.inputSelectionMenuState)
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto currentState =
            AccessibilityHandler::getCurrentState()
                .withExpandable();
        return state->shouldBeOpen.load()
                   ? currentState.withExpanded()
                   : currentState.withCollapsed();
    }

private:
    static AccessibilityActions createActions (
        MergerEditor& editor)
    {
        const auto state =
            editor.inputSelectionMenuState;
        const auto safeEditor =
            std::make_shared<
                Component::SafePointer<
                    MergerEditor>> (
                &editor);

        return AccessibilityActions().addAction (
            AccessibilityActionType::showMenu,
            [state, safeEditor]
            {
                const auto request =
                    state->toggle();
                const auto applyRequest =
                    [state,
                     safeEditor,
                     request]
                {
                    if (state->generation.load()
                            != request.generation
                        || *safeEditor == nullptr)
                    {
                        return;
                    }

                    (*safeEditor)
                        ->applyInputSelectionMenuRequest (
                            request.shouldBeOpen,
                            true,
                            request.generation);
                };

                if (MessageManager::getInstance()
                        ->isThisTheMessageThread())
                {
                    applyRequest();
                }
                else
                {
                    MessageManager::callAsync (
                        applyRequest);
                }
            });
    }

    std::shared_ptr<MergerInputMenuState> state;
};

MergerEditor::MergerEditor (GenericProcessor* parentNode)
    : GenericEditor (parentNode),
      inputSelectionMenuState (
          std::make_shared<
              MergerInputMenuState>())

{
    desiredWidth = 90;

    pipelineSelectorA = std::make_unique<ImageButton> ("Pipeline A");
    applySemanticMetadata (
        *pipelineSelectorA,
        "oe.processor."
            + String (
                parentNode
                    ->getNodeId())
            + ".route.input.a",
        "Merger input A",
        "Show merger input path A in the signal chain.");
    pipelineSelectorA->setRadioGroupId (
        2102,
        dontSendNotification);

    Image normalImageA = ImageCache::getFromMemory (BinaryData::MergerB01_png, BinaryData::MergerB01_pngSize);
    Image downImageA = ImageCache::getFromMemory (BinaryData::MergerA01_png, BinaryData::MergerA01_pngSize);
    Image normalImageB = ImageCache::getFromMemory (BinaryData::MergerA02_png, BinaryData::MergerA02_pngSize);
    Image downImageB = ImageCache::getFromMemory (BinaryData::MergerB02_png, BinaryData::MergerB02_pngSize);

    pipelineSelectorA->setImages (true, true, true, normalImageA, 1.0f, Colours::white.withAlpha (0.0f), normalImageA, 1.0f, Colours::black.withAlpha (0.0f), downImageA, 1.0f, Colours::white.withAlpha (0.0f));

    pipelineSelectorA->addListener (this);
    pipelineSelectorA->setBounds (-10, 25, 95, 50);
    pipelineSelectorA->setToggleState (true, dontSendNotification);
    addAndMakeVisible (pipelineSelectorA.get());

    pipelineSelectorB = std::make_unique<ImageButton> ("Pipeline B");
    applySemanticMetadata (
        *pipelineSelectorB,
        "oe.processor."
            + String (
                parentNode
                    ->getNodeId())
            + ".route.input.b",
        "Merger input B",
        "Show merger input path B in the signal chain.");
    pipelineSelectorB->setRadioGroupId (
        2102,
        dontSendNotification);

    pipelineSelectorB->setImages (true, true, true, normalImageB, 1.0f, Colours::white.withAlpha (0.0f), normalImageB, 1.0f, Colours::black.withAlpha (0.0f), downImageB, 1.0f, Colours::white.withAlpha (0.0f));

    pipelineSelectorB->addListener (this);
    pipelineSelectorB->setBounds (-10, 75, 95, 50);
    pipelineSelectorB->setToggleState (false, dontSendNotification);
    addAndMakeVisible (pipelineSelectorB.get());
}

void MergerEditor::startAcquisition()
{
}

void MergerEditor::stopAcquisition()
{
}

void MergerEditor::buttonClicked (Button* button)
{
    if (button == pipelineSelectorA.get())
    {
        AccessClass::getEditorViewport()->switchIO (getProcessor(), 0);
    }
    else if (button == pipelineSelectorB.get())
    {
        AccessClass::getEditorViewport()->switchIO (getProcessor(), 1);
    }
}

Array<GenericProcessor*> MergerEditor::getSelectableProcessors()
{
    Array<GenericProcessor*> selectableProcessors;

    auto* graph =
        AccessClass::
            getProcessorGraph();
    if (graph == nullptr)
        return selectableProcessors;

    Array<GenericProcessor*> availableProcessors =
        graph->getListOfProcessors();

    if (availableProcessors.size() > 0)
    {
        for (auto& processorToCheck : availableProcessors)
        {
            if (! processorToCheck->isSplitter() && processorToCheck != getProcessor() && processorToCheck->getDestNode() == 0)
            {
                bool isDownstream = false;
                GenericProcessor* sourceNode = processorToCheck->getSourceNode();

                while (sourceNode != 0)
                {
                    if (sourceNode == getProcessor())
                    {
                        isDownstream = true;
                        break;
                    }

                    sourceNode = sourceNode->getSourceNode();
                }

                if (! isDownstream)
                {
                    selectableProcessors.add (processorToCheck);
                }
            }
        }
    }

    return selectableProcessors;
}

std::unique_ptr<
    AccessibilityHandler>
    MergerEditor::
        createAccessibilityHandler()
{
    return std::make_unique<
        MergerEditorAccessibilityHandler> (
        *this);
}

void MergerEditor::
    toggleInputSelectionMenu (
        bool asynchronously)
{
    const auto request =
        inputSelectionMenuState
            ->toggle();
    applyInputSelectionMenuRequest (
        request.shouldBeOpen,
        asynchronously,
        request.generation);
}

void MergerEditor::
    applyInputSelectionMenuRequest (
        bool shouldBeOpen,
        bool asynchronously,
        uint64 generation)
{
    if (inputSelectionMenuState
            ->generation
            .load()
        != generation)
    {
        return;
    }

    if (! shouldBeOpen)
    {
        PopupMenu::dismissAllActiveMenus();
        if (auto* handler =
                getAccessibilityHandler())
        {
            handler->notifyAccessibilityEvent (
                AccessibilityEvent::structureChanged);
        }
        return;
    }

    if (auto* handler =
            getAccessibilityHandler())
    {
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::structureChanged);
    }

    if (inputSelectionMenuState
            ->activeMenuGeneration
            .load()
        != 0)
    {
        return;
    }

    if (! asynchronously)
    {
        openInputSelectionMenu (
            false,
            generation);
        return;
    }

    const auto state =
        inputSelectionMenuState;
    const auto safeEditor =
        std::make_shared<
            Component::SafePointer<
                MergerEditor>> (
            this);
    MessageManager::callAsync (
        [state,
         safeEditor,
         generation]
        {
            if (state->generation.load()
                    != generation
                || ! state->shouldBeOpen.load()
                || *safeEditor == nullptr)
            {
                return;
            }

            (*safeEditor)
                ->openInputSelectionMenu (
                    true,
                    generation);
        });
}

void MergerEditor::
    openInputSelectionMenu (
        bool asynchronously,
        uint64 generation)
{
    if (inputSelectionMenuState
                ->generation
                .load()
            != generation
        || ! inputSelectionMenuState
                  ->shouldBeOpen
                  .load())
    {
        return;
    }

    uint64 noActiveMenu = 0;
    if (! inputSelectionMenuState
              ->activeMenuGeneration
              .compare_exchange_strong (
                  noActiveMenu,
                  generation))
    {
        return;
    }

    auto model =
        createMergerInputMenuModel (
            *static_cast<
                Merger*> (
                getProcessor()),
            getSelectableProcessors());
    const auto safeEditor =
        Component::SafePointer<
            MergerEditor> (
            this);
    const auto state =
        inputSelectionMenuState;
    auto performSelection =
        [safeEditor,
         state,
         generation,
         choices = std::move (
             model.choices)] (
            int result)
    {
        uint64 activeGeneration =
            generation;
        if (! state
                  ->activeMenuGeneration
                  .compare_exchange_strong (
                      activeGeneration,
                      0))
        {
            return;
        }

        if (state->generation.load()
            != generation)
        {
            if (state->shouldBeOpen.load()
                && safeEditor != nullptr)
            {
                const auto currentGeneration =
                    state->generation.load();
                Timer::callAfterDelay (
                    20,
                    [safeEditor,
                     state,
                     currentGeneration]
                    {
                        if (safeEditor != nullptr
                            && state->generation.load()
                                   == currentGeneration
                            && state->shouldBeOpen.load())
                        {
                            safeEditor
                                ->applyInputSelectionMenuRequest (
                                    true,
                                    true,
                                    currentGeneration);
                        }
                    });
            }
            return;
        }

        if (safeEditor == nullptr
            || ! state->finish (
                generation))
        {
            return;
        }

        if (auto* handler =
                safeEditor
                    ->getAccessibilityHandler())
        {
            handler
                ->notifyAccessibilityEvent (
                    AccessibilityEvent::
                        structureChanged);
        }

        const auto choice =
            std::find_if (
                choices.begin(),
                choices.end(),
                [result] (
                    const auto& candidate)
                {
                    return candidate
                               .resultId
                           == result;
                });
        if (choice
            == choices.end())
            return;

        auto* graph =
            AccessClass::
                getProcessorGraph();
        if (graph == nullptr)
            return;

        auto* merger =
            dynamic_cast<
                Merger*> (
                safeEditor
                    ->getProcessor());
        if (merger == nullptr
            || graph
                       ->getProcessorWithNodeId (
                           merger
                               ->getNodeId())
                   != merger
            || merger
                       ->getSourceNode (
                           choice
                               ->inputIndex)
                   != choice
                          ->expectedInputSource)
        {
            return;
        }

        auto* source =
            graph
                ->getProcessorWithNodeId (
                    choice
                        ->sourceNodeId);
        if (source == nullptr
            || ! safeEditor
                     ->getSelectableProcessors()
                     .contains (
                         source))
            return;

        graph->connectMergerSource (
            safeEditor
                ->getProcessor(),
            source,
            choice
                ->inputIndex);
        graph->updateSettings (
            safeEditor
                ->getProcessor());
    };

    if (! asynchronously)
    {
        const auto result =
            model.menu.show();
        performSelection (
            result);
        return;
    }

    model.menu.showMenuAsync (
        PopupMenu::Options()
            .withTargetComponent (
                this),
        ModalCallbackFunction::create (
            std::move (
                performSelection)));
}

void MergerEditor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
        toggleInputSelectionMenu (
            false);
}

Array<GenericEditor*> MergerEditor::getConnectedEditors()
{
    Array<GenericEditor*> editors;

    Merger* processor = (Merger*) getProcessor();

    for (int pathNum = 0; pathNum < 2; pathNum++)
    {
        if (processor->getSourceNode (pathNum) != nullptr)
            editors.add (processor->getSourceNode (pathNum)->getEditor());
        else
            editors.add (nullptr);
    }

    return editors;
}

int MergerEditor::getPathForEditor (GenericEditor* editor)
{
    Merger* processor = (Merger*) getProcessor();

    for (int pathNum = 0; pathNum < 2; pathNum++)
    {
        if (processor->getSourceNode (pathNum) != nullptr)
        {
            if (processor->getEditor() == editor)
                return pathNum;
        }
    }

    return -1;
}

void MergerEditor::switchIO (int source)
{
    switchSource (source);

    select();
}

void MergerEditor::switchSource()
{
    bool isBOn = pipelineSelectorB->getToggleState();
    bool isAOn = pipelineSelectorA->getToggleState();

    pipelineSelectorB->setToggleState (! isBOn, dontSendNotification);
    pipelineSelectorA->setToggleState (! isAOn, dontSendNotification);

    Merger* processor = (Merger*) getProcessor();
    processor->switchIO();
}

void MergerEditor::switchSource (int source, bool notify)
{
    if (source == 0)
    {
        pipelineSelectorA->setToggleState (true, dontSendNotification);
        pipelineSelectorB->setToggleState (false, dontSendNotification);
    }
    else if (source == 1)
    {
        pipelineSelectorB->setToggleState (true, dontSendNotification);
        pipelineSelectorA->setToggleState (false, dontSendNotification);
    }
    else
    {
        return;
    }

    if (notify)
    {
        Merger* processor = (Merger*) getProcessor();
        processor->switchIO (source);
    }
}

void MergerEditor::updateSettings()
{
    /*for (auto button : streamButtons)
    {
        if (!incomingStreams.contains(button->getStreamId()))
        {
            streamButtonHolder->remove(button);
            streamButtons.removeObject(button);
        }
    }

    incomingStreams.clear();*/
}
