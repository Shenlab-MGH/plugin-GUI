#include "../../Source/Processors/AudioNode/AudioEditor.h"
#include "gtest/gtest.h"

namespace
{
void expectSemanticComponent (Component& component,
                              const String& expectedId,
                              const String& expectedTitle,
                              const String& expectedDescription)
{
    EXPECT_EQ (component.getComponentID(), expectedId);
    EXPECT_EQ (component.getTitle(), expectedTitle);
    EXPECT_EQ (component.getDescription(), expectedDescription);
    EXPECT_TRUE (component.isAccessible());
}
} // namespace

TEST (AudioEditorAccessibilityTests, ExposesAudioControls)
{
    MuteButton mute;
    expectSemanticComponent (mute,
                             "oe.audio.mute",
                             "Mute audio output",
                             "Mute or restore monitored audio output.");
    EXPECT_TRUE (mute.getClickingTogglesState());

    AudioWindowButton settings;
    expectSemanticComponent (settings,
                             "oe.audio.settings",
                             "Audio settings",
                             "Show or hide audio device settings and buffer size.");
    EXPECT_TRUE (settings.getClickingTogglesState());

    AudioLevelSlider volume (AudioLevelSlider::Kind::volume);
    expectSemanticComponent (volume,
                             "oe.audio.volume",
                             "Audio volume",
                             "Set monitored audio output volume.");
    EXPECT_DOUBLE_EQ (volume.getMinimum(), 0.0);
    EXPECT_DOUBLE_EQ (volume.getMaximum(), 100.0);

    AudioLevelSlider noiseGate (AudioLevelSlider::Kind::noiseGate);
    expectSemanticComponent (noiseGate,
                             "oe.audio.noise_gate",
                             "Audio noise gate",
                             "Set the monitored audio noise gate threshold.");
    EXPECT_DOUBLE_EQ (noiseGate.getMinimum(), 0.0);
    EXPECT_DOUBLE_EQ (noiseGate.getMaximum(), 100.0);
}
