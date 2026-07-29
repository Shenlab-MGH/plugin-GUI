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

namespace LfpViewer
{
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
              runtimeUuid_))
{
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
    StringArray streamKeys;
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
        if (stream
                ->streamKey
                .isEmpty()
            || streamKeys.contains (
                stream
                    ->streamKey))
        {
            return false;
        }
        streamKeys.add (
            stream
                ->streamKey);
    }

    return selectedPointerCount == 1;
}

int countExactIdentifier (
    StringRef identifier,
    const Array<
        DisplayBuffer::
            ChannelMetadata>& metadata)
{
    int count = 0;
    for (const auto& candidate :
         metadata)
    {
        if (candidate.identifier
            == identifier)
        {
            ++count;
        }
    }
    return count;
}

int countSourceLocalPair (
    int sourceNodeId,
    int localIndex,
    const Array<
        DisplayBuffer::
            ChannelMetadata>& metadata)
{
    int count = 0;
    for (const auto& candidate :
         metadata)
    {
        if (candidate.sourceNodeId
                == sourceNodeId
            && candidate.localIndex
                == localIndex)
        {
            ++count;
        }
    }
    return count;
}

int countRuntimeUuid (
    const Uuid& uuid,
    const Array<
        DisplayBuffer::
            ChannelMetadata>& metadata)
{
    int count = 0;
    for (const auto& candidate :
         metadata)
    {
        if (candidate.uuid
            == uuid)
        {
            ++count;
        }
    }
    return count;
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
            && countRuntimeUuid (
                   metadata.uuid,
                   selectedStream
                       .channelMetadata)
                   == 1)
        {
            const bool identifierIsPresent =
                metadata
                    .identifier
                    .trim()
                    .isNotEmpty();
            if (identifierIsPresent
                && countExactIdentifier (
                       metadata.identifier,
                       selectedStream
                           .channelMetadata)
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
                && countSourceLocalPair (
                       metadata
                           .sourceNodeId,
                       metadata
                           .localIndex,
                       selectedStream
                           .channelMetadata)
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
