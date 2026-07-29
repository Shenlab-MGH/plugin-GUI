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

#include "LfpStableChannelIdentity.h"
#include "DisplayBuffer.h"

#include <unordered_map>
#include <unordered_set>

namespace LfpViewer
{
namespace
{
std::atomic<uint64> nextIdentityGeneration {
    1
};
}

LfpStableChannelKey::LfpStableChannelKey (
    String exactIdentifier)
    : kind (Kind::identifier),
      identifier (
          std::move (
              exactIdentifier))
{
}

LfpStableChannelKey::LfpStableChannelKey (
    int sourceNodeId_,
    int localIndex_)
    : kind (
          Kind::
              sourceAndLocalIndex),
      sourceNodeId (
          sourceNodeId_),
      localIndex (
          localIndex_)
{
}

bool LfpStableChannelKey::operator== (
    const LfpStableChannelKey& other) const noexcept
{
    return kind == other.kind
        && identifier == other.identifier
        && sourceNodeId == other.sourceNodeId
        && localIndex == other.localIndex;
}

LfpStableChannelIdentity::
    LfpStableChannelIdentity (
        int paneIndex_,
        String streamKey_,
        std::optional<
            LfpStableChannelKey>
            stableChannelKey_,
        Uuid runtimeUuid_)
    : paneIndex (
          paneIndex_),
      streamKey (
          std::move (
              streamKey_)),
      stableChannelKey (
          std::move (
              stableChannelKey_)),
      runtimeUuid (
          std::move (
              runtimeUuid_)),
      generationState (
          std::make_shared<
              GenerationState> (
              nextIdentityGeneration
                  .fetch_add (
                      1,
                      std::memory_order_relaxed)))
{
}

void LfpStableChannelIdentity::
    revokeAgentActionability() const noexcept
{
    generationState->active.store (
        false,
        std::memory_order_release);
}

std::shared_ptr<
    const LfpStableChannelIdentity>
LfpStableChannelIdentity::
    createSuccessorGeneration() const
{
    return std::shared_ptr<
        const LfpStableChannelIdentity> (
        new LfpStableChannelIdentity (
            paneIndex,
            streamKey,
            stableChannelKey,
            runtimeUuid));
}

const LfpStableChannelKey*
LfpStableChannelIdentity::
    getStableChannelKey() const noexcept
{
    return stableChannelKey
               .has_value()
        ? &*stableChannelKey
        : nullptr;
}

namespace
{
bool hasValidStreamSet (
    const DisplayBuffer& selectedStream,
    const Array<DisplayBuffer*>&
        currentStreams)
{
    int selectedPointerCount = 0;
    std::unordered_set<
        std::string>
        streamKeys;
    for (const auto* stream :
         currentStreams)
    {
        if (stream == nullptr)
            return false;
        if (stream
            == &selectedStream)
        {
            ++selectedPointerCount;
        }
        const auto streamKey =
            stream->streamKey
                .toStdString();
        if (streamKey.empty()
            || ! streamKeys
                     .insert (
                         streamKey)
                     .second)
        {
            return false;
        }
    }

    return selectedPointerCount == 1;
}

uint64 makeSourceLocalKey (
    int sourceNodeId,
    int localIndex) noexcept
{
    return (static_cast<uint64> (
                static_cast<uint32> (
                    sourceNodeId))
            << 32)
        | static_cast<uint32> (
            localIndex);
}
} // namespace

std::vector<
    std::shared_ptr<
        const LfpStableChannelIdentity>>
resolveStableChannelIdentities (
    int paneIndex,
    const DisplayBuffer& selectedStream,
    const Array<DisplayBuffer*>&
        currentStreams)
{
    std::vector<
        std::shared_ptr<
            const LfpStableChannelIdentity>>
        identities;
    identities.reserve (
        static_cast<size_t> (
            selectedStream
                .channelMetadata
                .size()));

    const bool streamIsValid =
        hasValidStreamSet (
            selectedStream,
            currentStreams);

    std::unordered_map<
        std::string,
        int>
        identifierCounts;
    std::unordered_map<
        std::string,
        int>
        runtimeUuidCounts;
    std::unordered_map<
        uint64,
        int>
        sourceLocalCounts;
    const auto metadataCount =
        static_cast<size_t> (
            selectedStream
                .channelMetadata
                .size());
    identifierCounts.reserve (
        metadataCount);
    runtimeUuidCounts.reserve (
        metadataCount);
    sourceLocalCounts.reserve (
        metadataCount);
    for (const auto& metadata :
         selectedStream
             .channelMetadata)
    {
        ++identifierCounts[
            metadata.identifier
                .toStdString()];
        ++runtimeUuidCounts[
            metadata.uuid
                .toString()
                .toStdString()];
        ++sourceLocalCounts[
            makeSourceLocalKey (
                metadata.sourceNodeId,
                metadata.localIndex)];
    }

    for (const auto& metadata :
         selectedStream
             .channelMetadata)
    {
        std::optional<
            LfpStableChannelKey>
            stableKey;

        if (streamIsValid
            && metadata.uuid
                   != Uuid::null()
            && runtimeUuidCounts[
                   metadata.uuid
                       .toString()
                       .toStdString()]
                   == 1)
        {
            const bool identifierIsPresent =
                metadata
                    .identifier
                    .trim()
                    .isNotEmpty();
            if (identifierIsPresent
                && identifierCounts[
                       metadata.identifier
                           .toStdString()]
                       == 1)
            {
                stableKey =
                    LfpStableChannelKey (
                        metadata
                            .identifier);
            }
            else if (
                metadata.sourceNodeId
                    >= 0
                && metadata.localIndex
                       >= 0
                && sourceLocalCounts[
                       makeSourceLocalKey (
                           metadata.sourceNodeId,
                           metadata.localIndex)]
                       == 1)
            {
                stableKey =
                    LfpStableChannelKey (
                        metadata
                            .sourceNodeId,
                        metadata
                            .localIndex);
            }
        }

        const auto runtimeUuid =
            stableKey.has_value()
            ? metadata.uuid
            : Uuid::null();
        identities.emplace_back (
            std::shared_ptr<
                const LfpStableChannelIdentity> (
                new LfpStableChannelIdentity (
                    paneIndex,
                    selectedStream
                        .streamKey,
                    std::move (
                        stableKey),
                    runtimeUuid)));
    }

    return identities;
}
} // namespace LfpViewer
