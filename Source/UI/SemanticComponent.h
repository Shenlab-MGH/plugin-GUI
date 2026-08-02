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

#ifndef SEMANTICCOMPONENT_H
#define SEMANTICCOMPONENT_H

#include "../../JuceLibraryCode/JuceHeader.h"
#include "../TestableExport.h"

TESTABLE bool isValidSemanticId (StringRef id);
TESTABLE String sanitiseSemanticSegment (StringRef segment);
TESTABLE String createProcessorControlSemanticId (int nodeId,
                                                  StringRef controlName);

/** Returns the configuration-scoped semantic segment for a stream.
    The non-empty identifier is preferred; displayNameFallback is used only
    when the stream has no identifier. The fallback form is not durable
    identity and must be rediscovered after configuration changes. */
TESTABLE String createStreamSemanticSegment (
    int sourceNodeId,
    StringRef identifier,
    StringRef displayNameFallback);

/** Returns whether the indexed stream needs an index suffix to distinguish
    its sanitised semantic segment from its current siblings. */
TESTABLE bool streamSemanticSegmentNeedsIndexSuffix (
    const StringArray& siblingSemanticSegments,
    int streamIndex);

/** Returns the Windows UIA AutomationId for a stream selector table row.
    This locator is scoped to the processor's current configuration. Rows
    whose sanitised segments collide receive a deterministic index suffix;
    non-colliding row IDs remain unchanged. */
TESTABLE String createStreamSelectorRowSemanticId (
    StringRef tableSemanticId,
    const StringArray& siblingSemanticSegments,
    int streamIndex);
TESTABLE std::unique_ptr<AccessibilityHandler>
createReadOnlyProgressAccessibilityHandler (
    Component& component,
    std::function<double()> getValue);

TESTABLE std::unique_ptr<AccessibilityHandler>
createReadOnlyTextAccessibilityHandler (
    Component& component,
    std::function<String()> getValue);

class TESTABLE ReadOnlyValueTextButton : public TextButton
{
public:
    using TextButton::TextButton;
    ~ReadOnlyValueTextButton() override;

    std::unique_ptr<AccessibilityHandler>
    createAccessibilityHandler() override;

    /** Publishes the current value and semantic metadata for worker-thread
        accessibility providers. Must be called on the message thread. */
    void refreshAccessibilityState();

    void enablementChanged() override;
    void focusGained (FocusChangeType cause) override;
    void focusLost (FocusChangeType cause) override;

private:
    struct AccessibilityState;
    class AccessibilityHandlerImpl;

    std::shared_ptr<AccessibilityState>
        accessibilityState;
};

TESTABLE void setButtonTextWithAccessibilityValue (
    Button& button,
    StringRef value);

TESTABLE void addSemanticCommandItem (
    PopupMenu& menu,
    ApplicationCommandManager* commandManager,
    CommandID commandId,
    StringRef semanticId);

TESTABLE void addSemanticSubMenu (
    PopupMenu& menu,
    StringRef name,
    PopupMenu subMenu,
    StringRef semanticId,
    StringRef description);

TESTABLE void applySemanticMetadata (Component& component,
                                     StringRef id,
                                     StringRef title,
                                     StringRef description,
                                     StringRef help = {});

template <typename ComponentType,
          std::enable_if_t<
              std::is_base_of_v<Component, ComponentType>
                  && std::is_base_of_v<SettableTooltipClient, ComponentType>,
              int> = 0>
void applySemanticMetadata (ComponentType& component,
                            StringRef id,
                            StringRef title,
                            StringRef description,
                            StringRef help = {})
{
    applySemanticMetadata (static_cast<Component&> (component),
                           id,
                           title,
                           description,
                           help);
    component.setTooltip (String (help));
}

#endif // SEMANTICCOMPONENT_H
