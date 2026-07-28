#include "../../Source/Processors/Merger/Merger.h"
#include "../../Source/Processors/Merger/MergerEditor.h"
#include "../../Source/Processors/Splitter/Splitter.h"
#include "../../Source/Processors/Splitter/SplitterEditor.h"
#include "gtest/gtest.h"

namespace
{
Component* findDescendantById (
    Component& parent,
    StringRef componentId)
{
    for (auto* child :
         parent.getChildren())
    {
        if (child->getComponentID()
            == componentId)
            return child;

        if (auto* descendant =
                findDescendantById (
                    *child,
                    componentId))
            return descendant;
    }

    return nullptr;
}

void expectRouteChoice (
    Component& editor,
    StringRef componentId,
    StringRef title,
    StringRef description,
    bool expectedChecked)
{
    auto* component =
        findDescendantById (
            editor,
            componentId);
    ASSERT_NE (
        component,
        nullptr);
    EXPECT_EQ (
        component->getTitle(),
        title);
    EXPECT_EQ (
        component
            ->getDescription(),
        description);
    EXPECT_TRUE (
        component
            ->isAccessible());

    auto* button =
        dynamic_cast<
            Button*> (
                component);
    ASSERT_NE (
        button,
        nullptr);
    auto* handler =
        button
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::
            radioButton);
    EXPECT_TRUE (
        handler
            ->getActions()
            .contains (
                AccessibilityActionType::
                    press));
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isCheckable());
    EXPECT_EQ (
        handler
            ->getCurrentState()
            .isChecked(),
        expectedChecked);
}
} // namespace

TEST (ProcessorRoutingAccessibilityTests,
      ExposesSplitterOutputPathChoices)
{
    MessageManager::
        getInstance();
    MessageManagerLock
        messageManagerLock;

    Splitter processor;
    processor.setNodeId (101);
    auto* editor =
        dynamic_cast<
            SplitterEditor*> (
                processor
                    .createEditor());
    ASSERT_NE (
        editor,
        nullptr);
    editor->setBounds (
        0,
        0,
        200,
        150);
    editor->addToDesktop (0);

    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.a",
        "Splitter output A",
        "Show splitter output path A in the signal chain.",
        true);
    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.b",
        "Splitter output B",
        "Show splitter output path B in the signal chain.",
        false);

    editor->switchDest (1);

    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.a",
        "Splitter output A",
        "Show splitter output path A in the signal chain.",
        false);
    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.b",
        "Splitter output B",
        "Show splitter output path B in the signal chain.",
        true);
    EXPECT_EQ (
        processor.getPath(),
        1);
}

TEST (ProcessorRoutingAccessibilityTests,
      ExposesMergerInputPathChoices)
{
    MessageManager::
        getInstance();
    MessageManagerLock
        messageManagerLock;

    Merger processor;
    processor.setNodeId (102);
    auto* editor =
        dynamic_cast<
            MergerEditor*> (
                processor
                    .createEditor());
    ASSERT_NE (
        editor,
        nullptr);
    editor->setBounds (
        0,
        0,
        200,
        150);
    editor->addToDesktop (0);

    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.a",
        "Merger input A",
        "Show merger input path A in the signal chain.",
        true);
    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.b",
        "Merger input B",
        "Show merger input path B in the signal chain.",
        false);

    editor->switchSource (1);

    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.a",
        "Merger input A",
        "Show merger input path A in the signal chain.",
        false);
    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.b",
        "Merger input B",
        "Show merger input path B in the signal chain.",
        true);
    EXPECT_EQ (
        processor.getPath(),
        1);
}
