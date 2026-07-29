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
#include "LfpDisplayCanvas.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace LfpViewer
{
namespace
{
std::atomic<uint64> nextIdentityGeneration {
    1
};
constexpr auto stableChannelDiagnosticTimeout =
    std::chrono::milliseconds (100);

class StableChannelDiagnosticCompletion final
{
public:
    bool tryBeginCallback() noexcept
    {
        const std::lock_guard<std::mutex>
            lock (mutex);
        if (cancelled
            || callbackClaimed)
        {
            return false;
        }

        callbackClaimed = true;
        return true;
    }

    void complete (bool value) noexcept
    {
        {
            const std::lock_guard<std::mutex>
                lock (mutex);
            if (cancelled
                || ! callbackClaimed
                || completed)
            {
                return;
            }

            result = value;
            completed = true;
        }
        completionChanged.notify_one();
    }

    void cancel() noexcept
    {
        const std::lock_guard<std::mutex>
            lock (mutex);
        cancelled = true;
    }

    bool waitForResult (
        std::chrono::milliseconds timeout) noexcept
    {
        std::unique_lock<std::mutex>
            lock (mutex);
        if (! completionChanged.wait_for (
                lock,
                timeout,
                [this]
                {
                    return completed;
                }))
        {
            cancelled = true;
            return false;
        }

        return result;
    }

    bool waitForMutationResult (
        std::chrono::milliseconds timeout) noexcept
    {
        std::unique_lock<std::mutex>
            lock (mutex);
        if (completionChanged.wait_for (
                lock,
                timeout,
                [this]
                {
                    return completed;
                }))
        {
            return result;
        }

        if (! callbackClaimed)
        {
            cancelled = true;
            return false;
        }

        completionChanged.wait (
            lock,
            [this]
            {
                return completed;
            });
        return result;
    }

private:
    std::mutex mutex;
    std::condition_variable completionChanged;
    bool callbackClaimed = false;
    bool cancelled = false;
    bool completed = false;
    bool result = false;
};

#if BUILD_TESTS
std::mutex stableChannelDiagnosticTestMutex;
LfpStableChannelDiagnosticDispatcherForTests
    stableChannelDiagnosticDispatcherForTests;
std::function<void()>
    stableChannelDiagnosticValidationHookForTests;
#endif

bool dispatchStableChannelDiagnostic (
    std::function<void()> callback)
{
#if BUILD_TESTS
    LfpStableChannelDiagnosticDispatcherForTests
        testDispatcher;
    {
        const std::lock_guard<std::mutex>
            lock (
                stableChannelDiagnosticTestMutex);
        testDispatcher =
            stableChannelDiagnosticDispatcherForTests;
    }
    if (testDispatcher)
    {
        return testDispatcher (
            std::move (
                callback));
    }
#endif

    return MessageManager::callAsync (
        std::move (
            callback));
}

void notifyStableChannelDiagnosticValidationForTests()
{
#if BUILD_TESTS
    std::function<void()> testHook;
    {
        const std::lock_guard<std::mutex>
            lock (
                stableChannelDiagnosticTestMutex);
        testHook =
            stableChannelDiagnosticValidationHookForTests;
    }
    if (testHook)
        testHook();
#endif
}
}

#if BUILD_TESTS
void setStableChannelDiagnosticDispatcherForTests (
    LfpStableChannelDiagnosticDispatcherForTests
        dispatcher)
{
    const std::lock_guard<std::mutex>
        lock (
            stableChannelDiagnosticTestMutex);
    stableChannelDiagnosticDispatcherForTests =
        std::move (
            dispatcher);
}

void setStableChannelDiagnosticValidationHookForTests (
    std::function<void()> hook)
{
    const std::lock_guard<std::mutex>
        lock (
            stableChannelDiagnosticTestMutex);
    stableChannelDiagnosticValidationHookForTests =
        std::move (
            hook);
}
#endif

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
        Uuid runtimeUuid_,
        String persistedIdentifier_,
        int persistedSourceNodeId_,
        int persistedLocalIndex_,
        String persistedChannelName_,
        ContinuousChannel::Type
            persistedChannelType_)
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
      persistedIdentifier (
          std::move (
              persistedIdentifier_)),
      persistedSourceNodeId (
          persistedSourceNodeId_),
      persistedLocalIndex (
          persistedLocalIndex_),
      persistedChannelName (
          std::move (
              persistedChannelName_)),
      persistedChannelType (
          persistedChannelType_),
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
            runtimeUuid,
            persistedIdentifier,
            persistedSourceNodeId,
            persistedLocalIndex,
            persistedChannelName,
            persistedChannelType));
}

class LfpStableChannelActionOwnerState final
{
public:
    explicit LfpStableChannelActionOwnerState (
        LfpDisplayCanvas* owner_)
        : owner (owner_)
    {
    }

private:
    friend class LfpStableChannelActionRequest;

    LfpDisplayCanvas* owner;
};

std::shared_ptr<
    LfpStableChannelActionOwnerState>
LfpStableChannelActionRequest::
    createOwnerState (
        LfpDisplayCanvas* owner)
{
    jassert (
        MessageManager::
            existsAndIsCurrentThread());
    if (owner == nullptr
        || ! MessageManager::
                 existsAndIsCurrentThread())
    {
        return {};
    }

    return std::make_shared<
        LfpStableChannelActionOwnerState> (
        owner);
}

void LfpStableChannelActionRequest::
    retireOwnerState (
        std::shared_ptr<
            LfpStableChannelActionOwnerState>&
            ownerState) noexcept
{
    jassert (
        MessageManager::
            existsAndIsCurrentThread());
    if (ownerState != nullptr)
        ownerState->owner = nullptr;
    ownerState.reset();
}

LfpStableChannelActionRequest::
    LfpStableChannelActionRequest (
        std::weak_ptr<
            const LfpStableChannelActionOwnerState>
            ownerState_,
        std::weak_ptr<
            const LfpStableChannelIdentityBindingSlot>
            ownerPublication_,
        std::shared_ptr<
            const LfpStableChannelIdentity>
            retainedIdentity_)
    : ownerState (
          std::move (
              ownerState_)),
      ownerPublication (
          std::move (
              ownerPublication_)),
      retainedIdentity (
          std::move (
              retainedIdentity_))
{
}

bool LfpStableChannelActionRequest::
    validateCurrentAndAvailable() const
{
    const auto retainedOwnerState =
        ownerState;
    const auto retainedOwnerPublication =
        ownerPublication;
    const auto retained =
        retainedIdentity;
    const auto validateOnOwnerThread =
        [retainedOwnerState,
         retainedOwnerPublication,
         retained]
        {
            jassert (
                MessageManager::
                    existsAndIsCurrentThread());
            const auto liveOwnerState =
                retainedOwnerState.lock();
            const auto liveOwnerPublication =
                retainedOwnerPublication.lock();
            if (liveOwnerState == nullptr
                || liveOwnerPublication
                       == nullptr
                || liveOwnerState->owner
                       == nullptr
                || ! liveOwnerPublication
                        ->isCurrentAndAvailable (
                            retained))
            {
                return false;
            }

            notifyStableChannelDiagnosticValidationForTests();
            return liveOwnerState
                ->owner
                ->validateStableChannelAction (
                    retained);
        };

    auto* messageManager =
        MessageManager::
            getInstanceWithoutCreating();
    if (messageManager == nullptr)
        return false;

    if (messageManager
            ->isThisTheMessageThread())
    {
        return validateOnOwnerThread();
    }

    auto completion =
        std::make_shared<
            StableChannelDiagnosticCompletion>();
    const auto posted =
        dispatchStableChannelDiagnostic (
            [validateOnOwnerThread,
             completion]
            {
                if (! completion
                          ->tryBeginCallback())
                {
                    return;
                }

                completion->complete (
                    validateOnOwnerThread());
            });
    if (! posted)
    {
        completion->cancel();
        return false;
    }

    return completion->waitForResult (
        stableChannelDiagnosticTimeout);
}

bool LfpStableChannelActionRequest::
    requestWaveformVisibility (
        LfpWaveformVisibility visibility) const
{
    const auto retainedOwnerState =
        ownerState;
    const auto retainedOwnerPublication =
        ownerPublication;
    const auto retained =
        retainedIdentity;
    const auto performOnOwnerThread =
        [retainedOwnerState,
         retainedOwnerPublication,
         retained,
         visibility]
        {
            jassert (
                MessageManager::
                    existsAndIsCurrentThread());
            const auto liveOwnerState =
                retainedOwnerState.lock();
            const auto liveOwnerPublication =
                retainedOwnerPublication.lock();
            if (liveOwnerState == nullptr
                || liveOwnerPublication
                       == nullptr
                || liveOwnerState->owner
                       == nullptr
                || ! liveOwnerPublication
                        ->isCurrentAndAvailable (
                            retained))
            {
                return false;
            }

            return liveOwnerState
                ->owner
                ->requestStableChannelVisibility (
                    retained,
                    visibility);
        };

    auto* messageManager =
        MessageManager::
            getInstanceWithoutCreating();
    if (messageManager == nullptr)
        return false;

    if (messageManager
            ->isThisTheMessageThread())
    {
        return performOnOwnerThread();
    }

    auto completion =
        std::make_shared<
            StableChannelDiagnosticCompletion>();
    const auto posted =
        dispatchStableChannelDiagnostic (
            [performOnOwnerThread,
             completion]
            {
                if (! completion
                          ->tryBeginCallback())
                {
                    return;
                }

                bool result = false;
                try
                {
                    result =
                        performOnOwnerThread();
                }
                catch (...)
                {
                }
                completion->complete (
                    result);
            });
    if (! posted)
    {
        completion->cancel();
        return false;
    }

    return completion
        ->waitForMutationResult (
        stableChannelDiagnosticTimeout);
}

struct LfpStableChannelIdentityBindingSlot::
    Publication final
{
    Publication (
        std::shared_ptr<
            const LfpStableChannelIdentity>
            identity_,
        std::shared_ptr<
            const LfpStableChannelActionRequest>
            actionRequest_)
        : identity (
              std::move (
                  identity_)),
          actionRequest (
              std::move (
                  actionRequest_))
    {
    }

    const std::shared_ptr<
        const LfpStableChannelIdentity>
        identity;
    const std::shared_ptr<
        const LfpStableChannelActionRequest>
        actionRequest;
};

std::shared_ptr<
    const LfpStableChannelIdentity>
LfpStableChannelIdentityBindingSlot::
    get() const noexcept
{
    const auto publication =
        std::atomic_load_explicit (
            &current,
            std::memory_order_acquire);
    return publication != nullptr
        ? publication->identity
        : nullptr;
}

std::shared_ptr<
    const LfpStableChannelActionRequest>
LfpStableChannelIdentityBindingSlot::
    getActionRequest() const noexcept
{
    const auto publication =
        std::atomic_load_explicit (
            &current,
            std::memory_order_acquire);
    return publication != nullptr
        ? publication->actionRequest
        : nullptr;
}

bool LfpStableChannelIdentityBindingSlot::
    isCurrentAndAvailable (
        const std::shared_ptr<
            const LfpStableChannelIdentity>&
            retainedIdentity) const noexcept
{
    const auto publication =
        std::atomic_load_explicit (
        &current,
        std::memory_order_acquire);
    return publication != nullptr
        && publication->identity
               == retainedIdentity
        && retainedIdentity != nullptr
        && retainedIdentity
               ->isAgentActionable();
}

void LfpStableChannelIdentityBindingSlot::
    revoke() noexcept
{
    const auto publication =
        std::atomic_load_explicit (
            &current,
            std::memory_order_acquire);
    if (publication != nullptr
        && publication->identity
               != nullptr)
    {
        publication
            ->identity
            ->revokeAgentActionability();
    }
    std::atomic_store_explicit (
        &current,
        std::shared_ptr<
            const Publication>(),
        std::memory_order_release);
}

void LfpStableChannelIdentityBindingSlot::
    publishSuccessor (
        const std::shared_ptr<
            const LfpStableChannelIdentity>&
            blueprint,
        std::weak_ptr<
            const LfpStableChannelActionOwnerState>
            ownerState)
{
    revoke();
    jassert (
        MessageManager::
            existsAndIsCurrentThread());
    const auto self =
        weak_from_this();
    if (blueprint == nullptr
        || ownerState.expired()
        || self.expired()
        || ! MessageManager::
                 existsAndIsCurrentThread())
    {
        return;
    }

    const auto successor =
        blueprint
            ->createSuccessorGeneration();
    const auto request =
        std::shared_ptr<
            const LfpStableChannelActionRequest> (
            new LfpStableChannelActionRequest (
                std::move (
                    ownerState),
                self,
                successor));
    const auto publication =
        std::make_shared<
            const Publication> (
            successor,
            request);
    std::atomic_store_explicit (
        &current,
        publication,
        std::memory_order_release);
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
                    runtimeUuid,
                    metadata.identifier,
                    metadata.sourceNodeId,
                    metadata.localIndex,
                    metadata.name,
                    metadata.type)));
    }

    return identities;
}
} // namespace LfpViewer
