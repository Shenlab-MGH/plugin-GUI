#include "../../Source/AccessClass.cpp"

namespace AccessClass
{
// Test-build-only seam for exercising ProcessorGraph's non-console wiring without
// constructing the full application UI and its unrelated plugin dependencies.
void JUCE_API setEditorViewportForTesting (EditorViewport* viewport)
{
    ev = viewport;
}
} // namespace AccessClass
