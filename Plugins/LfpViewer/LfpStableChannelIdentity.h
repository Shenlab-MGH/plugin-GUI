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

#pragma once

#include <ProcessorHeaders.h>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace LfpViewer
{
class DisplayBuffer;
class LfpStableChannelIdentity;

/**
    Resolves copied channel snapshots against the complete current stream set.

    The returned vector follows the selected buffer's metadata order and never
    retains pointers to the buffer or its metadata.
 */
TESTABLE std::vector<
    std::shared_ptr<
        const LfpStableChannelIdentity>>
resolveStableChannelIdentities (
    int paneIndex,
    const DisplayBuffer& selectedStream,
    const Array<DisplayBuffer*>& currentStreams);

/** A typed stable key for one continuous channel within a stable stream. */
class LfpStableChannelKey final
{
public:
    enum class Kind
    {
        identifier,
        sourceAndLocalIndex
    };

    Kind getKind() const noexcept { return kind; }
    const String& getIdentifier() const noexcept { return identifier; }
    int getSourceNodeId() const noexcept { return sourceNodeId; }
    int getLocalIndex() const noexcept { return localIndex; }

    bool operator== (
        const LfpStableChannelKey& other) const noexcept;

private:
    friend TESTABLE std::vector<
        std::shared_ptr<
            const class LfpStableChannelIdentity>>
    resolveStableChannelIdentities (
        int,
        const DisplayBuffer&,
        const Array<DisplayBuffer*>&);

    explicit LfpStableChannelKey (
        String exactIdentifier);
    LfpStableChannelKey (
        int sourceNodeId,
        int localIndex);

    Kind kind;
    String identifier;
    int sourceNodeId = -1;
    int localIndex = -1;
};

/**
    Immutable copied identity bound to one LFP channel component generation.

    A null stable key marks a deliberately non-actionable snapshot.
 */
class LfpStableChannelIdentity final
{
public:
    int getPaneIndex() const noexcept { return paneIndex; }
    const String& getStreamKey() const noexcept { return streamKey; }
    TESTABLE const LfpStableChannelKey*
    getStableChannelKey() const noexcept;
    const Uuid& getRuntimeUuid() const noexcept { return runtimeUuid; }

    bool isAgentActionable() const noexcept;

    /** Permanently revokes this component generation. */
    void revokeAgentActionability() const noexcept;

    /** Copies the stable values into a fresh component generation. */
    std::shared_ptr<
        const LfpStableChannelIdentity>
    createSuccessorGeneration() const;

private:
    friend TESTABLE std::vector<
        std::shared_ptr<
            const LfpStableChannelIdentity>>
    resolveStableChannelIdentities (
        int,
        const DisplayBuffer&,
        const Array<DisplayBuffer*>&);

    LfpStableChannelIdentity (
        int paneIndex,
        String streamKey,
        std::optional<LfpStableChannelKey>
            stableChannelKey,
        Uuid runtimeUuid);

    struct GenerationState
    {
        explicit GenerationState (
            uint64 generation_) noexcept
            : generation (
                  generation_)
        {
        }

        std::atomic<bool> active {
            true
        };
        const uint64 generation;
    };

    const int paneIndex;
    const String streamKey;
    const std::optional<
        LfpStableChannelKey>
        stableChannelKey;
    const Uuid runtimeUuid;
    const std::shared_ptr<
        GenerationState>
        generationState;
};

inline bool LfpStableChannelIdentity::
    isAgentActionable() const noexcept
{
    return stableChannelKey.has_value()
        && runtimeUuid != Uuid::null()
        && generationState->active.load (
            std::memory_order_acquire);
}

} // namespace LfpViewer
