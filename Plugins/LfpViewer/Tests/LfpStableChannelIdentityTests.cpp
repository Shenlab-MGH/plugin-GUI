/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ------------------------------------------------------------------
*/

#include "gtest/gtest.h"

#include "../DisplayBuffer.h"
#include "../LfpChannelDisplay.h"
#include "../LfpChannelDisplayInfo.h"
#include "../LfpDisplay.h"
#include "../LfpDisplayCanvas.h"
#include "../LfpDisplayEditor.h"
#include "../LfpDisplayOptions.h"
#include "../LfpStableChannelIdentity.h"
#include <ModelProcessors.h>
#include <TestFixtures.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using namespace LfpViewer;

template <typename Identity,
          typename = void>
struct CanMintSuccessorGeneration
    : std::false_type
{
};

template <typename Identity>
struct CanMintSuccessorGeneration<
    Identity,
    std::void_t<decltype (
        std::declval<const Identity&>()
            .createSuccessorGeneration())>>
    : std::true_type
{
};

template <typename Identity,
          typename = void>
struct CanRevokeGeneration
    : std::false_type
{
};

template <typename Identity>
struct CanRevokeGeneration<
    Identity,
    std::void_t<decltype (
        std::declval<const Identity&>()
            .revokeAgentActionability())>>
    : std::true_type
{
};

static_assert (
    ! CanMintSuccessorGeneration<
        LfpStableChannelIdentity>::value,
    "Identity providers must not mint active successor generations");
static_assert (
    ! CanRevokeGeneration<
        LfpStableChannelIdentity>::value,
    "Identity providers must have a read-only snapshot API");

template <typename Display,
          typename = void>
struct CanPublishIdentityGenerations
    : std::false_type
{
};

template <typename Display>
struct CanPublishIdentityGenerations<
    Display,
    std::void_t<decltype (
        std::declval<Display&>()
            .bindStableChannelIdentities (
                std::declval<const std::vector<
                    std::shared_ptr<
                        const LfpStableChannelIdentity>>&>()))>>
    : std::true_type
{
};

template <typename Display,
          typename = void>
struct CanRefreshIdentityGenerations
    : std::false_type
{
};

template <typename Display>
struct CanRefreshIdentityGenerations<
    Display,
    std::void_t<decltype (
        std::declval<Display&>()
            .refreshStableChannelIdentityAvailability())>>
    : std::true_type
{
};

static_assert (
    ! CanPublishIdentityGenerations<
        LfpDisplay>::value,
    "Providers must not publish identity generations through LfpDisplay");
static_assert (
    ! CanRefreshIdentityGenerations<
        LfpDisplay>::value,
    "Providers must not activate identity generations through LfpDisplay");

template <typename Canvas,
          typename = void>
struct CanCreateStableChannelActionRequest
    : std::false_type
{
};

template <typename Canvas>
struct CanCreateStableChannelActionRequest<
    Canvas,
    std::void_t<decltype (
        std::declval<Canvas&>()
            .createStableChannelActionRequest (
                std::declval<std::shared_ptr<
                    const LfpStableChannelIdentity>>()))>>
    : std::true_type
{
};

template <typename Request,
          typename = void>
struct CanPerformArbitraryStableChannelCallback
    : std::false_type
{
};

template <typename Request>
struct CanPerformArbitraryStableChannelCallback<
    Request,
    std::void_t<decltype (
        std::declval<const Request&>()
            .performIfCurrentAndAvailable (
                std::declval<
                    const std::function<void()>&>()))>>
    : std::true_type
{
};

template <typename Request,
          typename = void>
struct CanCallLegacyStableChannelValidate
    : std::false_type
{
};

template <typename Request>
struct CanCallLegacyStableChannelValidate<
    Request,
    std::void_t<decltype (
        std::declval<const Request&>()
            .validate())>>
    : std::true_type
{
};

template <typename Request,
          typename = void>
struct CanValidateCurrentStableChannelAvailability
    : std::false_type
{
};

template <typename Request>
struct CanValidateCurrentStableChannelAvailability<
    Request,
    std::void_t<decltype (
        std::declval<const Request&>()
            .validateCurrentAndAvailable())>>
    : std::true_type
{
};

template <typename Provider,
          typename = void>
struct CanCopyPublishedStableChannelActionRequest
    : std::false_type
{
};

template <typename Provider>
struct CanCopyPublishedStableChannelActionRequest<
    Provider,
    std::void_t<decltype (
        std::declval<const Provider&>()
            .getStableChannelActionRequest())>>
    : std::true_type
{
};

static_assert (
    ! CanCreateStableChannelActionRequest<
        LfpDisplayCanvas>::value,
    "Workers and providers must not create stable channel requests");
static_assert (
    ! CanPerformArbitraryStableChannelCallback<
        LfpStableChannelActionRequest>::value,
    "Stable channel requests must not accept arbitrary callbacks");
static_assert (
    ! CanCallLegacyStableChannelValidate<
        LfpStableChannelActionRequest>::value,
    "Diagnostic validation must not be presented as mutation authorization");
static_assert (
    CanValidateCurrentStableChannelAvailability<
        LfpStableChannelActionRequest>::value,
    "Published requests must expose read-only current-availability diagnostics");
static_assert (
    CanCopyPublishedStableChannelActionRequest<
        LfpChannelDisplay>::value,
    "Providers must only copy requests already published by the owner");
static_assert (
    ! std::is_default_constructible<
        LfpStableChannelActionRequest>::value,
    "Workers must not default-construct stable channel requests");
static_assert (
    ! std::is_constructible<
        LfpStableChannelActionRequest,
        Component*,
        std::shared_ptr<
            const LfpStableChannelIdentity>>::value,
    "Stable channel requests must not expose a raw component constructor");

class ScopedStableChannelDiagnosticTestSeam final
{
public:
    ScopedStableChannelDiagnosticTestSeam (
        LfpStableChannelDiagnosticDispatcherForTests
            dispatcher,
        std::function<void()> validationHook)
    {
        setStableChannelDiagnosticDispatcherForTests (
            std::move (
                dispatcher));
        setStableChannelDiagnosticValidationHookForTests (
            std::move (
                validationHook));
    }

    ~ScopedStableChannelDiagnosticTestSeam()
    {
        setStableChannelDiagnosticValidationHookForTests (
            {});
        setStableChannelDiagnosticDispatcherForTests (
            {});
    }

private:
    JUCE_DECLARE_NON_COPYABLE (
        ScopedStableChannelDiagnosticTestSeam)
};

DisplayBuffer::ChannelMetadata makeIdentityMetadata (
    String identifier,
    int sourceNodeId,
    int localIndex,
    Uuid uuid = Uuid())
{
    DisplayBuffer::ChannelMetadata metadata;
    metadata.identifier = std::move (identifier);
    metadata.sourceNodeId = sourceNodeId;
    metadata.localIndex = localIndex;
    metadata.uuid = uuid;
    metadata.name = metadata.identifier;
    metadata.type =
        ContinuousChannel::Type::ELECTRODE;
    return metadata;
}

std::unique_ptr<DisplayBuffer> makeIdentityBuffer (
    int id,
    String streamKey,
    std::initializer_list<DisplayBuffer::ChannelMetadata> metadata)
{
    auto buffer = std::make_unique<DisplayBuffer> (
        id,
        "Stream " + String (id),
        std::move (streamKey),
        30000.0f);
    for (const auto& channel : metadata)
        buffer->channelMetadata.add (channel);
    buffer->numChannels = buffer->channelMetadata.size();
    return buffer;
}

std::vector<LfpDisplaySplitter*> getIdentityTestSplitters (
    LfpDisplayCanvas& canvas)
{
    std::vector<LfpDisplaySplitter*> result;
    std::function<void (Component&)> visit =
        [&] (Component& component)
        {
            if (auto* splitter =
                    dynamic_cast<LfpDisplaySplitter*> (
                        &component))
            {
                result.push_back (splitter);
            }
            for (int child = 0;
                 child < component.getNumChildComponents();
                 ++child)
            {
                visit (*component.getChildComponent (child));
            }
        };
    visit (canvas);
    std::sort (
        result.begin(),
        result.end(),
        [] (const auto* lhs, const auto* rhs)
        {
            return lhs->splitID < rhs->splitID;
        });
    return result;
}

UtilityButton* getIdentityTestEnableButton (
    LfpChannelDisplayInfo& channelInfo)
{
    UtilityButton* enableButton = nullptr;
    int buttonCount = 0;
    int utilityButtonCount = 0;
    for (int child = 0;
         child
         < channelInfo
               .getNumChildComponents();
         ++child)
    {
        auto* component =
            channelInfo.getChildComponent (
                child);
        if (dynamic_cast<Button*> (
                component)
            != nullptr)
        {
            ++buttonCount;
        }
        if (auto* utilityButton =
                dynamic_cast<UtilityButton*> (
                    component))
        {
            ++utilityButtonCount;
            enableButton = utilityButton;
        }
    }
    return buttonCount == 1
            && utilityButtonCount == 1
        ? enableButton
        : nullptr;
}

std::unique_ptr<AccessibilityHandler>
createIdentityTestEnableButtonHandler (
    UtilityButton& button)
{
    return static_cast<Component*> (
               &button)
        ->createAccessibilityHandler();
}

String decodeIdentityTestUtf8Hex (
    StringRef text)
{
    const String value (text);
    if (value.isEmpty()
        || value.length() % 2 != 0)
    {
        return {};
    }

    std::string bytes;
    bytes.reserve (
        static_cast<size_t> (
            value.length() / 2));
    const auto nibble =
        [] (juce_wchar character)
        {
            if (character >= '0'
                && character <= '9')
            {
                return int (character - '0');
            }
            if (character >= 'A'
                && character <= 'F')
            {
                return int (character - 'A') + 10;
            }
            return -1;
        };
    for (int index = 0;
         index < value.length();
         index += 2)
    {
        const auto high =
            nibble (value[index]);
        const auto low =
            nibble (value[index + 1]);
        if (high < 0
            || low < 0)
        {
            return {};
        }
        bytes.push_back (
            static_cast<char> (
                (high << 4) | low));
    }

    return String::fromUTF8 (
        bytes.data(),
        static_cast<int> (
            bytes.size()));
}

void configureIdentityPersistenceChannel (
    DisplayBuffer& buffer,
    int channelIndex,
    String identifier,
    int sourceNodeId,
    int localIndex,
    String channelName,
    ContinuousChannel::Type channelType)
{
    auto& metadata =
        buffer.channelMetadata
            .getReference (
                channelIndex);
    metadata.identifier =
        std::move (
            identifier);
    metadata.sourceNodeId =
        sourceNodeId;
    metadata.localIndex =
        localIndex;
    metadata.name =
        std::move (
            channelName);
    metadata.type =
        channelType;
}

void showAllIdentityTestChannels (
    LfpDisplaySplitter& splitter)
{
    for (int channel = 0;
         channel
         < splitter.lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        splitter.lfpDisplay
            ->setEnabledState (
                true,
                channel,
                true);
    }
}

XmlElement* getIdentityTestPane (
    XmlElement& root,
    int paneIndex = 0)
{
    return root.getChildByName (
        "LFPDISPLAY"
        + String (
            paneIndex));
}

XmlElement* getIdentityTestVisibilityState (
    XmlElement& root,
    int paneIndex = 0)
{
    auto* pane =
        getIdentityTestPane (
            root,
            paneIndex);
    return pane != nullptr
        ? pane->getChildByName (
              "WAVEFORM_VISIBILITY_STATE")
        : nullptr;
}

XmlElement* getIdentityTestHiddenRecord (
    XmlElement& root,
    int paneIndex = 0)
{
    auto* state =
        getIdentityTestVisibilityState (
            root,
            paneIndex);
    return state != nullptr
        ? state->getFirstChildElement()
        : nullptr;
}

Array<String> drainIdentityTestBroadcastMessages (
    MessageCenter& messageCenter)
{
    AudioBuffer<float> audioBuffer (
        1,
        1);
    audioBuffer.clear();
    MidiBuffer eventBuffer;
    static_cast<AudioProcessor&> (
        messageCenter)
        .processBlock (
            audioBuffer,
            eventBuffer);

    Array<String> messages;
    for (const auto metadata :
         eventBuffer)
    {
        if (auto event =
                TextEvent::deserialize (
                    metadata.data,
                    messageCenter
                        .getMessageChannel()))
        {
            messages.add (
                event->getText());
        }
    }
    return messages;
}

TEST (LfpStableChannelIdentityTests,
      UniqueExactIdentifiersTakePriority)
{
    const auto firstUuid = Uuid();
    const auto secondUuid = Uuid();
    auto buffer = makeIdentityBuffer (
        1,
        "source|stream",
        { makeIdentityMetadata (
              "  Probe.A  ",
              12,
              3,
              firstUuid),
          makeIdentityMetadata (
              "probe.a",
              12,
              3,
              secondUuid) });
    Array<DisplayBuffer*> streams { buffer.get() };

    const auto identities =
        resolveStableChannelIdentities (
            2,
            *buffer,
            streams);

    ASSERT_EQ (identities.size(), 2u);
    for (const auto& identity : identities)
        ASSERT_TRUE (identity->isAgentActionable());
    ASSERT_NE (
        identities[0]->getStableChannelKey(),
        nullptr);
    ASSERT_NE (
        identities[1]->getStableChannelKey(),
        nullptr);
    EXPECT_EQ (
        identities[0]
            ->getStableChannelKey()
            ->getKind(),
        LfpStableChannelKey::Kind::identifier);
    EXPECT_EQ (
        identities[0]
            ->getStableChannelKey()
            ->getIdentifier(),
        "  Probe.A  ");
    EXPECT_EQ (
        identities[1]
            ->getStableChannelKey()
            ->getIdentifier(),
        "probe.a");
}

TEST (LfpStableChannelIdentityTests,
      DuplicateOrBlankIdentifiersUseUniqueSourceLocalPairs)
{
    auto buffer = makeIdentityBuffer (
        1,
        "source|stream",
        { makeIdentityMetadata (
              "duplicate",
              21,
              0),
          makeIdentityMetadata (
              "duplicate",
              21,
              1),
          makeIdentityMetadata (
              " \t ",
              21,
              2) });
    Array<DisplayBuffer*> streams { buffer.get() };

    const auto identities =
        resolveStableChannelIdentities (
            0,
            *buffer,
            streams);

    ASSERT_EQ (identities.size(), 3u);
    for (int index = 0; index < 3; ++index)
    {
        const auto* key =
            identities[static_cast<size_t> (index)]
                ->getStableChannelKey();
        ASSERT_NE (key, nullptr);
        EXPECT_EQ (
            key->getKind(),
            LfpStableChannelKey::Kind::
                sourceAndLocalIndex);
        EXPECT_EQ (key->getSourceNodeId(), 21);
        EXPECT_EQ (key->getLocalIndex(), index);
    }
}

TEST (LfpStableChannelIdentityTests,
      InvalidUuidOrConflictingFallbackExposesNoStableKey)
{
    const auto duplicateUuid = Uuid();
    auto buffer = makeIdentityBuffer (
        1,
        "source|stream",
        { makeIdentityMetadata (
              "",
              30,
              0,
              duplicateUuid),
          makeIdentityMetadata (
              "",
              30,
              1,
              duplicateUuid),
          makeIdentityMetadata (
              "",
              31,
              2,
              Uuid::null()),
          makeIdentityMetadata (
              "",
              32,
              4),
          makeIdentityMetadata (
              "",
              32,
              4),
          makeIdentityMetadata (
              "",
              -1,
              5) });
    Array<DisplayBuffer*> streams { buffer.get() };

    const auto identities =
        resolveStableChannelIdentities (
            0,
            *buffer,
            streams);

    ASSERT_EQ (identities.size(), 6u);
    for (const auto& identity : identities)
    {
        EXPECT_FALSE (identity->isAgentActionable());
        EXPECT_EQ (
            identity->getStableChannelKey(),
            nullptr);
        EXPECT_EQ (
            identity->getRuntimeUuid(),
            Uuid::null());
    }
}

TEST (LfpStableChannelIdentityTests,
      EmptyOrDuplicateStreamKeysInvalidateEveryChannel)
{
    auto empty = makeIdentityBuffer (
        1,
        "",
        { makeIdentityMetadata ("one", 1, 0) });
    auto duplicateA = makeIdentityBuffer (
        2,
        "same",
        { makeIdentityMetadata ("two", 2, 0) });
    auto duplicateB = makeIdentityBuffer (
        3,
        "same",
        { makeIdentityMetadata ("three", 3, 0) });
    auto otherwiseUnique = makeIdentityBuffer (
        4,
        "otherwise|unique",
        { makeIdentityMetadata ("four", 4, 0) });
    Array<DisplayBuffer*> streams {
        empty.get(),
        duplicateA.get(),
        duplicateB.get(),
        otherwiseUnique.get()
    };

    for (auto* buffer : streams)
    {
        const auto identities =
            resolveStableChannelIdentities (
                1,
                *buffer,
                streams);
        ASSERT_EQ (identities.size(), 1u);
        EXPECT_FALSE (
            identities[0]
                ->isAgentActionable());
        EXPECT_EQ (
            identities[0]
                ->getStableChannelKey(),
            nullptr);
    }
}

TEST (LfpStableChannelIdentityTests,
      SnapshotCopiesPaneStreamAndRuntimeGeneration)
{
    const auto uuid = Uuid();
    auto buffer = makeIdentityBuffer (
        1,
        "stable|stream",
        { makeIdentityMetadata (
              "stable.channel",
              55,
              8,
              uuid) });
    Array<DisplayBuffer*> streams { buffer.get() };
    const auto identities =
        resolveStableChannelIdentities (
            2,
            *buffer,
            streams);
    ASSERT_EQ (identities.size(), 1u);
    const auto identity = identities[0];

    buffer->streamKey = "replacement";
    buffer->channelMetadata
        .getReference (0)
        .identifier = "replacement.channel";
    buffer->channelMetadata
        .getReference (0)
        .uuid = Uuid();

    EXPECT_EQ (identity->getPaneIndex(), 2);
    EXPECT_EQ (
        identity->getStreamKey(),
        "stable|stream");
    EXPECT_EQ (
        identity
            ->getStableChannelKey()
            ->getIdentifier(),
        "stable.channel");
    EXPECT_EQ (
        identity->getRuntimeUuid(),
        uuid);
}

TEST (LfpStableChannelIdentityTests,
      ResolvesLargeChannelSetsWithoutChangingIdentitySemantics)
{
    constexpr int channelCount = 12000;
    auto buffer = std::make_unique<DisplayBuffer> (
        1,
        "large stream",
        "large|stream",
        30000.0f);
    buffer->channelMetadata.ensureStorageAllocated (
        channelCount);
    for (int channel = 0;
         channel < channelCount;
         ++channel)
    {
        buffer->channelMetadata.add (
            makeIdentityMetadata (
                "large.channel."
                    + String (channel),
                90,
                channel));
    }
    buffer->numChannels = channelCount;
    Array<DisplayBuffer*> streams {
        buffer.get()
    };

    const auto identities =
        resolveStableChannelIdentities (
            2,
            *buffer,
            streams);

    ASSERT_EQ (
        identities.size(),
        static_cast<size_t> (
            channelCount));
    ASSERT_TRUE (
        identities.front()
            ->isAgentActionable());
    ASSERT_TRUE (
        identities.back()
            ->isAgentActionable());
    EXPECT_EQ (
        identities.front()
            ->getStableChannelKey()
            ->getIdentifier(),
        "large.channel.0");
    EXPECT_EQ (
        identities.back()
            ->getStableChannelKey()
            ->getIdentifier(),
        "large.channel.11999");
}

class LfpStableChannelIdentityBindingTests
    : public testing::Test
{
protected:
    void SetUp() override
    {
        tester =
            std::make_unique<ProcessorTester> (
                TestSourceNodeBuilder (
                    FakeSourceNodeParams {
                        4,
                        30000.0f,
                        0.195f,
                        2 }),
                TestGuiRuntimeLifetime::process);
        processor =
            tester->createProcessor<LfpDisplayNode> (
                Plugin::Processor::SINK);
    }

    std::unique_ptr<LfpDisplayCanvas>
    createIdentityCanvas (
        SplitLayouts layout)
    {
        auto canvas =
            std::make_unique<
                LfpDisplayCanvas> (
                processor,
                layout,
                false);
        canvas->updateSettings();
        canvas->setSize (
            900,
            600);
        canvas->resized();
        canvas->setVisible (
            true);
        canvas->updateSettings();
        return canvas;
    }

    std::unique_ptr<ProcessorTester> tester;
    LfpDisplayNode* processor = nullptr;
};

TEST_F (LfpStableChannelIdentityBindingTests,
        BaselineRawEnableButtonMustExposeStableWaveformVisibilitySurface)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_FALSE (buffers.isEmpty());
    const auto channelName =
        String::fromUTF8 (
            "A.\xCE\xB2");
    buffers[0]->streamKey =
        "task6/stream";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "id/1",
        17,
        0,
        channelName,
        ContinuousChannel::Type::
            ELECTRODE);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "task6/stream"));

    auto* enableButton =
        getIdentityTestEnableButton (
            *splitters[0]
                 ->lfpDisplay
                 ->channelInfo[0]);
    ASSERT_NE (enableButton, nullptr);
    EXPECT_TRUE (enableButton->isVisible());
    EXPECT_TRUE (
        splitters[0]
            ->lfpDisplay
            ->channelInfo[0]
            ->isVisible());
    EXPECT_NE (
        splitters[0]
            ->lfpDisplay
            ->channelInfo[0]
            ->getStableChannelIdentity(),
        nullptr);
    auto handler =
        static_cast<Component*> (
            enableButton)
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    const auto expectedId =
        "oe.processor."
        + String (
              processor
                  ->getNodeId())
        + ".lfp.display_1.stream_hex_7461736B362F73747265616D.channel_identifier_hex_69642F31.waveform_visibility";
    EXPECT_EQ (
        enableButton
            ->getComponentID(),
        expectedId);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::toggleButton);
    EXPECT_EQ (
        handler->getTitle(),
        "LFP display 1 stream \"task6/stream\" channel \""
            + channelName
            + "\" waveform visibility");
    EXPECT_EQ (
        handler->getDescription(),
        "Waveform visibility for channel \""
            + channelName
            + "\" (identifier \"id/1\") in stream \"task6/stream\" of LFP display 1.");
    EXPECT_EQ (
        handler->getHelp(),
        "Show or hide drawing for channel \""
            + channelName
            + "\" (identifier \"id/1\") in stream \"task6/stream\" of LFP display 1. Hiding stops waveform drawing only; it does not disable acquisition, buffering, hardware, or recording selection.");
    EXPECT_TRUE (
        handler->getActions()
            .contains (
                AccessibilityActionType::toggle));
    EXPECT_FALSE (
        handler->getActions()
            .contains (
                AccessibilityActionType::press));
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler->getValueInterface()
            ->isReadOnly());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Visible");
}

TEST (LfpStableChannelIdentityTests,
      WaveformVisibilityUtf8HexIsCanonicalAndReversible)
{
    const auto unicode =
        String::fromUTF8 (
            "A.\xCE\xB2");
    EXPECT_EQ (
        encodeWaveformVisibilityUtf8Hex (
            unicode),
        "412ECEB2");
    EXPECT_EQ (
        encodeWaveformVisibilityUtf8Hex (
            "id/1"),
        "69642F31");
    EXPECT_TRUE (
        encodeWaveformVisibilityUtf8Hex (
            "").isEmpty());

    const auto delimiterFirst =
        String::fromUTF8 (
            "a.b/c");
    const auto delimiterSecond =
        String::fromUTF8 (
            "a/b.c");
    const auto firstEncoded =
        encodeWaveformVisibilityUtf8Hex (
            delimiterFirst);
    const auto secondEncoded =
        encodeWaveformVisibilityUtf8Hex (
            delimiterSecond);
    EXPECT_NE (
        firstEncoded,
        secondEncoded);
    EXPECT_EQ (
        decodeIdentityTestUtf8Hex (
            firstEncoded),
        delimiterFirst);
    EXPECT_EQ (
        decodeIdentityTestUtf8Hex (
            secondEncoded),
        delimiterSecond);
    EXPECT_EQ (
        decodeIdentityTestUtf8Hex (
            encodeWaveformVisibilityUtf8Hex (
                unicode)),
        unicode);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityHandlersExposeEveryPaneAndStableKeyKind)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::THREE_VERT);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3u);
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    const auto unicodeStream =
        String::fromUTF8 (
            "stream/\xCE\xB1");
    const auto unicodeName =
        String::fromUTF8 (
            "Name \xCE\xB2");
    buffers[0]->streamKey =
        unicodeStream;
    buffers[1]->streamKey =
        "source.stream";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "id/1",
        101,
        0,
        unicodeName,
        ContinuousChannel::Type::ELECTRODE);
    configureIdentityPersistenceChannel (
        *buffers[0],
        1,
        "third.id",
        101,
        1,
        "Third",
        ContinuousChannel::Type::ELECTRODE);
    configureIdentityPersistenceChannel (
        *buffers[1],
        0,
        "",
        77,
        5,
        "Source channel",
        ContinuousChannel::Type::AUX);
    canvas->updateSettings();

    struct ExpectedSurface
    {
        int pane;
        String stream;
        int channel;
        String channelName;
        String automationId;
        String stableKeyText;
    };
    const auto node =
        String (
            processor->getNodeId());
    const std::array<ExpectedSurface, 3> expected {
        ExpectedSurface {
            0,
            unicodeStream,
            0,
            unicodeName,
            "oe.processor."
                + node
                + ".lfp.display_1.stream_hex_73747265616D2FCEB1.channel_identifier_hex_69642F31.waveform_visibility",
            "identifier \"id/1\"" },
        ExpectedSurface {
            1,
            "source.stream",
            0,
            "Source channel",
            "oe.processor."
                + node
                + ".lfp.display_2.stream_hex_736F757263652E73747265616D.channel_source_77_local_5.waveform_visibility",
            "source node 77, local channel index 5" },
        ExpectedSurface {
            2,
            unicodeStream,
            1,
            "Third",
            "oe.processor."
                + node
                + ".lfp.display_3.stream_hex_73747265616D2FCEB1.channel_identifier_hex_74686972642E6964.waveform_visibility",
            "identifier \"third.id\"" } };

    for (const auto& entry : expected)
    {
        ASSERT_TRUE (
            splitters[
                static_cast<size_t> (
                    entry.pane)]
                ->selectStreamByKey (
                    entry.stream));
        auto* button =
            getIdentityTestEnableButton (
                *splitters[
                     static_cast<size_t> (
                         entry.pane)]
                     ->lfpDisplay
                     ->channelInfo[
                         entry.channel]);
        ASSERT_NE (button, nullptr);
        auto handler =
            createIdentityTestEnableButtonHandler (
                *button);
        ASSERT_NE (handler, nullptr);

        const auto oneBasedPane =
            String (entry.pane + 1);
        const auto expectedTitle =
            "LFP display "
            + oneBasedPane
            + " stream \""
            + entry.stream
            + "\" channel \""
            + entry.channelName
            + "\" waveform visibility";
        const auto expectedDescription =
            "Waveform visibility for channel \""
            + entry.channelName
            + "\" ("
            + entry.stableKeyText
            + ") in stream \""
            + entry.stream
            + "\" of LFP display "
            + oneBasedPane
            + ".";
        const auto expectedHelp =
            "Show or hide drawing for channel \""
            + entry.channelName
            + "\" ("
            + entry.stableKeyText
            + ") in stream \""
            + entry.stream
            + "\" of LFP display "
            + oneBasedPane
            + ". Hiding stops waveform drawing only; it does not disable acquisition, buffering, hardware, or recording selection.";

        EXPECT_EQ (
            button->getComponentID(),
            entry.automationId);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::toggleButton);
        EXPECT_EQ (
            handler->getTitle(),
            expectedTitle);
        EXPECT_EQ (
            handler->getDescription(),
            expectedDescription);
        EXPECT_EQ (
            handler->getHelp(),
            expectedHelp);
        EXPECT_TRUE (
            handler->getCurrentState()
                .isCheckable());
        EXPECT_TRUE (
            handler->getCurrentState()
                .isChecked());
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::toggle));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::press));
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_TRUE (
            handler->getValueInterface()
                ->isReadOnly());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            "Visible");
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityGenerationRetiresOldHandlersAndKeepsHiddenActionable)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    auto* button =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (button, nullptr);
    auto visibleHandler =
        createIdentityTestEnableButtonHandler (
            *button);
    ASSERT_NE (visibleHandler, nullptr);
    const auto retiredVisibleActions =
        visibleHandler->getActions();
    ASSERT_TRUE (
        retiredVisibleActions.invoke (
            AccessibilityActionType::toggle));
    ASSERT_FALSE (
        display->getStoredChannelVisibility (
            0));
    EXPECT_FALSE (
        visibleHandler->isEnabled());
    EXPECT_TRUE (
        retiredVisibleActions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_FALSE (
        display->getStoredChannelVisibility (
            0));

    auto hiddenHandler =
        createIdentityTestEnableButtonHandler (
            *button);
    ASSERT_NE (hiddenHandler, nullptr);
    EXPECT_TRUE (
        hiddenHandler->isEnabled());
    EXPECT_TRUE (
        hiddenHandler->getCurrentState()
            .isCheckable());
    EXPECT_FALSE (
        hiddenHandler->getCurrentState()
            .isChecked());
    ASSERT_NE (
        hiddenHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        hiddenHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Hidden");

    display->setEnabledState (
        false,
        0,
        true);
    EXPECT_TRUE (
        hiddenHandler->isEnabled());
    const auto retiredHiddenActions =
        hiddenHandler->getActions();
    ASSERT_TRUE (
        retiredHiddenActions.invoke (
            AccessibilityActionType::toggle));
    ASSERT_TRUE (
        display->getStoredChannelVisibility (
            0));
    EXPECT_FALSE (
        hiddenHandler->isEnabled());
    EXPECT_TRUE (
        retiredHiddenActions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_TRUE (
        display->getStoredChannelVisibility (
            0));

    auto restoredHandler =
        createIdentityTestEnableButtonHandler (
            *button);
    ASSERT_NE (restoredHandler, nullptr);
    EXPECT_TRUE (
        restoredHandler->isEnabled());
    EXPECT_TRUE (
        restoredHandler->getCurrentState()
            .isChecked());
    ASSERT_NE (
        restoredHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        restoredHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Visible");
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityTracksProgrammaticAndStructuralAvailabilityChanges)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    auto* info =
        display->channelInfo[0];
    ASSERT_NE (info, nullptr);
    auto* button =
        getIdentityTestEnableButton (
            *info);
    ASSERT_NE (button, nullptr);

    const auto id =
        button->getComponentID();
    ASSERT_FALSE (id.isEmpty());
    auto visibleHandler =
        createIdentityTestEnableButtonHandler (
            *button);
    ASSERT_NE (visibleHandler, nullptr);

    display->setEnabledState (
        false,
        0,
        true);
    EXPECT_FALSE (
        display->getStoredChannelVisibility (
            0));
    EXPECT_FALSE (
        visibleHandler->isEnabled());
    EXPECT_EQ (
        button->getComponentID(),
        id);
    auto hiddenHandler =
        createIdentityTestEnableButtonHandler (
            *button);
    ASSERT_NE (hiddenHandler, nullptr);
    EXPECT_TRUE (
        hiddenHandler->isEnabled());
    ASSERT_NE (
        hiddenHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        hiddenHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Hidden");

    display->setEnabledState (
        false,
        0,
        true);
    EXPECT_TRUE (
        hiddenHandler->isEnabled());

    info->setSize (
        info->getWidth(),
        15);
    EXPECT_FALSE (
        button->isVisible());
    EXPECT_TRUE (
        button->getComponentID().isEmpty());
    EXPECT_FALSE (
        hiddenHandler->isEnabled());
    EXPECT_EQ (
        createIdentityTestEnableButtonHandler (
            *button),
        nullptr);

    info->setSize (
        info->getWidth(),
        30);
    EXPECT_TRUE (
        button->isVisible());
    EXPECT_EQ (
        button->getComponentID(),
        id);
    auto restoredHiddenHandler =
        createIdentityTestEnableButtonHandler (
            *button);
    ASSERT_NE (restoredHiddenHandler, nullptr);
    EXPECT_TRUE (
        restoredHiddenHandler->isEnabled());
    ASSERT_NE (
        restoredHiddenHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        restoredHiddenHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Hidden");

    ASSERT_TRUE (
        restoredHiddenHandler->getActions()
            .invoke (
                AccessibilityActionType::toggle));
    EXPECT_TRUE (
        display->getStoredChannelVisibility (
            0));
    EXPECT_FALSE (
        restoredHiddenHandler->isEnabled());

}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilitySuppressesInvalidAndAmbiguousStableIdentitySurfaces)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_FALSE (buffers.isEmpty());
    buffers[0]->streamKey =
        "ambiguous.visibility.stream";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "",
        900,
        2,
        "Ambiguous first",
        ContinuousChannel::Type::AUX);
    configureIdentityPersistenceChannel (
        *buffers[0],
        1,
        "",
        900,
        2,
        "Ambiguous second",
        ContinuousChannel::Type::AUX);
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .uuid = Uuid::null();
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .uuid = Uuid::null();
    canvas->updateSettings();
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "ambiguous.visibility.stream"));

    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    ASSERT_GE (display->channelInfo.size(), 2);
    for (int channel = 0;
         channel < 2;
         ++channel)
    {
        auto* button =
            getIdentityTestEnableButton (
                *display->channelInfo[channel]);
        ASSERT_NE (button, nullptr);
        EXPECT_TRUE (
            button->getComponentID().isEmpty());
        EXPECT_EQ (
            createIdentityTestEnableButtonHandler (
                *button),
            nullptr);
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityRepublishesAfterXmlVisibilityRestore)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    display->setEnabledState (
        false,
        0,
        true);
    auto* hiddenButton =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (hiddenButton, nullptr);
    auto hiddenHandler =
        createIdentityTestEnableButtonHandler (
            *hiddenButton);
    ASSERT_NE (hiddenHandler, nullptr);
    ASSERT_NE (
        hiddenHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        hiddenHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Hidden");

    XmlElement savedRoot ("ROOT");
    canvas->saveCustomParametersToXml (
        &savedRoot);
    showAllIdentityTestChannels (
        *splitters[0]);
    auto* visibleButton =
        getIdentityTestEnableButton (
            *splitters[0]
                 ->lfpDisplay
                 ->channelInfo[0]);
    ASSERT_NE (visibleButton, nullptr);
    auto visibleHandler =
        createIdentityTestEnableButtonHandler (
            *visibleButton);
    ASSERT_NE (visibleHandler, nullptr);
    ASSERT_NE (
        visibleHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        visibleHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Visible");

    canvas->loadCustomParametersFromXml (
        &savedRoot);
    splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* restoredButton =
        getIdentityTestEnableButton (
            *splitters[0]
                 ->lfpDisplay
                 ->channelInfo[0]);
    ASSERT_NE (restoredButton, nullptr);
    auto restoredHandler =
        createIdentityTestEnableButtonHandler (
            *restoredButton);
    ASSERT_NE (restoredHandler, nullptr);
    ASSERT_NE (
        restoredHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        restoredHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Hidden");
    EXPECT_FALSE (
        visibleHandler->isEnabled());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityRetiresGenerationWhenProcessorNodeIdChanges)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    ASSERT_NE (display, nullptr);
    auto* button =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (button, nullptr);
    const auto oldNodeId =
        processor->getNodeId();
    const auto oldId =
        button->getComponentID();
    ASSERT_FALSE (oldId.isEmpty());
    auto oldHandler =
        createIdentityTestEnableButtonHandler (
            *button);
    ASSERT_NE (oldHandler, nullptr);
    const auto oldActions =
        oldHandler->getActions();

    processor->setNodeId (
        oldNodeId + 10000);
    display->setEnabledState (
        true,
        0,
        true);

    const auto newId =
        "oe.processor."
        + String (processor->getNodeId())
        + oldId.fromFirstOccurrenceOf (
              ".lfp.",
              true,
              false);
    EXPECT_EQ (
        button->getComponentID(),
        newId);
    EXPECT_FALSE (
        oldHandler->isEnabled());
    ASSERT_TRUE (
        oldActions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_TRUE (
        display->getStoredChannelVisibility (
            0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityRetiresRetainedHandlersForDirectComponentLifecycle)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    ASSERT_NE (display, nullptr);
    auto* info = display->channelInfo[0];
    ASSERT_NE (info, nullptr);
    auto* button =
        getIdentityTestEnableButton (*info);
    ASSERT_NE (button, nullptr);

    const auto requireFreshEnabledHandler =
        [&] ()
        {
            auto handler =
                createIdentityTestEnableButtonHandler (
                    *button);
            EXPECT_NE (handler, nullptr);
            if (handler != nullptr)
                EXPECT_TRUE (handler->isEnabled());
            return handler;
        };

    auto buttonVisibleHandler =
        requireFreshEnabledHandler();
    button->setVisible (false);
    EXPECT_FALSE (
        buttonVisibleHandler->isEnabled());
    button->setVisible (true);

    auto buttonEnabledHandler =
        requireFreshEnabledHandler();
    button->setEnabled (false);
    EXPECT_FALSE (
        buttonEnabledHandler->isEnabled());
    button->setEnabled (true);

    auto ownerVisibleHandler =
        requireFreshEnabledHandler();
    info->setVisible (false);
    EXPECT_FALSE (
        ownerVisibleHandler->isEnabled());
    info->setVisible (true);

    auto ownerEnabledHandler =
        requireFreshEnabledHandler();
    info->setEnabled (false);
    EXPECT_FALSE (
        ownerEnabledHandler->isEnabled());
    info->setEnabled (true);

    auto removedHandler =
        requireFreshEnabledHandler();
    info->removeChildComponent (button);
    EXPECT_FALSE (
        removedHandler->isEnabled());
    info->addAndMakeVisible (button);
    EXPECT_TRUE (
        requireFreshEnabledHandler()
            ->isEnabled());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityControlsRetireAcrossReorderSkipAndFocus)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display = splitters[0]->lfpDisplay.get();
    ASSERT_NE (display, nullptr);
    ASSERT_GE (display->channelInfo.size(), 2);
    auto* firstButton =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (firstButton, nullptr);
    auto reverseHandler =
        createIdentityTestEnableButtonHandler (*firstButton);
    ASSERT_NE (reverseHandler, nullptr);

    display->setChannelsReversed (true);
    EXPECT_FALSE (reverseHandler->isEnabled());
    auto sortHandler =
        createIdentityTestEnableButtonHandler (*firstButton);
    ASSERT_NE (sortHandler, nullptr);
    EXPECT_TRUE (sortHandler->isEnabled());

    display->orderChannelsByDepth (true);
    EXPECT_FALSE (sortHandler->isEnabled());
    auto* secondButton =
        getIdentityTestEnableButton (
            *display->channelInfo[1]);
    ASSERT_NE (secondButton, nullptr);
    auto skipHandler =
        createIdentityTestEnableButtonHandler (*secondButton);
    ASSERT_NE (skipHandler, nullptr);

    display->setChannelDisplaySkipAmount (2);
    EXPECT_FALSE (skipHandler->isEnabled());
    EXPECT_TRUE (secondButton->getComponentID().isEmpty());
    display->setChannelDisplaySkipAmount (0);

    auto focusHandler =
        createIdentityTestEnableButtonHandler (*secondButton);
    ASSERT_NE (focusHandler, nullptr);
    ASSERT_GT (display->drawableChannels.size(), 0);
    display->toggleSingleChannel (display->drawableChannels[0]);
    EXPECT_FALSE (focusHandler->isEnabled());
    display->toggleSingleChannel (display->drawableChannels[0]);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityControlsRetireAcrossRebindStreamRemovalAndZeroChannels)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display = splitter->lfpDisplay.get();
    ASSERT_NE (display, nullptr);
    const auto buffers = processor->getDisplayBuffers();
    ASSERT_GE (buffers.size(), 2);
    ASSERT_TRUE (
        splitter->selectStreamByKey (buffers[0]->streamKey));
    auto* firstButton =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (firstButton, nullptr);
    auto rebindHandler =
        createIdentityTestEnableButtonHandler (*firstButton);
    ASSERT_NE (rebindHandler, nullptr);

    buffers[0]->channelMetadata
        .getReference (0)
        .uuid = Uuid();
    ASSERT_TRUE (
        splitter->selectStreamByKey (buffers[0]->streamKey));
    EXPECT_FALSE (rebindHandler->isEnabled());
    auto streamHandler =
        createIdentityTestEnableButtonHandler (*firstButton);
    ASSERT_NE (streamHandler, nullptr);

    const auto staleActions = streamHandler->getActions();
    std::atomic<bool> workerStarted { false };
    std::atomic<bool> workerFinished { false };
    std::thread worker (
        [&]
        {
            workerStarted.store (true);
            staleActions.invoke (AccessibilityActionType::toggle);
            workerFinished.store (true);
        });
    for (int attempt = 0;
         attempt < 100 && ! workerStarted.load();
         ++attempt)
    {
        Thread::sleep (1);
    }
    ASSERT_TRUE (workerStarted.load());
    ASSERT_TRUE (
        splitter->selectStreamByKey (buffers[1]->streamKey));
    for (int attempt = 0;
         attempt < 100 && ! workerFinished.load();
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
    worker.join();
    EXPECT_TRUE (workerFinished.load());
    EXPECT_TRUE (display->getStoredChannelVisibility (0));
    EXPECT_FALSE (streamHandler->isEnabled());

    auto* secondButton =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (secondButton, nullptr);
    auto removalHandler =
        createIdentityTestEnableButtonHandler (*secondButton);
    ASSERT_NE (removalHandler, nullptr);
    canvas->removeBufferForDisplay (
        splitter->splitID,
        buffers[1]);
    EXPECT_FALSE (removalHandler->isEnabled());

    display->setNumChannels (0);
    EXPECT_EQ (display->getNumChannels(), 0);
    EXPECT_FALSE (removalHandler->isEnabled());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityRetainedHandlerIsDisabledAfterCanvasDestruction)
{
    std::unique_ptr<AccessibilityHandler> retainedHandler;
    AccessibilityActions retainedActions;
    {
        auto canvas =
            createIdentityCanvas (
                SplitLayouts::SINGLE);
        canvas->addToDesktop (0);
        canvas->setVisible (true);
        canvas->updateSettings();
        const auto splitters =
            getIdentityTestSplitters (*canvas);
        ASSERT_FALSE (splitters.empty());
        auto* button =
            getIdentityTestEnableButton (
                *splitters[0]
                     ->lfpDisplay
                     ->channelInfo[0]);
        ASSERT_NE (button, nullptr);
        retainedHandler =
            createIdentityTestEnableButtonHandler (*button);
        ASSERT_NE (retainedHandler, nullptr);
        retainedActions = retainedHandler->getActions();
    }

    EXPECT_FALSE (retainedHandler->isEnabled());
    EXPECT_TRUE (
        retainedActions.invoke (
            AccessibilityActionType::toggle));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WaveformVisibilityControlChangesDrawingWithoutDataRecordingFocusOrAudioEffects)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->updateSettings();
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display = splitter->lfpDisplay.get();
    ASSERT_NE (display, nullptr);
    auto* button =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (button, nullptr);
    auto handler =
        createIdentityTestEnableButtonHandler (*button);
    ASSERT_NE (handler, nullptr);
    const auto actions = handler->getActions();
    auto* buffer = splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    auto* stream =
        processor->getDataStream (
            splitter->selectedStreamId);
    ASSERT_NE (stream, nullptr);
    auto* messageCenter =
        tester->processorGraph->getMessageCenter();
    ASSERT_NE (messageCenter, nullptr);
    ASSERT_TRUE (
        drainIdentityTestBroadcastMessages (*messageCenter).isEmpty());

    const auto selectedStreamId = splitter->selectedStreamId;
    const auto selectedStreamKey = splitter->selectedStreamKey;
    const auto bufferDisplays = buffer->displays;
    const auto bufferChannelCount = buffer->numChannels;
    const auto processorEnabled = processor->isEnabled;
    const auto focused = display->getSingleChannelState();
    const auto paused = display->isPaused();
    std::vector<bool> recorded;
    for (const auto* channel : stream->getContinuousChannels())
    {
        ASSERT_NE (channel, nullptr);
        recorded.push_back (channel->isRecorded);
    }

    ASSERT_TRUE (
        actions.invoke (
            AccessibilityActionType::toggle));
    EXPECT_FALSE (display->getStoredChannelVisibility (0));
    EXPECT_EQ (splitter->displayBuffer, buffer);
    EXPECT_EQ (splitter->selectedStreamId, selectedStreamId);
    EXPECT_EQ (splitter->selectedStreamKey, selectedStreamKey);
    EXPECT_EQ (buffer->displays, bufferDisplays);
    EXPECT_EQ (buffer->numChannels, bufferChannelCount);
    EXPECT_EQ (processor->isEnabled, processorEnabled);
    EXPECT_EQ (display->getSingleChannelState(), focused);
    EXPECT_EQ (display->isPaused(), paused);
    const auto channelsAfter = stream->getContinuousChannels();
    ASSERT_EQ (
        channelsAfter.size(),
        static_cast<int> (recorded.size()));
    for (int index = 0; index < channelsAfter.size(); ++index)
    {
        EXPECT_EQ (
            channelsAfter[index]->isRecorded,
            recorded[static_cast<size_t> (index)]);
    }
    EXPECT_TRUE (
        drainIdentityTestBroadcastMessages (*messageCenter).isEmpty());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        PaneAndSelectedStreamAreBoundToDisplayAndInfo)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::TWO_VERT,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_GE (splitters.size(), 2u);
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    ASSERT_TRUE (
        splitters[0]->selectStreamByKey (
            buffers[0]->streamKey));
    ASSERT_TRUE (
        splitters[1]->selectStreamByKey (
            buffers[1]->streamKey));

    for (int pane = 0; pane < 2; ++pane)
    {
        auto* display =
            splitters[static_cast<size_t> (pane)]
                ->lfpDisplay.get();
        ASSERT_GT (display->channels.size(), 0);
        const auto channelIdentity =
            display->channels[0]
                ->getStableChannelIdentity();
        const auto infoIdentity =
            display->channelInfo[0]
                ->getStableChannelIdentity();
        ASSERT_NE (channelIdentity, nullptr);
        EXPECT_EQ (
            channelIdentity,
            infoIdentity);
        EXPECT_EQ (
            channelIdentity->getPaneIndex(),
            pane);
        EXPECT_EQ (
            channelIdentity->getStreamKey(),
            buffers[pane]->streamKey);
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        HiddenStateDoesNotRetargetSameLocalIndexAcrossStreams)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[0]->streamKey));
    ASSERT_TRUE (
        display->channels[0]
            ->getEnabledState());

    display->setEnabledState (
        false,
        0,
        true);
    ASSERT_FALSE (
        display->channels[0]
            ->getEnabledState());

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[1]->streamKey));

    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        FocusedStreamSwitchDoesNotRetargetOldChannelIndex)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[0]->streamKey));
    ASSERT_GE (
        display->drawableChannels.size(),
        2);
    display->toggleSingleChannel (
        display->drawableChannels[1]);
    ASSERT_TRUE (
        display->getSingleChannelState());
    ASSERT_EQ (
        display->getSingleChannelShown(),
        1);

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[1]->streamKey));

    EXPECT_FALSE (
        display->getSingleChannelState());
    EXPECT_GT (
        display->drawableChannels.size(),
        1);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        HiddenStateIsIsolatedByPaneAndChannel)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::TWO_VERT,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_GE (splitters.size(), 2u);
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_FALSE (buffers.isEmpty());
    for (int pane = 0; pane < 2; ++pane)
    {
        ASSERT_TRUE (
            splitters[
                static_cast<size_t> (
                    pane)]
                ->selectStreamByKey (
                    buffers[0]
                        ->streamKey));
    }
    auto* first =
        splitters[0]
            ->lfpDisplay.get();
    auto* second =
        splitters[1]
            ->lfpDisplay.get();
    ASSERT_GE (first->channels.size(), 2);
    ASSERT_GE (second->channels.size(), 2);

    first->setEnabledState (
        false,
        0,
        true);

    EXPECT_FALSE (
        first->channels[0]
            ->getEnabledState());
    EXPECT_TRUE (
        first->channels[1]
            ->getEnabledState());
    EXPECT_TRUE (
        second->channels[0]
            ->getEnabledState());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        HiddenStateFollowsStableIdentityThroughAllDrawableReordering)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer =
        splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    ASSERT_GE (
        buffer->channelMetadata.size(),
        2);
    const auto hiddenIdentifier =
        buffer->channelMetadata[0]
            .identifier;
    display->setEnabledState (
        false,
        0,
        true);

    std::swap (
        buffer->channelMetadata
            .getReference (0),
        buffer->channelMetadata
            .getReference (1));
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));
    ASSERT_EQ (
        buffer->channelMetadata[1]
            .identifier,
        hiddenIdentifier);
    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_FALSE (
        display->channels[1]
            ->getEnabledState());

    display->setChannelsReversed (
        true);
    display->orderChannelsByDepth (
        true);
    display->setChannelDisplaySkipAmount (
        2);
    display->setChannelDisplaySkipAmount (
        0);

    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                0));
    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                1));
    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_FALSE (
        display->channels[1]
            ->getEnabledState());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        FocusTemporarilyShowsHiddenTargetWithoutChangingStoredVisibility)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    ASSERT_GE (
        display->drawableChannels.size(),
        2);
    const auto target =
        display->drawableChannels[1];
    const auto targetIndex =
        display->channels
            .indexOf (
                target.channel);
    ASSERT_GE (targetIndex, 0);
    display->setEnabledState (
        false,
        targetIndex,
        true);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                targetIndex));

    display->toggleSingleChannel (
        target);

    EXPECT_TRUE (
        target.channel
            ->getEnabledState());
    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                targetIndex));

    display->toggleSingleChannel (
        display->drawableChannels[0]);

    EXPECT_FALSE (
        target.channel
            ->getEnabledState());
    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                targetIndex));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        InvalidIdentityHideDefaultsVisibleAndBadBoundsAreNoOps)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer =
        splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    ASSERT_GE (
        buffer->channelMetadata.size(),
        2);
    buffer->channelMetadata
        .getReference (1)
        .uuid =
        buffer->channelMetadata[0]
            .uuid;
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));
    ASSERT_FALSE (
        display->channels[0]
            ->getStableChannelIdentity()
            ->isAgentActionable());

    display->setEnabledState (
        false,
        0,
        true);
    display->setEnabledState (
        false,
        -1,
        true);
    display->setEnabledState (
        false,
        display->channels.size(),
        true);

    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                0));
    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                -1));
    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                display->channels
                    .size()));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        RemovedAndReaddedChannelDoesNotInheritHiddenState)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer =
        splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    ASSERT_GE (
        buffer->channelMetadata.size(),
        2);
    const int removedIndex =
        buffer->channelMetadata.size()
        - 1;
    auto removedMetadata =
        buffer->channelMetadata[
            removedIndex];
    display->setEnabledState (
        false,
        removedIndex,
        true);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                removedIndex));

    buffer->channelMetadata
        .remove (
            removedIndex);
    buffer->numChannels =
        buffer->channelMetadata.size();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));
    removedMetadata.uuid = Uuid();
    buffer->channelMetadata.add (
        removedMetadata);
    buffer->numChannels =
        buffer->channelMetadata.size();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));

    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                removedIndex));
    EXPECT_TRUE (
        display->channels[
            removedIndex]
            ->getEnabledState());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ZeroChannelsClearsOnlyTheCurrentStreamsHiddenState)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[0]->streamKey));
    display->setEnabledState (
        false,
        0,
        true);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[1]->streamKey));
    display->setEnabledState (
        false,
        1,
        true);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                1));
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[0]->streamKey));
    display->setNumChannels (
        0);
    ASSERT_EQ (
        display->channels.size(),
        0);
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .uuid = Uuid();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[0]->streamKey));

    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                0));
    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[1]->streamKey));
    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                1));
    EXPECT_FALSE (
        display->channels[1]
            ->getEnabledState());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        RemovedAndReaddedStreamDoesNotInheritHiddenState)
{
    auto dynamicTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    30000.0f,
                    0.195f,
                    2 }),
            TestGuiRuntimeLifetime::process);
    auto* dynamicProcessor =
        dynamicTester
            ->createProcessor<LfpDisplayNode> (
                Plugin::Processor::SINK);
    dynamicProcessor->setHeadlessMode (
        false);
    dynamicProcessor->setProcessorType (
        Plugin::Processor::SPLITTER);
    auto* editor =
        static_cast<LfpDisplayEditor*> (
            dynamicProcessor
                ->createEditor());
    dynamicProcessor->setProcessorType (
        Plugin::Processor::SINK);
    ASSERT_NE (editor, nullptr);
    dynamicProcessor->setHeadlessMode (
        true);
    editor->canvas.reset (
        editor->createNewCanvas());
    auto* canvas =
        dynamic_cast<LfpDisplayCanvas*> (
            editor->canvas.get());
    ASSERT_NE (canvas, nullptr);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    const auto initialBuffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (
        initialBuffers.size(),
        2);
    auto* removedBuffer =
        initialBuffers[1];
    const auto removedStreamKey =
        removedBuffer->streamKey;
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            removedStreamKey));
    auto* display =
        splitter->lfpDisplay.get();
    display->setEnabledState (
        false,
        0,
        true);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));
    auto* source =
        dynamic_cast<FakeSourceNode*> (
            dynamicTester
                ->getSourceNode());
    ASSERT_NE (source, nullptr);

    source
        ->setStreamCountPreservingExisting (
            1,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    source
        ->setStreamCountPreservingExisting (
            2,
            4);
    dynamicTester
        ->updateSourceNodeSettings();

    const auto replacementBuffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (
        replacementBuffers.size(),
        2);
    ASSERT_EQ (
        replacementBuffers[1]
            ->streamKey,
        removedStreamKey);
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            removedStreamKey));
    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                0));
    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());

    editor->canvas.reset();
    dynamicProcessor
        ->setHeadlessMode (
            true);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        AmbiguousZeroChannelTransitionPreservesAStillAvailableUniqueStream)
{
    auto dynamicTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    30000.0f,
                    0.195f,
                    3 }),
            TestGuiRuntimeLifetime::process);
    auto* dynamicProcessor =
        dynamicTester
            ->createProcessor<LfpDisplayNode> (
                Plugin::Processor::SINK);
    dynamicProcessor->setHeadlessMode (
        false);
    dynamicProcessor->setProcessorType (
        Plugin::Processor::SPLITTER);
    auto* editor =
        static_cast<LfpDisplayEditor*> (
            dynamicProcessor
                ->createEditor());
    dynamicProcessor->setProcessorType (
        Plugin::Processor::SINK);
    ASSERT_NE (editor, nullptr);
    dynamicProcessor->setHeadlessMode (
        true);
    editor->canvas.reset (
        editor->createNewCanvas());
    auto* canvas =
        dynamic_cast<LfpDisplayCanvas*> (
            editor->canvas.get());
    ASSERT_NE (canvas, nullptr);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    const auto initialBuffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (
        initialBuffers.size(),
        3);
    const auto firstStreamKey =
        initialBuffers[0]
            ->streamKey;
    const auto secondStreamKey =
        initialBuffers[1]
            ->streamKey;
    const auto uniqueStreamKey =
        initialBuffers[2]
            ->streamKey;
    ASSERT_NE (
        firstStreamKey,
        secondStreamKey);
    ASSERT_NE (
        firstStreamKey,
        uniqueStreamKey);
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            uniqueStreamKey));
    display->setEnabledState (
        false,
        0,
        true);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));

    initialBuffers[1]
        ->streamKey =
        firstStreamKey;
    canvas->updateSettings();
    ASSERT_EQ (
        splitter->displayBuffer,
        nullptr);
    ASSERT_TRUE (
        splitter
            ->selectedStreamKey
            .isEmpty());
    ASSERT_EQ (
        display->getNumChannels(),
        0);

    initialBuffers[1]
        ->streamKey =
        secondStreamKey;
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            uniqueStreamKey));

    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));
    EXPECT_FALSE (
        display->channels[0]
            ->getEnabledState());

    auto* source =
        dynamic_cast<FakeSourceNode*> (
            dynamicTester
                ->getSourceNode());
    ASSERT_NE (source, nullptr);
    source
        ->setStreamCountPreservingExisting (
            2,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    source
        ->setStreamCountPreservingExisting (
            3,
            4);
    dynamicTester
        ->updateSourceNodeSettings();

    const auto replacementBuffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (
        replacementBuffers.size(),
        3);
    ASSERT_EQ (
        replacementBuffers[2]
            ->streamKey,
        uniqueStreamKey);
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            uniqueStreamKey));
    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                0));
    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());

    editor->canvas.reset();
    dynamicProcessor
        ->setHeadlessMode (
            true);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        SettingsReplacementAndRemovalInvalidateOldBindings)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display = splitter->lfpDisplay.get();
    ASSERT_GT (display->channels.size(), 0);
    auto* channelComponent = display->channels[0];
    auto* infoComponent = display->channelInfo[0];
    const auto oldIdentity =
        channelComponent
            ->getStableChannelIdentity();
    ASSERT_NE (oldIdentity, nullptr);
    auto* buffer = splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);

    const auto replacementUuid = Uuid();
    buffer->channelMetadata
        .getReference (0)
        .identifier =
        "replacement.identifier";
    buffer->channelMetadata
        .getReference (0)
        .uuid = replacementUuid;
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));

    EXPECT_EQ (
        display->channels[0],
        channelComponent);
    EXPECT_EQ (
        display->channelInfo[0],
        infoComponent);
    const auto replacementIdentity =
        channelComponent
            ->getStableChannelIdentity();
    ASSERT_NE (replacementIdentity, nullptr);
    EXPECT_FALSE (
        oldIdentity
            ->isAgentActionable());
    EXPECT_NE (
        replacementIdentity,
        oldIdentity);
    EXPECT_EQ (
        replacementIdentity,
        infoComponent
            ->getStableChannelIdentity());
    EXPECT_EQ (
        oldIdentity
            ->getStableChannelKey()
            ->getIdentifier(),
        "identifier.g0.s0.c0");
    EXPECT_EQ (
        replacementIdentity
            ->getStableChannelKey()
            ->getIdentifier(),
        "replacement.identifier");
    EXPECT_EQ (
        replacementIdentity
            ->getRuntimeUuid(),
        replacementUuid);

    canvas->removeBufferForDisplay (
        splitter->splitID,
        buffer);
    EXPECT_FALSE (
        replacementIdentity
            ->isAgentActionable());
    EXPECT_EQ (
        channelComponent
            ->getStableChannelIdentity(),
        nullptr);
    EXPECT_EQ (
        infoComponent
            ->getStableChannelIdentity(),
        nullptr);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        InvalidMetadataBindsANonActionableSharedSnapshot)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* buffer = splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    ASSERT_GE (buffer->channelMetadata.size(), 2);
    buffer->channelMetadata
        .getReference (1)
        .uuid =
        buffer->channelMetadata[0].uuid;
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));

    const auto channelIdentity =
        splitter->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    const auto infoIdentity =
        splitter->lfpDisplay
            ->channelInfo[0]
            ->getStableChannelIdentity();
    ASSERT_NE (channelIdentity, nullptr);
    EXPECT_EQ (
        channelIdentity,
        infoIdentity);
    EXPECT_FALSE (
        channelIdentity
            ->isAgentActionable());
    EXPECT_EQ (
        channelIdentity
            ->getStableChannelKey(),
        nullptr);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ZeroChannelStateReleasesBoundSnapshots)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    ASSERT_GT (display->channels.size(), 0);
    std::shared_ptr<
        const LfpStableChannelIdentity>
        oldIdentity =
            display->channels[0]
                ->getStableChannelIdentity();
    ASSERT_NE (oldIdentity, nullptr);
    ASSERT_TRUE (
        oldIdentity
            ->isAgentActionable());

    display->setNumChannels (0);

    EXPECT_FALSE (
        oldIdentity
            ->isAgentActionable());
    EXPECT_EQ (display->channels.size(), 0);
    EXPECT_EQ (display->channelInfo.size(), 0);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        DrawableReorderingAndFocusPreserveBoundSnapshots)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display = splitter->lfpDisplay.get();
    ASSERT_GE (display->channels.size(), 2);
    const auto first =
        display->channels[0]
            ->getStableChannelIdentity();
    const auto second =
        display->channels[1]
            ->getStableChannelIdentity();
    ASSERT_NE (first, nullptr);
    ASSERT_NE (second, nullptr);

    display->setChannelsReversed (true);
    display->orderChannelsByDepth (true);
    splitter->select();

    EXPECT_EQ (
        display->channels[0]
            ->getStableChannelIdentity(),
        first);
    EXPECT_EQ (
        display->channelInfo[0]
            ->getStableChannelIdentity(),
        first);
    EXPECT_EQ (
        display->channels[1]
            ->getStableChannelIdentity(),
        second);
    EXPECT_EQ (
        display->channelInfo[1]
            ->getStableChannelIdentity(),
        second);

    display->setChannelDisplaySkipAmount (2);
    EXPECT_FALSE (
        second
            ->isAgentActionable());
    display->setChannelDisplaySkipAmount (0);
    const auto restoredSecond =
        display->channels[1]
            ->getStableChannelIdentity();
    ASSERT_NE (restoredSecond, nullptr);
    EXPECT_NE (restoredSecond, second);
    EXPECT_EQ (
        restoredSecond,
        display->channelInfo[1]
            ->getStableChannelIdentity());
    EXPECT_TRUE (
        restoredSecond
            ->isAgentActionable());

    ASSERT_FALSE (
        display->drawableChannels
            .isEmpty());
    display->toggleSingleChannel (
        display->drawableChannels[0]);
    EXPECT_FALSE (
        restoredSecond
            ->isAgentActionable());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        RetainedSnapshotIsRevokedWhenRebindingToAnExistingStream)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::TWO_VERT,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_GE (splitters.size(), 2u);
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    auto* splitter = splitters[0];
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[0]->streamKey));
    auto* channel =
        splitter->lfpDisplay
            ->channels[0];
    const auto componentBefore =
        channel;
    const auto retained =
        channel
            ->getStableChannelIdentity();
    ASSERT_NE (retained, nullptr);
    ASSERT_TRUE (
        retained
            ->isAgentActionable());
    const auto originalUuid =
        retained->getRuntimeUuid();

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[1]->streamKey));

    EXPECT_EQ (
        splitter->lfpDisplay
            ->channels[0],
        componentBefore);
    EXPECT_EQ (
        buffers[0]
            ->channelMetadata[0]
            .uuid,
        originalUuid);
    EXPECT_FALSE (
        retained
            ->isAgentActionable());
    const auto rebound =
        channel
            ->getStableChannelIdentity();
    ASSERT_NE (rebound, nullptr);
    EXPECT_NE (rebound, retained);
    EXPECT_TRUE (
        rebound
            ->isAgentActionable());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ThirdPaneHideAndShowRevokesAndReplacesSnapshot)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::THREE_VERT,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3u);
    auto* third = splitters[2];
    const auto retained =
        third->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    ASSERT_NE (retained, nullptr);
    ASSERT_TRUE (
        retained
            ->isAgentActionable());

    canvas->setLayout (
        SplitLayouts::TWO_VERT);

    EXPECT_FALSE (
        retained
            ->isAgentActionable());

    canvas->setLayout (
        SplitLayouts::THREE_VERT);
    const auto restored =
        third->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    ASSERT_NE (restored, nullptr);
    EXPECT_NE (restored, retained);
    EXPECT_TRUE (
        restored
            ->isAgentActionable());
    EXPECT_FALSE (
        retained
            ->isAgentActionable());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ChannelAndInfoShareRevocationAcrossHideAndDisable)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    auto* channel = display->channels[0];
    auto* info = display->channelInfo[0];
    const auto original =
        channel
            ->getStableChannelIdentity();
    ASSERT_EQ (
        original,
        info->getStableChannelIdentity());
    ASSERT_TRUE (
        original
            ->isAgentActionable());

    channel->setEnabled (false);
    EXPECT_FALSE (
        original
            ->isAgentActionable());
    EXPECT_EQ (
        channel
            ->getStableChannelIdentity(),
        info
            ->getStableChannelIdentity());

    channel->setEnabled (true);
    const auto afterEnable =
        channel
            ->getStableChannelIdentity();
    ASSERT_NE (afterEnable, nullptr);
    EXPECT_NE (afterEnable, original);
    EXPECT_EQ (
        afterEnable,
        info
            ->getStableChannelIdentity());
    EXPECT_TRUE (
        afterEnable
            ->isAgentActionable());

    info->setVisible (false);
    EXPECT_FALSE (
        afterEnable
            ->isAgentActionable());
    info->setVisible (true);
    const auto afterShow =
        channel
            ->getStableChannelIdentity();
    ASSERT_NE (afterShow, nullptr);
    EXPECT_NE (afterShow, afterEnable);
    EXPECT_EQ (
        afterShow,
        info
            ->getStableChannelIdentity());
    EXPECT_TRUE (
        afterShow
            ->isAgentActionable());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ShrinkAndCanvasDestructionRevokeRetainedSnapshots)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display = splitter->lfpDisplay.get();
    ASSERT_GE (display->channels.size(), 4);
    const auto removed =
        display->channels[3]
            ->getStableChannelIdentity();
    ASSERT_TRUE (
        removed
            ->isAgentActionable());

    display->setNumChannels (2);
    EXPECT_FALSE (
        removed
            ->isAgentActionable());

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            splitter->displayBuffer
                ->streamKey));
    const auto retainedForDestruction =
        display->channels[0]
            ->getStableChannelIdentity();
    ASSERT_TRUE (
        retainedForDestruction
            ->isAgentActionable());

    canvas.reset();
    EXPECT_FALSE (
        retainedForDestruction
            ->isAgentActionable());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ConcurrentReaderCannotObserveRevokedSnapshotRevive)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::TWO_VERT,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    auto* splitter = splitters[0];
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffers[0]->streamKey));
    const auto retained =
        splitter->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    ASSERT_TRUE (
        retained
            ->isAgentActionable());

    std::atomic<bool> readerStarted {
        false
    };
    std::atomic<bool> rebindComplete {
        false
    };
    std::atomic<bool> sawRevoked {
        false
    };
    std::atomic<bool> sawRevival {
        false
    };
    std::thread reader (
        [&]
        {
            readerStarted.store (
                true);
            bool revoked = false;
            while (! rebindComplete
                         .load())
            {
                if (! retained
                           ->isAgentActionable())
                {
                    revoked = true;
                }
            }
            for (int check = 0;
                 check < 10000;
                 ++check)
            {
                const bool actionable =
                    retained
                        ->isAgentActionable();
                if (! actionable)
                    revoked = true;
                else if (revoked)
                    sawRevival.store (
                        true);
            }
            sawRevoked.store (
                revoked);
        });
    while (! readerStarted.load())
        std::this_thread::yield();

    EXPECT_TRUE (
        splitter->selectStreamByKey (
            buffers[1]->streamKey));
    rebindComplete.store (
        true);
    reader.join();

    EXPECT_TRUE (
        sawRevoked.load());
    EXPECT_FALSE (
        sawRevival.load());
    EXPECT_FALSE (
        retained
            ->isAgentActionable());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        HiddenPaneBindingNeverPublishesAnActiveGeneration)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::THREE_VERT,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3u);
    auto* third = splitters[2];
    canvas->setLayout (
        SplitLayouts::TWO_VERT);
    ASSERT_FALSE (third->isVisible());

    bool observedPairPublication =
        false;
    bool publishedActiveWhileHidden =
        false;
    third->lfpDisplay
        ->setStableIdentityLifecycleTestHook (
            [&] (
                LfpDisplay::
                    StableIdentityLifecycleTestPhase
                        phase,
                int channelIndex)
            {
                if (phase
                        == LfpDisplay::
                               StableIdentityLifecycleTestPhase::
                                   afterPairPublication
                    && channelIndex == 0)
                {
                    observedPairPublication =
                        true;
                    const auto identity =
                        third->lfpDisplay
                            ->channels[0]
                            ->getStableChannelIdentity();
                    const auto request =
                        third->lfpDisplay
                            ->channels[0]
                            ->getStableChannelActionRequest();
                    publishedActiveWhileHidden =
                        identity != nullptr
                        && identity
                               ->isAgentActionable()
                        || request != nullptr;
                }
            });

    canvas->updateSettings();

    EXPECT_TRUE (
        observedPairPublication);
    EXPECT_FALSE (
        publishedActiveWhileHidden);
    EXPECT_EQ (
        third->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity(),
        nullptr);
    EXPECT_EQ (
        third->lfpDisplay
            ->channels[0]
            ->getStableChannelActionRequest(),
        nullptr);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        PairPublicationHasNoOneSidedObservableState)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer = splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);

    bool observedPublicationBoundary =
        false;
    bool pairWasConsistent =
        true;
    display
        ->setStableIdentityLifecycleTestHook (
            [&] (
                LfpDisplay::
                    StableIdentityLifecycleTestPhase
                        phase,
                int channelIndex)
            {
                if (phase
                        == LfpDisplay::
                               StableIdentityLifecycleTestPhase::
                                   afterChannelPublicationBeforeInfo
                    && channelIndex == 0)
                {
                    observedPublicationBoundary =
                        true;
                    std::shared_ptr<
                        const LfpStableChannelIdentity>
                        channelSnapshot;
                    std::shared_ptr<
                        const LfpStableChannelIdentity>
                        infoSnapshot;
                    std::shared_ptr<
                        const LfpStableChannelActionRequest>
                        channelRequest;
                    std::shared_ptr<
                        const LfpStableChannelActionRequest>
                        infoRequest;
                    std::thread worker (
                        [&]
                        {
                            channelSnapshot =
                                display->channels[0]
                                    ->getStableChannelIdentity();
                            infoSnapshot =
                                display->channelInfo[0]
                                    ->getStableChannelIdentity();
                            channelRequest =
                                display->channels[0]
                                    ->getStableChannelActionRequest();
                            infoRequest =
                                display->channelInfo[0]
                                    ->getStableChannelActionRequest();
                        });
                    worker.join();
                    pairWasConsistent =
                        channelSnapshot
                            == infoSnapshot
                        && channelRequest
                               == infoRequest;
                }
            });
    buffer->channelMetadata
        .getReference (0)
        .uuid = Uuid();

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));

    EXPECT_TRUE (
        observedPublicationBoundary);
    EXPECT_TRUE (
        pairWasConsistent);
    EXPECT_EQ (
        display->channels[0]
            ->getStableChannelIdentity(),
        display->channelInfo[0]
            ->getStableChannelIdentity());
    EXPECT_EQ (
        display->channels[0]
            ->getStableChannelActionRequest(),
        display->channelInfo[0]
            ->getStableChannelActionRequest());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        SkipRevokesOutgoingGenerationBeforeHierarchyMutation)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    ASSERT_GE (display->channels.size(), 2);
    auto* outgoing =
        display->channels[1];
    const auto retained =
        outgoing
            ->getStableChannelIdentity();
    ASSERT_TRUE (
        retained
            ->isAgentActionable());

    bool observedPreMutationBoundary =
        false;
    bool wasRevokedBeforeMutation =
        false;
    bool hierarchyWasIntact =
        false;
    display
        ->setStableIdentityLifecycleTestHook (
            [&] (
                LfpDisplay::
                    StableIdentityLifecycleTestPhase
                        phase,
                int)
            {
                if (phase
                    == LfpDisplay::
                           StableIdentityLifecycleTestPhase::
                               beforeDrawableHierarchyMutation)
                {
                    observedPreMutationBoundary =
                        true;
                    wasRevokedBeforeMutation =
                        ! retained
                              ->isAgentActionable();
                    hierarchyWasIntact =
                        outgoing
                            ->getParentComponent()
                        == display;
                }
            });

    display
        ->setChannelDisplaySkipAmount (
            2);

    EXPECT_TRUE (
        observedPreMutationBoundary);
    EXPECT_TRUE (
        wasRevokedBeforeMutation);
    EXPECT_TRUE (
        hierarchyWasIntact);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        PaneGenerationIsRevokedBeforeVisibilityMutation)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::THREE_VERT,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3u);
    auto* third = splitters[2];
    const auto retained =
        third->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    ASSERT_TRUE (
        retained
            ->isAgentActionable());

    bool observedBoundary = false;
    bool revokedBeforeVisibility =
        false;
    bool paneWasStillVisible =
        false;
    third->lfpDisplay
        ->setStableIdentityLifecycleTestHook (
            [&] (
                LfpDisplay::
                    StableIdentityLifecycleTestPhase
                        phase,
                int)
            {
                if (phase
                    == LfpDisplay::
                           StableIdentityLifecycleTestPhase::
                               beforePaneVisibilityMutation)
                {
                    observedBoundary = true;
                    revokedBeforeVisibility =
                        ! retained
                              ->isAgentActionable();
                    paneWasStillVisible =
                        third->isVisible();
                }
            });

    canvas->setLayout (
        SplitLayouts::TWO_VERT);

    EXPECT_TRUE (observedBoundary);
    EXPECT_TRUE (
        revokedBeforeVisibility);
    EXPECT_TRUE (
        paneWasStillVisible);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ChannelGenerationIsRevokedBeforeEnabledStateMutation)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    auto* channel =
        display->channels[0];
    const auto retained =
        channel
            ->getStableChannelIdentity();
    ASSERT_TRUE (
        retained
            ->isAgentActionable());

    bool observedBoundary = false;
    bool revokedBeforeState =
        false;
    bool channelWasStillEnabled =
        false;
    display
        ->setStableIdentityLifecycleTestHook (
            [&] (
                LfpDisplay::
                    StableIdentityLifecycleTestPhase
                        phase,
                int channelIndex)
            {
                if (phase
                        == LfpDisplay::
                               StableIdentityLifecycleTestPhase::
                                   beforeChannelStateMutation
                    && channelIndex == 0
                    && ! observedBoundary)
                {
                    observedBoundary = true;
                    revokedBeforeState =
                        ! retained
                              ->isAgentActionable();
                    channelWasStillEnabled =
                        channel
                            ->getEnabledState();
                }
            });

    display->setEnabledState (
        false,
        0,
        true);

    EXPECT_TRUE (observedBoundary);
    EXPECT_TRUE (
        revokedBeforeState);
    EXPECT_TRUE (
        channelWasStillEnabled);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        PublishedDiagnosticAllowsCurrentVisibleRequestAndRejectsSameStreamRebind)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer =
        splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    const auto retained =
        display->channels[0]
            ->getStableChannelIdentity();
    ASSERT_NE (retained, nullptr);
    ASSERT_TRUE (canvas->isShowing());
    ASSERT_TRUE (canvas->isEnabled());
    ASSERT_TRUE (splitter->isShowing());
    ASSERT_TRUE (splitter->isEnabled());
    ASSERT_TRUE (display->isShowing());
    ASSERT_TRUE (display->isEnabled());
    ASSERT_TRUE (
        display->channels[0]
            ->isShowing());
    ASSERT_TRUE (
        display->channels[0]
            ->Component::isEnabled());
    ASSERT_TRUE (
        display->channelInfo[0]
            ->isShowing());
    ASSERT_TRUE (
        display->channelInfo[0]
            ->Component::isEnabled());
    const auto request =
        display->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);

    EXPECT_TRUE (
        request
            ->validateCurrentAndAvailable());
    std::atomic<bool> workerFinished {
        false
    };
    bool workerResult = false;
    std::thread worker (
        [&,
         workerRequest = request]
        {
            workerResult =
                workerRequest
                    ->validateCurrentAndAvailable();
            workerFinished.store (
                true);
        });
    auto* messageManager =
        MessageManager::
            getInstanceWithoutCreating();
    ASSERT_NE (
        messageManager,
        nullptr);
    for (int dispatch = 0;
         dispatch < 1000
         && ! workerFinished.load();
         ++dispatch)
    {
        messageManager
            ->runDispatchLoopUntil (
                1);
    }
    ASSERT_TRUE (
        workerFinished.load());
    worker.join();
    EXPECT_TRUE (
        workerResult);

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));
    EXPECT_FALSE (
        request
            ->validateCurrentAndAvailable());
    EXPECT_FALSE (
        request
            ->requestWaveformVisibility (
                LfpWaveformVisibility::
                    hidden));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        OwnerRequestHidesCurrentChannelAndRejectsTheStaleGeneration)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    const auto request =
        display->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    ASSERT_TRUE (
        request
            ->validateCurrentAndAvailable());

    EXPECT_TRUE (
        request
            ->requestWaveformVisibility (
                LfpWaveformVisibility::
                    hidden));
    EXPECT_FALSE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));
    EXPECT_FALSE (
        request
            ->validateCurrentAndAvailable());

    EXPECT_FALSE (
        request
            ->requestWaveformVisibility (
                LfpWaveformVisibility::
                    visible));
    EXPECT_FALSE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        VisibilityRequestChangesDrawingWithoutChangingDataOrControlState)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer =
        splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    auto* stream =
        processor->getDataStream (
            splitter->selectedStreamId);
    ASSERT_NE (stream, nullptr);
    auto* messageCenter =
        tester->processorGraph
            ->getMessageCenter();
    ASSERT_NE (messageCenter, nullptr);
    ASSERT_TRUE (
        drainIdentityTestBroadcastMessages (
            *messageCenter)
            .isEmpty());
    const auto request =
        display->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    const auto selectedStreamId =
        splitter->selectedStreamId;
    const auto selectedStreamKey =
        splitter->selectedStreamKey;
    const auto bufferDisplays =
        buffer->displays;
    const auto bufferChannelCount =
        buffer->numChannels;
    const auto processorWasEnabled =
        processor->isEnabled;
    std::vector<bool> recordedSelection;
    for (const auto* channel :
         stream->getContinuousChannels())
    {
        ASSERT_NE (channel, nullptr);
        recordedSelection.push_back (
            channel->isRecorded);
    }

    ASSERT_TRUE (
        request
            ->requestWaveformVisibility (
                LfpWaveformVisibility::
                    hidden));

    EXPECT_FALSE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_EQ (
        splitter->displayBuffer,
        buffer);
    EXPECT_EQ (
        splitter->selectedStreamId,
        selectedStreamId);
    EXPECT_EQ (
        splitter->selectedStreamKey,
        selectedStreamKey);
    EXPECT_EQ (
        buffer->numChannels,
        bufferChannelCount);
    EXPECT_EQ (
        buffer->displays,
        bufferDisplays);
    EXPECT_EQ (
        processor->isEnabled,
        processorWasEnabled);
    const auto channelsAfter =
        stream->getContinuousChannels();
    ASSERT_EQ (
        channelsAfter.size(),
        static_cast<int> (
            recordedSelection.size()));
    for (int index = 0;
         index < channelsAfter.size();
         ++index)
    {
        EXPECT_EQ (
            channelsAfter[index]
                ->isRecorded,
            recordedSelection[
                static_cast<size_t> (
                    index)]);
    }
    EXPECT_TRUE (
        drainIdentityTestBroadcastMessages (
            *messageCenter)
            .isEmpty());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        SameComponentRebindRejectsOldVisibilityRequest)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer =
        splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    auto* componentBefore =
        display->channels[0];
    const auto oldRequest =
        componentBefore
            ->getStableChannelActionRequest();
    ASSERT_NE (oldRequest, nullptr);

    buffer->channelMetadata
        .getReference (0)
        .uuid = Uuid();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));
    ASSERT_EQ (
        display->channels[0],
        componentBefore);
    const auto currentRequest =
        componentBefore
            ->getStableChannelActionRequest();
    ASSERT_NE (currentRequest, nullptr);
    ASSERT_NE (
        currentRequest,
        oldRequest);

    EXPECT_FALSE (
        oldRequest
            ->requestWaveformVisibility (
                LfpWaveformVisibility::
                    hidden));
    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_TRUE (
        currentRequest
            ->requestWaveformVisibility (
                LfpWaveformVisibility::
                    hidden));
    EXPECT_FALSE (
        display->channels[0]
            ->getEnabledState());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        TimedOutWorkerVisibilityRequestCannotMutateWhenCallbackRunsLate)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    const auto request =
        display->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);

    std::mutex callbackMutex;
    std::function<void()> acceptedCallback;
    bool workerResult = true;
    {
        ScopedStableChannelDiagnosticTestSeam seam (
            [&] (
                std::function<void()> callback)
            {
                const std::lock_guard<
                    std::mutex>
                    lock (
                        callbackMutex);
                acceptedCallback =
                    std::move (
                        callback);
                return true;
            },
            {});
        std::thread worker (
            [&]
            {
                workerResult =
                    request
                        ->requestWaveformVisibility (
                            LfpWaveformVisibility::
                                hidden);
            });
        worker.join();

        std::function<void()> lateCallback;
        {
            const std::lock_guard<
                std::mutex>
                lock (
                    callbackMutex);
            lateCallback =
                std::move (
                    acceptedCallback);
        }
        ASSERT_TRUE (
            static_cast<bool> (
                lateCallback));
        lateCallback();
    }

    EXPECT_FALSE (workerResult);
    EXPECT_TRUE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_TRUE (
        display
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ClaimedWorkerVisibilityRequestWaitsForExplicitCompletion)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    const auto request =
        display->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);

    std::mutex callbackMutex;
    std::condition_variable
        callbackChanged;
    std::function<void()>
        acceptedCallback;
    std::mutex mutationMutex;
    std::condition_variable
        mutationChanged;
    bool mutationBarrierReached =
        false;
    bool releaseMutation = false;
    std::atomic<bool> workerFinished {
        false
    };
    bool workerResult = false;
    bool workerWasPendingAfterTimeout =
        false;
    display
        ->setStableIdentityLifecycleTestHook (
            [&] (
                LfpDisplay::
                    StableIdentityLifecycleTestPhase
                        phase,
                int channelIndex)
            {
                if (phase
                        != LfpDisplay::
                               StableIdentityLifecycleTestPhase::
                                   beforeVisibilityRequestMutation
                    || channelIndex != 0)
                {
                    return;
                }

                std::unique_lock<
                    std::mutex>
                    lock (
                        mutationMutex);
                if (! mutationBarrierReached)
                {
                    mutationBarrierReached =
                        true;
                    mutationChanged
                        .notify_all();
                    mutationChanged.wait (
                        lock,
                        [&]
                        {
                            return releaseMutation;
                        });
                }
            });

    {
        ScopedStableChannelDiagnosticTestSeam seam (
            [&] (
                std::function<void()> callback)
            {
                {
                    const std::lock_guard<
                        std::mutex>
                        lock (
                            callbackMutex);
                    acceptedCallback =
                        std::move (
                            callback);
                }
                callbackChanged
                    .notify_one();
                return true;
            },
            {});
        std::thread worker (
            [&]
            {
                workerResult =
                    request
                        ->requestWaveformVisibility (
                            LfpWaveformVisibility::
                                hidden);
                workerFinished.store (
                    true);
            });
        {
            std::unique_lock<
                std::mutex>
                lock (
                    callbackMutex);
            ASSERT_TRUE (
                callbackChanged.wait_for (
                    lock,
                    std::chrono::
                        seconds (1),
                    [&]
                    {
                        return static_cast<
                            bool> (
                            acceptedCallback);
                    }));
        }
        std::thread timeoutObserver (
            [&]
            {
                {
                    std::unique_lock<
                        std::mutex>
                        lock (
                            mutationMutex);
                    mutationChanged.wait (
                        lock,
                        [&]
                        {
                            return mutationBarrierReached;
                        });
                }
                Thread::sleep (150);
                workerWasPendingAfterTimeout =
                    ! workerFinished.load();
                {
                    const std::lock_guard<
                        std::mutex>
                        lock (
                            mutationMutex);
                    releaseMutation = true;
                }
                mutationChanged
                    .notify_all();
            });

        acceptedCallback();
        timeoutObserver.join();
        worker.join();
    }
    display
        ->setStableIdentityLifecycleTestHook (
            {});

    EXPECT_TRUE (
        workerWasPendingAfterTimeout);
    EXPECT_TRUE (workerResult);
    EXPECT_TRUE (
        workerFinished.load());
    EXPECT_FALSE (
        display->channels[0]
            ->getEnabledState());
    EXPECT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WorkerDiagnosticFailsClosedWhenMessageThreadDoesNotDispatch)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto request =
        splitters[0]
            ->lfpDisplay
            ->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    ASSERT_TRUE (
        request
            ->validateCurrentAndAvailable());

    std::atomic<bool> workerStarted {
        false
    };
    std::atomic<bool> workerFinished {
        false
    };
    bool workerResult = true;
    std::thread worker (
        [&,
         workerRequest = request]
        {
            workerStarted.store (true);
            workerResult =
                workerRequest
                    ->validateCurrentAndAvailable();
            workerFinished.store (true);
        });
    while (! workerStarted.load())
        std::this_thread::yield();

    for (int wait = 0;
         wait < 500
         && ! workerFinished.load();
         ++wait)
    {
        Thread::sleep (1);
    }
    const bool returnedWithoutDispatch =
        workerFinished.load();

    auto* messageManager =
        MessageManager::
            getInstanceWithoutCreating();
    ASSERT_NE (
        messageManager,
        nullptr);
    for (int dispatch = 0;
         dispatch < 1000
         && ! workerFinished.load();
         ++dispatch)
    {
        messageManager
            ->runDispatchLoopUntil (
                1);
    }
    ASSERT_TRUE (
        workerFinished.load());
    worker.join();

    EXPECT_TRUE (
        returnedWithoutDispatch);
    EXPECT_FALSE (
        workerResult);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WorkerDiagnosticFailsClosedWithoutExceptionWhenAcceptedCallbackIsDropped)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto request =
        splitters[0]
            ->lfpDisplay
            ->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    ASSERT_TRUE (
        request
            ->validateCurrentAndAvailable());

    std::atomic<int> dispatchCount {
        0
    };
    std::atomic<int> validationCount {
        0
    };
    std::exception_ptr workerException;
    bool workerResult = true;
    {
        ScopedStableChannelDiagnosticTestSeam seam (
            [&] (
                std::function<void()>)
            {
                dispatchCount.fetch_add (1);
                return true;
            },
            [&]
            {
                validationCount.fetch_add (1);
            });
        std::thread worker (
            [&]
            {
                try
                {
                    workerResult =
                        request
                            ->validateCurrentAndAvailable();
                }
                catch (...)
                {
                    workerException =
                        std::current_exception();
                }
            });
        worker.join();
    }

    EXPECT_FALSE (
        static_cast<bool> (
            workerException));
    EXPECT_FALSE (workerResult);
    EXPECT_EQ (dispatchCount.load(), 1);
    EXPECT_EQ (validationCount.load(), 0);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        TimedOutWorkerDiagnosticDoesNotRunLiveValidationWhenAcceptedCallbackRunsLate)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto request =
        splitters[0]
            ->lfpDisplay
            ->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    ASSERT_TRUE (
        request
            ->validateCurrentAndAvailable());

    std::mutex callbackMutex;
    std::function<void()> acceptedCallback;
    std::atomic<int> dispatchCount {
        0
    };
    std::atomic<int> validationCount {
        0
    };
    std::exception_ptr workerException;
    bool workerResult = true;
    {
        ScopedStableChannelDiagnosticTestSeam seam (
            [&] (
                std::function<void()> callback)
            {
                {
                    const std::lock_guard<std::mutex>
                        lock (
                            callbackMutex);
                    acceptedCallback =
                        std::move (
                            callback);
                }
                dispatchCount.fetch_add (1);
                return true;
            },
            [&]
            {
                validationCount.fetch_add (1);
            });
        std::thread worker (
            [&]
            {
                try
                {
                    workerResult =
                        request
                            ->validateCurrentAndAvailable();
                }
                catch (...)
                {
                    workerException =
                        std::current_exception();
                }
            });
        worker.join();

        std::function<void()> lateCallback;
        {
            const std::lock_guard<std::mutex>
                lock (
                    callbackMutex);
            lateCallback =
                std::move (
                    acceptedCallback);
        }
        ASSERT_TRUE (
            static_cast<bool> (
                lateCallback));
        EXPECT_NO_THROW (
            lateCallback());
    }

    EXPECT_FALSE (
        static_cast<bool> (
            workerException));
    EXPECT_FALSE (workerResult);
    EXPECT_EQ (dispatchCount.load(), 1);
    EXPECT_EQ (validationCount.load(), 0);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        AcceptedWorkerDiagnosticCompletesNormallyExactlyOnce)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto request =
        splitters[0]
            ->lfpDisplay
            ->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    ASSERT_TRUE (
        request
            ->validateCurrentAndAvailable());

    std::mutex callbackMutex;
    WaitableEvent callbackAccepted;
    std::function<void()> acceptedCallback;
    std::atomic<int> dispatchCount {
        0
    };
    std::atomic<int> validationCount {
        0
    };
    std::exception_ptr workerException;
    bool workerResult = false;
    bool callbackWasAccepted = false;
    {
        ScopedStableChannelDiagnosticTestSeam seam (
            [&] (
                std::function<void()> callback)
            {
                {
                    const std::lock_guard<std::mutex>
                        lock (
                            callbackMutex);
                    acceptedCallback =
                        std::move (
                            callback);
                }
                dispatchCount.fetch_add (1);
                callbackAccepted.signal();
                return true;
            },
            [&]
            {
                validationCount.fetch_add (1);
            });
        std::thread worker (
            [&]
            {
                try
                {
                    workerResult =
                        request
                            ->validateCurrentAndAvailable();
                }
                catch (...)
                {
                    workerException =
                        std::current_exception();
                }
            });
        callbackWasAccepted =
            callbackAccepted.wait (
                1000);

        std::function<void()> callback;
        {
            const std::lock_guard<std::mutex>
                lock (
                    callbackMutex);
            callback =
                std::move (
                    acceptedCallback);
        }
        if (callback)
            callback();
        worker.join();
    }

    EXPECT_TRUE (callbackWasAccepted);
    EXPECT_FALSE (
        static_cast<bool> (
            workerException));
    EXPECT_TRUE (workerResult);
    EXPECT_EQ (dispatchCount.load(), 1);
    EXPECT_EQ (validationCount.load(), 1);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        GenericComponentPointerHideRejectsAtLifecycleEntry)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();

    struct VisibilityCase
    {
        Component* component;
        LfpDisplay::
            StableIdentityLifecycleTestPhase
                expectedPhase;
    };
    const std::vector<VisibilityCase>
        cases {
            {
                canvas.get(),
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        canvasVisibilityChangedEntry },
            {
                splitter,
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        splitterVisibilityChangedEntry },
            {
                display->channels[0],
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        channelVisibilityChangedEntry },
            {
                display->channelInfo[0],
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        infoVisibilityChangedEntry }
        };

    for (const auto& testCase :
         cases)
    {
        const auto retained =
            display->channels[0]
                ->getStableChannelIdentity();
        ASSERT_NE (retained, nullptr);
        const auto request =
            display->channels[0]
                ->getStableChannelActionRequest();
        ASSERT_NE (request, nullptr);
        ASSERT_TRUE (
            testCase.component
                ->isShowing());
        ASSERT_TRUE (
            request
                ->validateCurrentAndAvailable());
        bool observedEntry = false;
        bool gateAcceptedAtEntry = false;
        display
            ->setStableIdentityLifecycleTestHook (
                [&] (
                    LfpDisplay::
                        StableIdentityLifecycleTestPhase
                            phase,
                    int channelIndex)
                {
                    if (phase
                            == testCase
                                   .expectedPhase
                        && (channelIndex == -1
                            || channelIndex == 0))
                    {
                        observedEntry = true;
                        gateAcceptedAtEntry =
                            request
                                ->validateCurrentAndAvailable();
                    }
                });

        testCase.component
            ->setVisible (
                false);

        EXPECT_TRUE (
            observedEntry);
        EXPECT_FALSE (
            gateAcceptedAtEntry);

        display
            ->setStableIdentityLifecycleTestHook (
                {});
        testCase.component
            ->setVisible (
                true);
        ASSERT_NE (
            display->channels[0]
                ->getStableChannelIdentity(),
            nullptr);
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        GenericComponentPointerDisableRejectsAtLifecycleEntry)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();

    struct EnablementCase
    {
        Component* component;
        LfpDisplay::
            StableIdentityLifecycleTestPhase
                expectedPhase;
    };
    const std::vector<EnablementCase>
        cases {
            {
                canvas.get(),
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        canvasEnablementChangedEntry },
            {
                splitter,
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        splitterEnablementChangedEntry },
            {
                display->channels[0],
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        channelEnablementChangedEntry },
            {
                display->channelInfo[0],
                LfpDisplay::
                    StableIdentityLifecycleTestPhase::
                        infoEnablementChangedEntry }
        };

    for (const auto& testCase :
         cases)
    {
        const auto retained =
            display->channels[0]
                ->getStableChannelIdentity();
        ASSERT_NE (retained, nullptr);
        const auto request =
            display->channels[0]
                ->getStableChannelActionRequest();
        ASSERT_NE (request, nullptr);
        ASSERT_TRUE (
            testCase.component
                ->isShowing());
        ASSERT_TRUE (
            testCase.component
                ->isEnabled());
        ASSERT_TRUE (
            request
                ->validateCurrentAndAvailable());
        bool observedEntry = false;
        bool gateAcceptedAtEntry = false;
        display
            ->setStableIdentityLifecycleTestHook (
                [&] (
                    LfpDisplay::
                        StableIdentityLifecycleTestPhase
                            phase,
                    int channelIndex)
                {
                    if (phase
                            == testCase
                                   .expectedPhase
                        && (channelIndex == -1
                            || channelIndex == 0))
                    {
                        observedEntry = true;
                        gateAcceptedAtEntry =
                            request
                                ->validateCurrentAndAvailable();
                    }
                });

        testCase.component
            ->setEnabled (
                false);

        EXPECT_TRUE (
            observedEntry);
        EXPECT_FALSE (
            gateAcceptedAtEntry);

        display
            ->setStableIdentityLifecycleTestHook (
                {});
        testCase.component
            ->setEnabled (
                true);
        ASSERT_NE (
            display->channels[0]
                ->getStableChannelIdentity(),
            nullptr);
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WorkerLiveGateDuringGenericMutationReturnsFalseWithoutDeadlock)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    const auto retained =
        display->channels[0]
            ->getStableChannelIdentity();
    ASSERT_NE (retained, nullptr);
    const auto request =
        display->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    ASSERT_TRUE (
        splitter->isShowing());
    ASSERT_TRUE (
        request
            ->validateCurrentAndAvailable());

    std::atomic<bool> workerStarted {
        false
    };
    std::atomic<bool> workerFinished {
        false
    };
    bool workerResult = true;
    std::thread worker;
    display
        ->setStableIdentityLifecycleTestHook (
            [&] (
                LfpDisplay::
                    StableIdentityLifecycleTestPhase
                        phase,
                int)
            {
                if (phase
                    != LfpDisplay::
                           StableIdentityLifecycleTestPhase::
                               splitterVisibilityChangedEntry)
                {
                    return;
                }
                worker =
                    std::thread (
                        [&]
                        {
                            workerStarted.store (
                                true);
                            workerResult =
                                request
                                    ->validateCurrentAndAvailable();
                            workerFinished.store (
                                true);
                        });
                while (! workerStarted.load())
                    std::this_thread::yield();
            });

    Component* genericSplitter =
        splitter;
    genericSplitter->setVisible (
        false);

    auto* messageManager =
        MessageManager::
            getInstanceWithoutCreating();
    ASSERT_NE (
        messageManager,
        nullptr);
    for (int dispatch = 0;
         dispatch < 1000
         && ! workerFinished.load();
         ++dispatch)
    {
        messageManager
            ->runDispatchLoopUntil (
                1);
    }
    ASSERT_TRUE (
        workerFinished.load());
    worker.join();

    EXPECT_FALSE (
        workerResult);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        RetainedLiveGateRejectsAfterOwnerDestruction)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    const auto retained =
        splitters[0]
            ->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    ASSERT_NE (retained, nullptr);
    const auto request =
        splitters[0]
            ->lfpDisplay
            ->channels[0]
            ->getStableChannelActionRequest();
    ASSERT_NE (request, nullptr);
    ASSERT_TRUE (
        request
            ->validateCurrentAndAvailable());

    canvas.reset();

    EXPECT_FALSE (
        request
            ->validateCurrentAndAvailable());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        RealDisplayBulkTransitionsHaveLinearAvailabilityWork)
{
    constexpr int channelCount = 12000;
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->resized();
    canvas->setVisible (true);
    canvas->updateSettings();
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    auto* buffer =
        splitter->displayBuffer;
    ASSERT_NE (buffer, nullptr);
    for (int channel =
             buffer->channelMetadata
                 .size();
         channel < channelCount;
         ++channel)
    {
        buffer->channelMetadata
            .add (
            makeIdentityMetadata (
                "large.real."
                    + String (channel),
                101,
                channel));
    }
    buffer->numChannels =
        channelCount;
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            buffer->streamKey));
    ASSERT_EQ (
        display->channels.size(),
        channelCount);
    ASSERT_TRUE (
        display
            ->getStoredChannelVisibility (
                channelCount - 1));
    display->setEnabledState (
        false,
        channelCount - 1,
        true);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                channelCount - 1));
    ASSERT_FALSE (
        display->channels[
            channelCount - 1]
            ->getEnabledState());

    display
        ->resetStableIdentityAvailabilityWorkForTests();
    display
        ->setChannelDisplaySkipAmount (
            2);
    display
        ->setChannelDisplaySkipAmount (
            0);
    ASSERT_FALSE (
        display->drawableChannels
            .isEmpty());
    display
        ->toggleSingleChannel (
            display->drawableChannels[0]);

    EXPECT_LE (
        display
            ->getStableIdentityAvailabilityWorkForTests(),
        static_cast<uint64> (
            channelCount * 20));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        SaveWritesLegacyStateAndDeterministicV2HiddenRecordsWithoutRuntimeCoordinates)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter = splitters[0];
    auto* display =
        splitter->lfpDisplay.get();
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    ASSERT_GE (
        buffers[0]->channelMetadata.size(),
        2);
    ASSERT_GE (
        buffers[1]->channelMetadata.size(),
        2);

    buffers[0]->streamKey =
        "stream.z";
    buffers[1]->streamKey =
        "stream.a";
    auto& zChannel =
        buffers[0]->channelMetadata
            .getReference (0);
    zChannel.identifier =
        "identifier.z";
    zChannel.sourceNodeId = 42;
    zChannel.localIndex = 7;
    zChannel.name = "Channel Z";
    zChannel.type =
        ContinuousChannel::Type::AUX;
    auto& aChannel =
        buffers[1]->channelMetadata
            .getReference (1);
    aChannel.identifier =
        "identifier.a";
    aChannel.sourceNodeId = 24;
    aChannel.localIndex = 3;
    aChannel.name = "Channel A";
    aChannel.type =
        ContinuousChannel::Type::ADC;
    configureIdentityPersistenceChannel (
        *buffers[1],
        0,
        "identifier.zeta",
        25,
        4,
        "Channel A-Z",
        ContinuousChannel::Type::ELECTRODE);
    canvas->updateSettings();

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "stream.z"));
    auto* zEnable =
        getIdentityTestEnableButton (
            *display->channelInfo[0]);
    ASSERT_NE (zEnable, nullptr);
    ASSERT_TRUE (
        zEnable->getToggleState());
    zEnable->triggerClick();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            20);
    ASSERT_FALSE (
        zEnable->getToggleState());
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                0));

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "stream.a"));
    auto* aEnable =
        getIdentityTestEnableButton (
            *display->channelInfo[1]);
    ASSERT_NE (aEnable, nullptr);
    aEnable->triggerClick();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            20);
    ASSERT_FALSE (
        display
            ->getStoredChannelVisibility (
                1));
    display->setEnabledState (
        false,
        0,
        true);

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "stream.z"));
    XmlElement root ("ROOT");
    splitter->options
        ->saveParameters (
            &root);
    auto* pane =
        root.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (pane, nullptr);
    ASSERT_EQ (
        pane->getStringAttribute (
            "ChannelDisplayState")
            .length(),
        display->getNumChannels());
    EXPECT_EQ (
        pane->getStringAttribute (
            "ChannelDisplayState")
            .substring (
                0,
                1),
        "0");

    auto* visibility =
        pane->getChildByName (
            "WAVEFORM_VISIBILITY_STATE");
    ASSERT_NE (visibility, nullptr);
    EXPECT_EQ (
        visibility->getStringAttribute (
            "version"),
        "2");
    ASSERT_EQ (
        visibility
            ->getNumChildElements(),
        3);
    const auto* first =
        visibility
            ->getChildElement (0);
    const auto* second =
        visibility
            ->getChildElement (1);
    const auto* third =
        visibility
            ->getChildElement (2);
    ASSERT_NE (first, nullptr);
    ASSERT_NE (second, nullptr);
    ASSERT_NE (third, nullptr);
    EXPECT_TRUE (
        first->hasTagName (
            "HIDDEN_CHANNEL"));
    EXPECT_TRUE (
        second->hasTagName (
            "HIDDEN_CHANNEL"));
    EXPECT_TRUE (
        third->hasTagName (
            "HIDDEN_CHANNEL"));
    EXPECT_EQ (
        first->getStringAttribute (
            "stream_key"),
        "stream.a");
    EXPECT_EQ (
        first->getStringAttribute (
            "stable_key_kind"),
        "identifier");
    EXPECT_EQ (
        first->getStringAttribute (
            "identifier"),
        "identifier.a");
    EXPECT_EQ (
        first->getIntAttribute (
            "source_node_id"),
        24);
    EXPECT_EQ (
        first->getIntAttribute (
            "local_index"),
        3);
    EXPECT_EQ (
        first->getStringAttribute (
            "channel_name"),
        "Channel A");
    EXPECT_EQ (
        first->getIntAttribute (
            "channel_type"),
        static_cast<int> (
            ContinuousChannel::Type::ADC));
    EXPECT_EQ (
        second->getStringAttribute (
            "stream_key"),
        "stream.a");
    EXPECT_EQ (
        second->getStringAttribute (
            "identifier"),
        "identifier.zeta");
    EXPECT_EQ (
        third->getStringAttribute (
            "stream_key"),
        "stream.z");
    EXPECT_EQ (
        third->getStringAttribute (
            "identifier"),
        "identifier.z");
    EXPECT_EQ (
        third->getIntAttribute (
            "source_node_id"),
        42);
    EXPECT_EQ (
        third->getIntAttribute (
            "local_index"),
        7);
    EXPECT_EQ (
        third->getStringAttribute (
            "channel_name"),
        "Channel Z");
    EXPECT_EQ (
        third->getIntAttribute (
            "channel_type"),
        static_cast<int> (
            ContinuousChannel::Type::AUX));

    const auto savedXml =
        root.toString();
    EXPECT_FALSE (
        savedXml.containsIgnoreCase (
            "uuid"));
    EXPECT_FALSE (
        savedXml.contains (
            "stream_id"));
    EXPECT_FALSE (
        savedXml.contains (
            "global_index"));
    EXPECT_FALSE (
        savedXml.contains (
            "drawable"));
    EXPECT_FALSE (
        savedXml.contains (
            "screen"));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        V2RoundTripKeepsPaneAndStreamIsolationAcrossRuntimeRestartAndReorder)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::TWO_VERT);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_GE (splitters.size(), 2u);
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "roundtrip.z";
    buffers[1]->streamKey =
        "roundtrip.a";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "roundtrip.identifier.z",
        70,
        0,
        "Roundtrip Z",
        ContinuousChannel::Type::AUX);
    configureIdentityPersistenceChannel (
        *buffers[1],
        1,
        "roundtrip.identifier.a",
        80,
        1,
        "Roundtrip A",
        ContinuousChannel::Type::ADC);
    canvas->updateSettings();

    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "roundtrip.z"));
    ASSERT_TRUE (
        splitters[1]
            ->selectStreamByKey (
                "roundtrip.a"));
    splitters[0]
        ->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    splitters[0]
        ->lfpDisplay
        ->setEnabledState (
            false,
            2,
            true);
    splitters[1]
        ->lfpDisplay
        ->setEnabledState (
            false,
            1,
            true);

    XmlElement savedRoot (
        "ROOT");
    splitters[0]
        ->options
        ->saveParameters (
            &savedRoot);
    splitters[1]
        ->options
        ->saveParameters (
            &savedRoot);

    showAllIdentityTestChannels (
        *splitters[0]);
    showAllIdentityTestChannels (
        *splitters[1]);
    for (auto* buffer :
         buffers)
    {
        for (auto& metadata :
             buffer->channelMetadata)
        {
            metadata.uuid =
                Uuid();
        }
    }
    std::swap (
        buffers[0]->channelMetadata
            .getReference (0),
        buffers[0]->channelMetadata
            .getReference (1));
    buffers[0]->channelMetadata
        .getReference (1)
        .localIndex = 99;
    canvas->updateSettings();

    splitters[0]
        ->options
        ->loadParameters (
            &savedRoot);
    splitters[1]
        ->options
        ->loadParameters (
            &savedRoot);

    ASSERT_EQ (
        buffers[0]->channelMetadata[1]
            .identifier,
        "roundtrip.identifier.z");
    EXPECT_TRUE (
        splitters[0]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    EXPECT_FALSE (
        splitters[0]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                1));
    EXPECT_FALSE (
        splitters[0]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                2));
    EXPECT_TRUE (
        splitters[1]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    EXPECT_FALSE (
        splitters[1]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                1));
    EXPECT_TRUE (
        splitters[1]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                2));

    XmlElement restoredRoot (
        "ROOT");
    splitters[0]
        ->options
        ->saveParameters (
            &restoredRoot);
    auto* restoredPane =
        restoredRoot
            .getChildByName (
                "LFPDISPLAY0");
    ASSERT_NE (
        restoredPane,
        nullptr);
    auto* restoredState =
        restoredPane
            ->getChildByName (
                "WAVEFORM_VISIBILITY_STATE");
    ASSERT_NE (
        restoredState,
        nullptr);
    const XmlElement* movedRecord =
        nullptr;
    for (const auto* record :
         restoredState
             ->getChildIterator())
    {
        if (record
                ->getStringAttribute (
                    "identifier")
            == "roundtrip.identifier.z")
        {
            movedRecord = record;
        }
    }
    ASSERT_NE (
        movedRecord,
        nullptr);
    EXPECT_EQ (
        movedRecord
            ->getIntAttribute (
                "local_index"),
        99);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        V2AuthorityQuarantinesUnknownMalformedDuplicateConflictingAndUnmatchedRecords)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "safe.stream";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "safe.identifier",
        50,
        4,
        "Safe Channel",
        ContinuousChannel::Type::ELECTRODE);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "safe.stream"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement baseline (
        "ROOT");
    splitter->options
        ->saveParameters (
            &baseline);
    auto* baselinePane =
        baseline.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (
        baselinePane,
        nullptr);
    baselinePane->setAttribute (
        "ChannelDisplayState",
        "0000");

    const auto expectTargetVisible =
        [&] (
            XmlElement& root,
            const String& caseName)
    {
        SCOPED_TRACE (
            caseName);
        showAllIdentityTestChannels (
            *splitter);
        splitter->options
            ->loadParameters (
                &root);
        EXPECT_TRUE (
            splitter
                ->lfpDisplay
                ->getStoredChannelVisibility (
                    0));
        EXPECT_TRUE (
            splitter
                ->lfpDisplay
                ->channels[0]
                ->getEnabledState());
    };

    XmlElement unknownVersion (
        baseline);
    getIdentityTestVisibilityState (
        unknownVersion)
        ->setAttribute (
            "version",
            999);
    expectTargetVisible (
        unknownVersion,
        "unknown version");

    XmlElement malformedInteger (
        baseline);
    getIdentityTestHiddenRecord (
        malformedInteger)
        ->setAttribute (
            "source_node_id",
            "50tail");
    expectTargetVisible (
        malformedInteger,
        "malformed integer");

    XmlElement mixedMalformedRecord (
        baseline);
    auto* mixedMalformedState =
        getIdentityTestVisibilityState (
            mixedMalformedRecord);
    auto* malformedSibling =
        new XmlElement (
            *mixedMalformedState
                 ->getFirstChildElement());
    malformedSibling->setAttribute (
        "source_node_id",
        "50tail");
    mixedMalformedState
        ->addChildElement (
            malformedSibling);
    expectTargetVisible (
        mixedMalformedRecord,
        "valid and malformed records share one stable key");

    XmlElement duplicateRecord (
        baseline);
    auto* duplicateState =
        getIdentityTestVisibilityState (
            duplicateRecord);
    duplicateState
        ->addChildElement (
            new XmlElement (
                *duplicateState
                     ->getFirstChildElement()));
    expectTargetVisible (
        duplicateRecord,
        "duplicate record");

    XmlElement conflictingRecord (
        baseline);
    auto* conflictingState =
        getIdentityTestVisibilityState (
            conflictingRecord);
    auto* secondRecord =
        new XmlElement (
            *conflictingState
                 ->getFirstChildElement());
    secondRecord->setAttribute (
        "local_index",
        99);
    conflictingState
        ->addChildElement (
            secondRecord);
    expectTargetVisible (
        conflictingRecord,
        "multiple records resolve one identity");

    XmlElement signatureConflict (
        baseline);
    getIdentityTestHiddenRecord (
        signatureConflict)
        ->setAttribute (
            "channel_name",
            "Replacement Channel");
    expectTargetVisible (
        signatureConflict,
        "signature conflict");

    XmlElement mixedSignatureConflict (
        baseline);
    auto* mixedSignatureState =
        getIdentityTestVisibilityState (
            mixedSignatureConflict);
    auto* conflictingSignatureRecord =
        new XmlElement (
            *mixedSignatureState
                 ->getFirstChildElement());
    conflictingSignatureRecord
        ->setAttribute (
            "channel_name",
            "Replacement Channel");
    mixedSignatureState
        ->addChildElement (
            conflictingSignatureRecord);
    expectTargetVisible (
        mixedSignatureConflict,
        "valid and signature-conflicting records share one key");

    XmlElement unmatched (
        baseline);
    auto* unmatchedRecord =
        getIdentityTestHiddenRecord (
            unmatched);
    unmatchedRecord->setAttribute (
        "identifier",
        "missing.identifier");
    unmatchedRecord->setAttribute (
        "source_node_id",
        999);
    unmatchedRecord->setAttribute (
        "local_index",
        999);
    expectTargetVisible (
        unmatched,
        "unmatched record");

    XmlElement ambiguousIdentifier (
        baseline);
    buffers[0]->channelMetadata
        .getReference (1)
        .identifier =
        "safe.identifier";
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "safe.stream"));
    expectTargetVisible (
        ambiguousIdentifier,
        "ambiguous current identifier");
    buffers[0]->channelMetadata
        .getReference (1)
        .identifier =
        "other.identifier";
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "safe.stream"));
    EXPECT_TRUE (
        splitter
            ->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        LegacyMigrationRequiresAnUnambiguousTargetAndStrictBinaryCharacters)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto allBuffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (allBuffers.size(), 2);
    allBuffers[1]->numChannels = 0;
    canvas->updateSettings();
    const auto oneBuffer =
        processor->getDisplayBuffers();
    ASSERT_EQ (oneBuffer.size(), 1);
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                oneBuffer[0]
                    ->streamKey));

    XmlElement legacyRoot (
        "ROOT");
    splitter->options
        ->saveParameters (
            &legacyRoot);
    auto* legacyPane =
        legacyRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (
        legacyPane,
        nullptr);
    legacyPane
        ->removeChildElement (
            legacyPane
                ->getChildByName (
                    "WAVEFORM_VISIBILITY_STATE"),
            true);
    legacyPane->setAttribute (
        "ChannelDisplayState",
        "01x");
    showAllIdentityTestChannels (
        *splitter);
    splitter->options
        ->loadParameters (
            &legacyRoot);
    ASSERT_GE (
        splitter->lfpDisplay
            ->getNumChannels(),
        4);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                1));
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                2));
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                3));

    allBuffers[1]->numChannels =
        allBuffers[1]
            ->channelMetadata
            .size();
    canvas->updateSettings();
    ASSERT_EQ (
        processor
            ->getDisplayBuffers()
            .size(),
        2);
    legacyPane->setAttribute (
        "ChannelDisplayState",
        "0000");
    showAllIdentityTestChannels (
        *splitter);
    splitter->options
        ->loadParameters (
            &legacyRoot);
    for (int channel = 0;
         channel
         < splitter->lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        EXPECT_FALSE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    channel));
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        SourceLocalV2RoundTripRequiresUniqueMatchAndExactNameTypeSignature)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "source.local.stream";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "",
        91,
        5,
        "Source Local",
        ContinuousChannel::Type::AUX);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "source.local.stream"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement savedRoot (
        "ROOT");
    splitter->options
        ->saveParameters (
            &savedRoot);
    auto* savedRecord =
        getIdentityTestHiddenRecord (
            savedRoot);
    ASSERT_NE (
        savedRecord,
        nullptr);
    EXPECT_EQ (
        savedRecord
            ->getStringAttribute (
                "stable_key_kind"),
        "source_local");
    EXPECT_FALSE (
        savedRecord->hasAttribute (
            "identifier"));

    showAllIdentityTestChannels (
        *splitter);
    buffers[0]->channelMetadata
        .getReference (0)
        .uuid =
        Uuid();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "source.local.stream"));
    splitter->options
        ->loadParameters (
            &savedRoot);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    showAllIdentityTestChannels (
        *splitter);
    buffers[0]->channelMetadata
        .getReference (0)
        .name =
        "Replacement";
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "source.local.stream"));
    splitter->options
        ->loadParameters (
            &savedRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ValidEmptyAndMultipleV2NodesRemainAuthoritativeOverLegacyAndDirtyState)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "authority.stream";
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "authority.stream"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement baseline (
        "ROOT");
    splitter->options
        ->saveParameters (
            &baseline);
    auto* baselinePane =
        baseline.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (
        baselinePane,
        nullptr);
    baselinePane->setAttribute (
        "ChannelDisplayState",
        "0000");

    showAllIdentityTestChannels (
        *splitter);
    splitter->options
        ->loadParameters (
            &baseline);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    for (int channel = 1;
         channel
         < splitter->lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        EXPECT_TRUE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    channel));
    }

    XmlElement emptyState (
        baseline);
    auto* emptyStateNode =
        getIdentityTestVisibilityState (
            emptyState);
    ASSERT_NE (
        emptyStateNode,
        nullptr);
    emptyStateNode
        ->deleteAllChildElements();
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            1,
            true);
    splitter->options
        ->loadParameters (
            &emptyState);
    for (int channel = 0;
         channel
         < splitter->lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        EXPECT_TRUE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    channel));
    }

    XmlElement multipleStates (
        baseline);
    auto* multiplePane =
        getIdentityTestPane (
            multipleStates);
    ASSERT_NE (
        multiplePane,
        nullptr);
    multiplePane
        ->addChildElement (
            new XmlElement (
                *multiplePane
                     ->getChildByName (
                         "WAVEFORM_VISIBILITY_STATE")));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            2,
            true);
    splitter->options
        ->loadParameters (
            &multipleStates);
    for (int channel = 0;
         channel
         < splitter->lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        EXPECT_TRUE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    channel));
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        RemovedAmbiguousAndKindChangedChannelsDoNotResurrectStagedHiddenState)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "no.resurrection";
    buffers[1]->streamKey =
        "other.stream";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "no.resurrection.identifier",
        120,
        6,
        "No Resurrection",
        ContinuousChannel::Type::ELECTRODE);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "no.resurrection"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement savedRoot (
        "ROOT");
    splitter->options
        ->saveParameters (
            &savedRoot);

    showAllIdentityTestChannels (
        *splitter);
    auto removedMetadata =
        buffers[0]->channelMetadata[0];
    buffers[0]->channelMetadata
        .remove (
            0);
    buffers[0]->numChannels =
        buffers[0]->channelMetadata
            .size();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "no.resurrection"));
    splitter->options
        ->loadParameters (
            &savedRoot);
    buffers[0]->channelMetadata
        .insert (
            0,
            removedMetadata);
    buffers[0]->numChannels =
        buffers[0]->channelMetadata
            .size();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "no.resurrection"));
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement currentSavedRoot (
        "ROOT");
    splitter->options
        ->saveParameters (
            &currentSavedRoot);
    showAllIdentityTestChannels (
        *splitter);
    buffers[0]->channelMetadata
        .getReference (1)
        .identifier =
        "no.resurrection.identifier";
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "no.resurrection"));
    splitter->options
        ->loadParameters (
            &currentSavedRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    buffers[0]->channelMetadata
        .getReference (1)
        .identifier =
        "other.identifier";
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "no.resurrection"));
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    buffers[1]->streamKey =
        "no.resurrection";
    splitter->options
        ->loadParameters (
            &currentSavedRoot);
    buffers[1]->streamKey =
        "other.stream";
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter
            ->selectStreamByKey (
                "no.resurrection"));
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        ThreePanesRoundTripHiddenStateForEveryCurrentStreamIndependently)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::THREE_VERT);
    const auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3u);
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "three.stream.a";
    buffers[1]->streamKey =
        "three.stream.b";
    canvas->updateSettings();

    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "three.stream.a"));
    splitters[0]->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "three.stream.b"));
    splitters[0]->lfpDisplay
        ->setEnabledState (
            false,
            1,
            true);
    ASSERT_TRUE (
        splitters[1]
            ->selectStreamByKey (
                "three.stream.b"));
    splitters[1]->lfpDisplay
        ->setEnabledState (
            false,
            2,
            true);
    ASSERT_TRUE (
        splitters[2]
            ->selectStreamByKey (
                "three.stream.a"));
    splitters[2]->lfpDisplay
        ->setEnabledState (
            false,
            3,
            true);

    XmlElement savedRoot (
        "ROOT");
    for (auto* splitter :
         splitters)
    {
        splitter->options
            ->saveParameters (
                &savedRoot);
        for (const auto* buffer :
             buffers)
        {
            ASSERT_TRUE (
                splitter
                    ->selectStreamByKey (
                        buffer
                            ->streamKey));
            showAllIdentityTestChannels (
                *splitter);
        }
    }

    for (auto* splitter :
         splitters)
    {
        splitter->options
            ->loadParameters (
                &savedRoot);
    }

    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "three.stream.a"));
    EXPECT_FALSE (
        splitters[0]->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    EXPECT_TRUE (
        splitters[0]->lfpDisplay
            ->getStoredChannelVisibility (
                1));
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "three.stream.b"));
    EXPECT_TRUE (
        splitters[0]->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    EXPECT_FALSE (
        splitters[0]->lfpDisplay
            ->getStoredChannelVisibility (
                1));
    ASSERT_TRUE (
        splitters[1]
            ->selectStreamByKey (
                "three.stream.b"));
    EXPECT_FALSE (
        splitters[1]->lfpDisplay
            ->getStoredChannelVisibility (
                2));
    ASSERT_TRUE (
        splitters[2]
            ->selectStreamByKey (
                "three.stream.a"));
    EXPECT_FALSE (
        splitters[2]->lfpDisplay
            ->getStoredChannelVisibility (
                3));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        FullCanvasV2LoadStaysPendingAcrossAnEmptyLateBind)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "late.bind.stream";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "late.bind.identifier",
        301,
        7,
        "Late Bind",
        ContinuousChannel::Type::AUX);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "late.bind.stream"));
    auto* enableButton =
        getIdentityTestEnableButton (
            *splitters[0]
                 ->lfpDisplay
                 ->channelInfo[0]);
    ASSERT_NE (
        enableButton,
        nullptr);
    enableButton->triggerClick();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            20);
    ASSERT_FALSE (
        splitters[0]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    const int firstChannelCount =
        buffers[0]->numChannels;
    const int secondChannelCount =
        buffers[1]->numChannels;
    {
        XmlElement savedRoot (
            "ROOT");
        canvas
            ->saveCustomParametersToXml (
                &savedRoot);
        buffers[0]->numChannels = 0;
        buffers[1]->numChannels = 0;
        splitters[0]
            ->options
            ->loadParameters (
                &savedRoot);
    }
    buffers[0]->numChannels =
        firstChannelCount;
    buffers[1]->numChannels =
        secondChannelCount;
    canvas->updateSettings();
    splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "late.bind.stream"));

    EXPECT_FALSE (
        splitters[0]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    enableButton =
        getIdentityTestEnableButton (
            *splitters[0]
                 ->lfpDisplay
                 ->channelInfo[0]);
    ASSERT_NE (
        enableButton,
        nullptr);
    EXPECT_FALSE (
        enableButton
            ->getToggleState());
}

TEST_F (LfpStableChannelIdentityBindingTests,
        FullCustomThreePaneAllStreamStateSurvivesFreshRuntimeAndMetadataChurn)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::THREE_VERT);
    auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3u);
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "fresh.three.a";
    buffers[1]->streamKey =
        "fresh.three.b";
    for (int stream = 0;
         stream < buffers.size();
         ++stream)
    {
        for (int channel = 0;
             channel
             < buffers[stream]
                   ->channelMetadata
                   .size();
             ++channel)
        {
            configureIdentityPersistenceChannel (
                *buffers[stream],
                channel,
                "fresh."
                    + String (stream)
                    + ".channel."
                    + String (channel),
                500 + stream,
                channel,
                "Fresh "
                    + String (stream)
                    + " "
                    + String (channel),
                ContinuousChannel::Type::
                    ELECTRODE);
        }
    }
    canvas->updateSettings();

    String expectedHidden[3][2];
    for (int pane = 0;
         pane < 3;
         ++pane)
    {
        for (int stream = 0;
             stream < 2;
             ++stream)
        {
            const int channel =
                (pane + stream)
                % buffers[stream]
                      ->channelMetadata
                      .size();
            expectedHidden[pane][stream] =
                buffers[stream]
                    ->channelMetadata[
                        channel]
                    .identifier;
            ASSERT_TRUE (
                splitters[
                    static_cast<size_t> (
                        pane)]
                    ->selectStreamByKey (
                        buffers[stream]
                            ->streamKey));
            splitters[
                static_cast<size_t> (
                    pane)]
                ->lfpDisplay
                ->setEnabledState (
                    false,
                    channel,
                    true);
        }
    }

    XmlElement savedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &savedRoot);
    String savedV2[3];
    for (int pane = 0;
         pane < 3;
         ++pane)
    {
        const auto* state =
            getIdentityTestVisibilityState (
                savedRoot,
                pane);
        ASSERT_NE (state, nullptr);
        EXPECT_EQ (
            state->getNumChildElements(),
            2);
        savedV2[pane] =
            state->toString();
    }

    canvas.reset();
    for (auto* buffer :
         buffers)
    {
        for (auto& metadata :
             buffer->channelMetadata)
        {
            metadata.uuid = Uuid();
        }
        std::swap (
            buffer->channelMetadata
                .getReference (0),
            buffer->channelMetadata
                .getReference (3));
        std::swap (
            buffer->channelMetadata
                .getReference (1),
            buffer->channelMetadata
                .getReference (2));
    }

    canvas =
        createIdentityCanvas (
            SplitLayouts::THREE_VERT);
    canvas
        ->loadCustomParametersFromXml (
            &savedRoot);
    splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3u);
    for (int pane = 0;
         pane < 3;
         ++pane)
    {
        for (int stream = 0;
             stream < 2;
             ++stream)
        {
            ASSERT_TRUE (
                splitters[
                    static_cast<size_t> (
                        pane)]
                    ->selectStreamByKey (
                        buffers[stream]
                            ->streamKey));
            int hiddenCount = 0;
            for (int channel = 0;
                 channel
                 < buffers[stream]
                       ->channelMetadata
                       .size();
                 ++channel)
            {
                const bool expectedVisible =
                    buffers[stream]
                        ->channelMetadata[
                            channel]
                        .identifier
                    != expectedHidden[
                           pane][stream];
                EXPECT_EQ (
                    splitters[
                        static_cast<
                            size_t> (
                            pane)]
                        ->lfpDisplay
                        ->getStoredChannelVisibility (
                            channel),
                    expectedVisible);
                if (! expectedVisible)
                    ++hiddenCount;
            }
            EXPECT_EQ (
                hiddenCount,
                1);
        }
    }

    XmlElement restoredRoot (
        "ROOT");
    XmlElement repeatedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &restoredRoot);
    canvas
        ->saveCustomParametersToXml (
            &repeatedRoot);
    for (int pane = 0;
         pane < 3;
         ++pane)
    {
        const auto* restoredState =
            getIdentityTestVisibilityState (
                restoredRoot,
                pane);
        const auto* repeatedState =
            getIdentityTestVisibilityState (
                repeatedRoot,
                pane);
        ASSERT_NE (
            restoredState,
            nullptr);
        ASSERT_NE (
            repeatedState,
            nullptr);
        EXPECT_EQ (
            restoredState->toString(),
            savedV2[pane]);
        EXPECT_EQ (
            repeatedState->toString(),
            savedV2[pane]);
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        LegacyParentStreamKeyControlsExactMigrationAndSoleStreamFallback)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "legacy.target";
    buffers[1]->streamKey =
        "legacy.other";
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "legacy.target"));

    XmlElement legacyRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &legacyRoot);
    auto* legacyPane =
        getIdentityTestPane (
            legacyRoot);
    ASSERT_NE (
        legacyPane,
        nullptr);
    legacyPane
        ->removeChildElement (
            legacyPane
                ->getChildByName (
                    "WAVEFORM_VISIBILITY_STATE"),
            true);
    legacyPane->setAttribute (
        "ChannelDisplayState",
        "0111");
    legacyPane->setAttribute (
        "stream_key",
        "legacy.target");

    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &legacyRoot);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    buffers[1]->numChannels = 0;
    canvas->updateSettings();
    legacyPane->setAttribute (
        "stream_key",
        "legacy.missing");
    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &legacyRoot);
    for (int channel = 0;
         channel
         < splitter->lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        EXPECT_TRUE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    channel));
    }

    legacyPane->removeAttribute (
        "stream_key");
    canvas
        ->loadCustomParametersFromXml (
            &legacyRoot);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    buffers[1]->numChannels =
        buffers[1]
            ->channelMetadata
            .size();
    buffers[0]->streamKey =
        "legacy.duplicate";
    buffers[1]->streamKey =
        "legacy.duplicate";
    canvas->updateSettings();
    legacyPane->setAttribute (
        "stream_key",
        "legacy.duplicate");
    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &legacyRoot);
    for (int channel = 0;
         channel
         < splitter->lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        EXPECT_TRUE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    channel));
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        SourceLocalRestoreUsesRawMetadataAndQuarantinesCrossKindConflicts)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "raw.source.local";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "initially.duplicate",
        401,
        4,
        "Raw Source Local",
        ContinuousChannel::Type::ADC);
    configureIdentityPersistenceChannel (
        *buffers[0],
        1,
        "initially.duplicate",
        401,
        5,
        "Other Duplicate",
        ContinuousChannel::Type::ADC);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "raw.source.local"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement savedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &savedRoot);
    auto* savedRecord =
        getIdentityTestHiddenRecord (
            savedRoot);
    ASSERT_NE (
        savedRecord,
        nullptr);
    ASSERT_EQ (
        savedRecord
            ->getStringAttribute (
                "stable_key_kind"),
        "source_local");

    showAllIdentityTestChannels (
        *splitter);
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .identifier =
        "now.unique.other";
    canvas->updateSettings();
    canvas
        ->loadCustomParametersFromXml (
            &savedRoot);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    XmlElement crossKindRoot (
        savedRoot);
    auto* crossKindState =
        getIdentityTestVisibilityState (
            crossKindRoot);
    ASSERT_NE (
        crossKindState,
        nullptr);
    auto* identifierRecord =
        new XmlElement (
            *crossKindState
                 ->getFirstChildElement());
    identifierRecord->setAttribute (
        "stable_key_kind",
        "identifier");
    identifierRecord->setAttribute (
        "identifier",
        "initially.duplicate");
    crossKindState->addChildElement (
        identifierRecord);
    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &crossKindRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    buffers[0]
        ->channelMetadata
        .getReference (0)
        .sourceNodeId =
        499;
    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &savedRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        IdentifierRestoreCountsInvalidRawDuplicateMetadataAsAmbiguous)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "raw.identifier.ambiguity";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "raw.duplicated.identifier",
        801,
        0,
        "Identifier Target",
        ContinuousChannel::Type::ELECTRODE);
    configureIdentityPersistenceChannel (
        *buffers[0],
        1,
        "initially.other.identifier",
        801,
        1,
        "Invalid Duplicate",
        ContinuousChannel::Type::ELECTRODE);
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .uuid =
        Uuid();
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .uuid =
        Uuid();
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "raw.identifier.ambiguity"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement savedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &savedRoot);
    auto* savedRecord =
        getIdentityTestHiddenRecord (
            savedRoot);
    ASSERT_NE (
        savedRecord,
        nullptr);
    ASSERT_EQ (
        savedRecord
            ->getStringAttribute (
                "stable_key_kind"),
        "identifier");

    showAllIdentityTestChannels (
        *splitter);
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .identifier =
        "raw.duplicated.identifier";
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .uuid =
        Uuid::null();
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "raw.identifier.ambiguity"));
    const auto actionableIdentity =
        splitter->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    const auto invalidIdentity =
        splitter->lfpDisplay
            ->channels[1]
            ->getStableChannelIdentity();
    ASSERT_NE (
        actionableIdentity,
        nullptr);
    ASSERT_NE (
        actionableIdentity
            ->getStableChannelKey(),
        nullptr);
    ASSERT_NE (
        invalidIdentity,
        nullptr);
    ASSERT_EQ (
        invalidIdentity
            ->getStableChannelKey(),
        nullptr);

    canvas
        ->loadCustomParametersFromXml (
            &savedRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        SourceLocalRestoreCountsInvalidRawDuplicateMetadataAsAmbiguous)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "raw.source.ambiguity";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "",
        802,
        4,
        "Source Target",
        ContinuousChannel::Type::AUX);
    configureIdentityPersistenceChannel (
        *buffers[0],
        1,
        "initially.other.source",
        802,
        5,
        "Invalid Duplicate",
        ContinuousChannel::Type::AUX);
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .uuid =
        Uuid();
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .uuid =
        Uuid();
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "raw.source.ambiguity"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement savedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &savedRoot);
    auto* savedRecord =
        getIdentityTestHiddenRecord (
            savedRoot);
    ASSERT_NE (
        savedRecord,
        nullptr);
    ASSERT_EQ (
        savedRecord
            ->getStringAttribute (
                "stable_key_kind"),
        "source_local");

    showAllIdentityTestChannels (
        *splitter);
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .identifier =
        "now.unique.source.target";
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .sourceNodeId =
        802;
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .localIndex =
        4;
    buffers[0]
        ->channelMetadata
        .getReference (1)
        .uuid =
        Uuid::null();
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "raw.source.ambiguity"));
    const auto actionableIdentity =
        splitter->lfpDisplay
            ->channels[0]
            ->getStableChannelIdentity();
    const auto invalidIdentity =
        splitter->lfpDisplay
            ->channels[1]
            ->getStableChannelIdentity();
    ASSERT_NE (
        actionableIdentity,
        nullptr);
    ASSERT_NE (
        actionableIdentity
            ->getStableChannelKey(),
        nullptr);
    ASSERT_NE (
        invalidIdentity,
        nullptr);
    ASSERT_EQ (
        invalidIdentity
            ->getStableChannelKey(),
        nullptr);

    canvas
        ->loadCustomParametersFromXml (
            &savedRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        WriterOmitsNonCanonicalRecordsAndRoundTripsCanonicalUnavailableSentinels)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey = "   ";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "writer.canonical.identifier",
        803,
        0,
        "Writer Canonical",
        ContinuousChannel::Type::ADC);
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .uuid =
        Uuid();
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "   "));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);

    XmlElement whitespaceRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &whitespaceRoot);
    auto* whitespaceState =
        getIdentityTestVisibilityState (
            whitespaceRoot);
    ASSERT_NE (
        whitespaceState,
        nullptr);
    EXPECT_EQ (
        whitespaceState
            ->getNumChildElements(),
        0);

    showAllIdentityTestChannels (
        *splitter);
    buffers[0]->streamKey =
        "writer.canonical";
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .sourceNodeId =
        -2;
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .localIndex =
        0;
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "writer.canonical"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement invalidSentinelRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &invalidSentinelRoot);
    auto* invalidSentinelState =
        getIdentityTestVisibilityState (
            invalidSentinelRoot);
    ASSERT_NE (
        invalidSentinelState,
        nullptr);
    EXPECT_EQ (
        invalidSentinelState
            ->getNumChildElements(),
        0);

    showAllIdentityTestChannels (
        *splitter);
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .sourceNodeId =
        -1;
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .localIndex =
        -1;
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "writer.canonical"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement canonicalRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &canonicalRoot);
    auto* canonicalRecord =
        getIdentityTestHiddenRecord (
            canonicalRoot);
    ASSERT_NE (
        canonicalRecord,
        nullptr);
    EXPECT_EQ (
        canonicalRecord
            ->getIntAttribute (
                "source_node_id"),
        -1);
    EXPECT_EQ (
        canonicalRecord
            ->getIntAttribute (
                "local_index"),
        -1);

    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &canonicalRoot);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        V2ClosedSchemaRejectsForbiddenFieldsWhitespaceKeysAndBadAuxiliarySentinels)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "closed.schema";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "closed.schema.identifier",
        501,
        9,
        "Closed Schema",
        ContinuousChannel::Type::ELECTRODE);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "closed.schema"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement savedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &savedRoot);

    const char* const forbiddenAttributes[] = {
        "runtime_uuid",
        "runtime_stream_id",
        "global_index",
        "drawable_index",
        "screen_x"
    };
    for (const auto* forbidden :
         forbiddenAttributes)
    {
        XmlElement malformedRoot (
            savedRoot);
        auto* malformedRecord =
            getIdentityTestHiddenRecord (
                malformedRoot);
        ASSERT_NE (
            malformedRecord,
            nullptr);
        malformedRecord->setAttribute (
            forbidden,
            "forbidden");
        showAllIdentityTestChannels (
            *splitter);
        canvas
            ->loadCustomParametersFromXml (
                &malformedRoot);
        EXPECT_TRUE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    0))
            << forbidden;
    }

    XmlElement canonicalSentinelRoot (
        savedRoot);
    auto* canonicalSentinelRecord =
        getIdentityTestHiddenRecord (
            canonicalSentinelRoot);
    ASSERT_NE (
        canonicalSentinelRecord,
        nullptr);
    canonicalSentinelRecord->setAttribute (
        "source_node_id",
        -1);
    canonicalSentinelRecord->setAttribute (
        "local_index",
        -1);
    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &canonicalSentinelRoot);
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    XmlElement badSentinelRoot (
        savedRoot);
    auto* badSentinelRecord =
        getIdentityTestHiddenRecord (
            badSentinelRoot);
    ASSERT_NE (
        badSentinelRecord,
        nullptr);
    badSentinelRecord->setAttribute (
        "source_node_id",
        -2);
    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &badSentinelRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));

    XmlElement whitespaceRoot (
        savedRoot);
    auto* whitespacePane =
        getIdentityTestPane (
            whitespaceRoot);
    auto* whitespaceRecord =
        getIdentityTestHiddenRecord (
            whitespaceRoot);
    ASSERT_NE (
        whitespacePane,
        nullptr);
    ASSERT_NE (
        whitespaceRecord,
        nullptr);
    buffers[0]->streamKey = "   ";
    whitespacePane->setAttribute (
        "stream_key",
        "   ");
    whitespaceRecord->setAttribute (
        "stream_key",
        "   ");
    canvas->updateSettings();
    showAllIdentityTestChannels (
        *splitter);
    canvas
        ->loadCustomParametersFromXml (
            &whitespaceRoot);
    EXPECT_TRUE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        DuplicatePaneParentsAreAuthoritativeFailVisible)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_FALSE (buffers.isEmpty());
    buffers[0]->streamKey =
        "duplicate.parent";
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "duplicate.parent"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);

    XmlElement duplicateRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &duplicateRoot);
    auto* firstPane =
        getIdentityTestPane (
            duplicateRoot);
    ASSERT_NE (
        firstPane,
        nullptr);
    auto* validDuplicate =
        new XmlElement (
            *firstPane);
    auto* firstState =
        firstPane->getChildByName (
            "WAVEFORM_VISIBILITY_STATE");
    ASSERT_NE (
        firstState,
        nullptr);
    firstState->deleteAllChildElements();
    duplicateRoot.addChildElement (
        validDuplicate);

    canvas
        ->loadCustomParametersFromXml (
            &duplicateRoot);
    EXPECT_FALSE (
        splitter->isLoading);
    for (int channel = 0;
         channel
         < splitter->lfpDisplay
               ->getNumChannels();
         ++channel)
    {
        EXPECT_TRUE (
            splitter->lfpDisplay
                ->getStoredChannelVisibility (
                    channel));
    }
}

TEST_F (LfpStableChannelIdentityBindingTests,
        FullCanvasFreshRuntimeRestoresTheRealEnableButtonAndShowingRemovesTheRecord)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "fresh.runtime";
    configureIdentityPersistenceChannel (
        *buffers[0],
        0,
        "fresh.runtime.identifier",
        701,
        10,
        "Fresh Runtime",
        ContinuousChannel::Type::AUX);
    canvas->updateSettings();
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "fresh.runtime"));
    auto* enableButton =
        getIdentityTestEnableButton (
            *splitters[0]
                 ->lfpDisplay
                 ->channelInfo[0]);
    ASSERT_NE (
        enableButton,
        nullptr);
    enableButton->triggerClick();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            20);
    ASSERT_FALSE (
        enableButton
            ->getToggleState());

    XmlElement savedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &savedRoot);
    ASSERT_NE (
        getIdentityTestHiddenRecord (
            savedRoot),
        nullptr);
    canvas.reset();
    buffers[0]
        ->channelMetadata
        .getReference (0)
        .uuid =
        Uuid();
    buffers[0]
        ->channelMetadata
        .swap (
            0,
            1);

    auto restoredCanvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    restoredCanvas
        ->loadCustomParametersFromXml (
            &savedRoot);
    splitters =
        getIdentityTestSplitters (
            *restoredCanvas);
    ASSERT_FALSE (splitters.empty());
    ASSERT_TRUE (
        splitters[0]
            ->selectStreamByKey (
                "fresh.runtime"));
    ASSERT_FALSE (
        splitters[0]
            ->lfpDisplay
            ->getStoredChannelVisibility (
                1));
    enableButton =
        getIdentityTestEnableButton (
            *splitters[0]
                 ->lfpDisplay
                 ->channelInfo[1]);
    ASSERT_NE (
        enableButton,
        nullptr);
    ASSERT_FALSE (
        enableButton
            ->getToggleState());

    enableButton->triggerClick();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            20);
    ASSERT_TRUE (
        enableButton
            ->getToggleState());
    XmlElement shownRoot (
        "ROOT");
    restoredCanvas
        ->saveCustomParametersToXml (
            &shownRoot);
    auto* shownState =
        getIdentityTestVisibilityState (
            shownRoot);
    ASSERT_NE (
        shownState,
        nullptr);
    EXPECT_EQ (
        shownState
            ->getNumChildElements(),
        0);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        EquivalentMutationOrdersAndRepeatedFullSavesProduceExactV2Xml)
{
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[0]->streamKey =
        "deterministic.z";
    buffers[1]->streamKey =
        "deterministic.a";
    canvas->updateSettings();

    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "deterministic.z"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            1,
            true);
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "deterministic.a"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    XmlElement firstRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &firstRoot);
    auto* firstState =
        getIdentityTestVisibilityState (
            firstRoot);
    ASSERT_NE (
        firstState,
        nullptr);
    const auto firstV2 =
        firstState->toString();

    for (const auto* buffer :
         buffers)
    {
        ASSERT_TRUE (
            splitter->selectStreamByKey (
                buffer->streamKey));
        showAllIdentityTestChannels (
            *splitter);
    }
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "deterministic.a"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            0,
            true);
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "deterministic.z"));
    splitter->lfpDisplay
        ->setEnabledState (
            false,
            1,
            true);

    XmlElement secondRoot (
        "ROOT");
    XmlElement repeatedRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &secondRoot);
    canvas
        ->saveCustomParametersToXml (
            &repeatedRoot);
    auto* secondState =
        getIdentityTestVisibilityState (
            secondRoot);
    auto* repeatedState =
        getIdentityTestVisibilityState (
            repeatedRoot);
    ASSERT_NE (
        secondState,
        nullptr);
    ASSERT_NE (
        repeatedState,
        nullptr);
    EXPECT_EQ (
        secondState->toString(),
        firstV2);
    EXPECT_EQ (
        repeatedState->toString(),
        firstV2);
}

TEST_F (LfpStableChannelIdentityBindingTests,
        PersistedChannelTypeValidationAcceptsEveryDefinedEnumValueOnly)
{
    EXPECT_TRUE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                0));
    EXPECT_TRUE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                1));
    EXPECT_TRUE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                2));
    EXPECT_TRUE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                100));
    EXPECT_FALSE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                -1));
    EXPECT_FALSE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                3));
    EXPECT_FALSE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                99));
    EXPECT_FALSE (
        LfpDisplay::
            isPersistedChannelTypeValueAcceptedForTests (
                101));
}

TEST_F (LfpStableChannelIdentityBindingTests,
        TwelveThousandHiddenRecordsRestoreWithLinearResolutionWork)
{
    constexpr int channelCount =
        12000;
    auto canvas =
        createIdentityCanvas (
            SplitLayouts::SINGLE);
    const auto splitters =
        getIdentityTestSplitters (
            *canvas);
    ASSERT_FALSE (splitters.empty());
    auto* splitter =
        splitters[0];
    const auto buffers =
        processor->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    auto* buffer =
        buffers[0];
    buffers[1]->numChannels = 0;
    buffer->streamKey =
        "large.restore";
    for (int channel =
             buffer->channelMetadata
                 .size();
         channel < channelCount;
         ++channel)
    {
        buffer->channelMetadata.add (
            makeIdentityMetadata (
                "large.restore."
                    + String (
                        channel),
                601,
                channel));
    }
    buffer->numChannels =
        channelCount;
    canvas->updateSettings();
    ASSERT_TRUE (
        splitter->selectStreamByKey (
            "large.restore"));
    ASSERT_EQ (
        splitter->lfpDisplay
            ->getNumChannels(),
        channelCount);

    XmlElement restoreRoot (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &restoreRoot);
    auto* state =
        getIdentityTestVisibilityState (
            restoreRoot);
    ASSERT_NE (
        state,
        nullptr);
    state->deleteAllChildElements();
    for (int channel = 0;
         channel < channelCount;
         ++channel)
    {
        const auto& metadata =
            buffer->channelMetadata[
                channel];
        auto* record =
            state->createNewChildElement (
                "HIDDEN_CHANNEL");
        record->setAttribute (
            "stream_key",
            "large.restore");
        record->setAttribute (
            "stable_key_kind",
            "identifier");
        record->setAttribute (
            "identifier",
            metadata.identifier);
        record->setAttribute (
            "source_node_id",
            601);
        record->setAttribute (
            "local_index",
            channel);
        record->setAttribute (
            "channel_name",
            metadata.name);
        record->setAttribute (
            "channel_type",
            0);
    }

    splitter->lfpDisplay
        ->resetWaveformVisibilityResolutionWorkForTests();
    canvas
        ->loadCustomParametersFromXml (
            &restoreRoot);

    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                0));
    EXPECT_FALSE (
        splitter->lfpDisplay
            ->getStoredChannelVisibility (
                channelCount - 1));
    EXPECT_LE (
        splitter->lfpDisplay
            ->getWaveformVisibilityResolutionWorkForTests(),
        static_cast<uint64> (
            channelCount * 4));
}
} // namespace
