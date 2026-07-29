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
#include "../LfpStableChannelIdentity.h"
#include <ModelProcessors.h>
#include <TestFixtures.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace
{
using namespace LfpViewer;

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

    std::unique_ptr<ProcessorTester> tester;
    LfpDisplayNode* processor = nullptr;
};

TEST_F (LfpStableChannelIdentityBindingTests,
        PaneAndSelectedStreamAreBoundToDisplayAndInfo)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::TWO_VERT,
        false);
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
        SettingsReplacementAndRemovalInvalidateOldBindings)
{
    auto canvas = std::make_unique<LfpDisplayCanvas> (
        processor,
        SplitLayouts::SINGLE,
        false);
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
    auto splitters =
        getIdentityTestSplitters (*canvas);
    ASSERT_FALSE (splitters.empty());
    auto* display =
        splitters[0]
            ->lfpDisplay.get();
    ASSERT_GT (display->channels.size(), 0);
    std::weak_ptr<
        const LfpStableChannelIdentity>
        oldIdentity =
            display->channels[0]
                ->getStableChannelIdentity();
    ASSERT_FALSE (oldIdentity.expired());

    display->setNumChannels (0);

    EXPECT_TRUE (oldIdentity.expired());
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
    display->setChannelDisplaySkipAmount (2);
    ASSERT_FALSE (
        display->drawableChannels
            .isEmpty());
    display->toggleSingleChannel (
        display->drawableChannels[0]);
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
}
} // namespace
