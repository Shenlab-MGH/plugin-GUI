#include "../../Source/Processors/AudioNode/AudioEditor.h"
#include "../../Source/Processors/AudioMonitor/AudioMonitorEditor.h"
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

Component* findDescendantWithId (Component& parent, StringRef id)
{
    if (parent.getComponentID() == id)
        return &parent;

    for (auto* child : parent.getChildren())
        if (auto* match = findDescendantWithId (*child, id))
            return match;

    return nullptr;
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

TEST (AudioEditorAccessibilityTests, ExposesAudioMonitorParameterControls)
{
    CategoricalParameter outputParameter (
        nullptr,
        Parameter::PROCESSOR_SCOPE,
        "audio_output",
        "Audio output",
        "Choose the monitored audio output channel.",
        { "Left", "Both", "Right" },
        1);
    outputParameter.setKey ("106|Audio Output: Left/Right");

    AudioOutputSelector outputSelector (&outputParameter);
    const auto outputAutomationId = outputParameter.getAutomationId();
    auto* outputGroup = outputSelector.findChildWithID (outputAutomationId);
    ASSERT_NE (outputGroup, nullptr);
    EXPECT_EQ (outputGroup->getComponentID(), outputAutomationId);
    EXPECT_NE (findDescendantWithId (*outputGroup, outputAutomationId + ".left"), nullptr);
    EXPECT_NE (findDescendantWithId (*outputGroup, outputAutomationId + ".both"), nullptr);
    EXPECT_NE (findDescendantWithId (*outputGroup, outputAutomationId + ".right"), nullptr);

    BooleanParameter muteParameter (
        nullptr,
        Parameter::PROCESSOR_SCOPE,
        "mute_audio",
        "Mute audio",
        "Mute monitored audio output.",
        false);
    muteParameter.setKey ("106|Mute Audio: Main");

    MonitorMuteButton mute (&muteParameter);
    const auto muteAutomationId = muteParameter.getAutomationId();
    auto* muteControl = mute.findChildWithID (muteAutomationId);
    ASSERT_NE (muteControl, nullptr);
    EXPECT_EQ (muteControl->getComponentID(), muteAutomationId);
    EXPECT_EQ (muteControl->getTitle(), "Mute audio");
    EXPECT_EQ (muteControl->getDescription(), "Mute monitored audio output.");
}
