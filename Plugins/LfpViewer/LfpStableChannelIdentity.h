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
class LfpDisplayCanvas;
class LfpStableChannelIdentity;
class LfpStableChannelActionRequest;
class LfpStableChannelActionOwnerState;
class LfpStableChannelIdentityBindingSlot;

/** The only value payload accepted by stable waveform visibility requests. */
enum class LfpWaveformVisibility
{
    visible,
    hidden
};

#if BUILD_TESTS
using LfpStableChannelDiagnosticDispatcherForTests =
    std::function<bool (
        std::function<void()>)>;

TESTABLE void
setStableChannelDiagnosticDispatcherForTests (
    LfpStableChannelDiagnosticDispatcherForTests
        dispatcher);
TESTABLE void
setStableChannelDiagnosticValidationHookForTests (
    std::function<void()> hook);
#endif

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

    /**
        Returns whether this immutable generation token is structurally valid
        and has not been revoked.

        This is a necessary precondition only. It is never sufficient
        authorization for an agent mutation; live component availability must
        also be revalidated by the owner of a published
        LfpStableChannelActionRequest.
     */
    bool isAgentActionable() const noexcept;

private:
    friend class LfpDisplay;
    friend class LfpStableChannelIdentityBindingSlot;

    /** Permanently revokes this component generation. */
    void revokeAgentActionability() const noexcept;

    /** Copies the stable values into a fresh component generation. */
    std::shared_ptr<
        const LfpStableChannelIdentity>
    createSuccessorGeneration() const;

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

/**
    An immutable, owner-published diagnostic for one channel generation.

    This object never accepts a callback or target pointer and does not
    authorize mutation. Future action APIs must pass a narrow descriptor and
    value payload to the owner, which must re-resolve, revalidate, and directly
    dispatch the action in one message-thread handler.
 */
class LfpStableChannelActionRequest final
{
public:
    /**
        Reports whether the owner still considers this exact generation
        current and available.

        This is diagnostic only and must never be used as mutation
        authorization.
     */
    TESTABLE bool validateCurrentAndAvailable() const;

    /**
        Requests one owner-controlled waveform visibility mutation.

        The owner revalidates and applies this value in one message-thread
        turn; this object never exposes mutation authorization.
     */
    TESTABLE bool requestWaveformVisibility (
        LfpWaveformVisibility visibility) const;

private:
    friend class LfpDisplayCanvas;
    friend class LfpStableChannelIdentityBindingSlot;

    static std::shared_ptr<
        LfpStableChannelActionOwnerState>
    createOwnerState (
        LfpDisplayCanvas* owner);
    static void retireOwnerState (
        std::shared_ptr<
            LfpStableChannelActionOwnerState>&
            ownerState) noexcept;

    LfpStableChannelActionRequest (
        std::weak_ptr<
            const LfpStableChannelActionOwnerState>
            ownerState,
        std::weak_ptr<
            const LfpStableChannelIdentityBindingSlot>
            ownerPublication,
        std::shared_ptr<
            const LfpStableChannelIdentity>
            retainedIdentity);

    const std::weak_ptr<
        const LfpStableChannelActionOwnerState>
        ownerState;
    const std::weak_ptr<
        const LfpStableChannelIdentityBindingSlot>
        ownerPublication;
    const std::shared_ptr<
        const LfpStableChannelIdentity>
        retainedIdentity;
};

/**
    One atomic publication point shared by a channel and its info component.

    Mutation is restricted to LfpDisplay; providers can only load snapshots.
 */
class LfpStableChannelIdentityBindingSlot final
    : public std::enable_shared_from_this<
          LfpStableChannelIdentityBindingSlot>
{
public:
    std::shared_ptr<
        const LfpStableChannelIdentity>
    get() const noexcept;
    std::shared_ptr<
        const LfpStableChannelActionRequest>
    getActionRequest() const noexcept;

private:
    friend class LfpDisplay;
    friend class LfpStableChannelActionRequest;

    void revoke() noexcept;
    void publishSuccessor (
        const std::shared_ptr<
            const LfpStableChannelIdentity>&
            blueprint,
        std::weak_ptr<
            const LfpStableChannelActionOwnerState>
            ownerState);
    bool isCurrentAndAvailable (
        const std::shared_ptr<
            const LfpStableChannelIdentity>&
            retainedIdentity) const noexcept;

    struct Publication;

    std::shared_ptr<
        const Publication>
        current;
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
