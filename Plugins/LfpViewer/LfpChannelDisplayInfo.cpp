/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "LfpChannelDisplayInfo.h"
#include "EventDisplayInterface.h"
#include "LfpBitmapPlotter.h"
#include "LfpBitmapPlotterInfo.h"
#include "LfpChannelDisplay.h"
#include "LfpDisplay.h"
#include "LfpDisplayCanvas.h"
#include "LfpDisplayNode.h"
#include "LfpDisplayOptions.h"
#include "LfpTimescale.h"
#include "LfpViewport.h"
#include "PerPixelBitmapPlotter.h"
#include "ShowHideOptionsButton.h"
#include "SupersampledBitmapPlotter.h"

#include <math.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

using namespace LfpViewer;

namespace LfpViewer
{
namespace
{
constexpr auto channelActionDispatchTimeout =
    std::chrono::milliseconds (100);

class LfpChannelActionCompletion final
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

    void complete() noexcept
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

    void waitForMutationCompletion (
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
            return;
        }
        if (! callbackClaimed)
        {
            cancelled = true;
            return;
        }
        completionChanged.wait (
            lock,
            [this]
            {
                return completed;
            });
    }

private:
    std::mutex mutex;
    std::condition_variable completionChanged;
    bool callbackClaimed = false;
    bool cancelled = false;
    bool completed = false;
};
} // namespace

String encodeWaveformVisibilityUtf8Hex (
    StringRef text)
{
    const String value (text);
    if (value.isEmpty())
        return {};

    const auto utf8 = value.toUTF8();
    const auto* bytes = reinterpret_cast<const uint8*> (
        utf8.getAddress());
    constexpr char hex[] = "0123456789ABCDEF";
    String result;

    for (int index = 0;
         index < value.getNumBytesAsUTF8();
         ++index)
    {
        const auto byte = bytes[index];
        result += hex[(byte >> 4) & 0x0f];
        result += hex[byte & 0x0f];
    }

    return result;
}

class LfpWaveformVisibilityAccessibilityState final
    : public std::enable_shared_from_this<
          LfpWaveformVisibilityAccessibilityState>
{
public:
    LfpWaveformVisibilityAccessibilityState (
        LfpChannelDisplayInfo& ownerToUse,
        const LfpStableChannelIdentity& identity,
        int nodeId,
        bool visible)
        : owner (&ownerToUse),
          processorNodeId (nodeId),
          paneIndex (identity.getPaneIndex()),
          streamKey (identity.getStreamKey()),
          runtimeUuid (identity.getRuntimeUuid()),
          persistedIdentifier (
              identity.getPersistedIdentifier()),
          persistedSourceNodeId (
              identity.getPersistedSourceNodeId()),
          persistedLocalIndex (
              identity.getPersistedLocalIndex()),
          persistedChannelName (
              identity.getPersistedChannelName()),
          persistedChannelType (
              identity.getPersistedChannelType()),
          waveformVisible (visible)
    {
        const auto* key = identity.getStableChannelKey();
        jassert (key != nullptr);
        if (key == nullptr)
            return;

        stableKeyKind = key->getKind();
        stableIdentifier = key->getIdentifier();
        stableSourceNodeId = key->getSourceNodeId();
        stableLocalIndex = key->getLocalIndex();

        const auto streamHex =
            encodeWaveformVisibilityUtf8Hex (
                streamKey);
        if (streamHex.isEmpty())
            return;

        const auto oneBasedPane = paneIndex + 1;
        if (stableKeyKind
            == LfpStableChannelKey::Kind::
                   identifier)
        {
            const auto identifierHex =
                encodeWaveformVisibilityUtf8Hex (
                    stableIdentifier);
            if (identifierHex.isEmpty())
                return;
            automationId =
                "oe.processor."
                + String (nodeId)
                + ".lfp.display_"
                + String (oneBasedPane)
                + ".stream_hex_"
                + streamHex
                + ".channel_identifier_hex_"
                + identifierHex
                + ".waveform_visibility";
            stableKeyText =
                "identifier \""
                + stableIdentifier
                + "\"";
        }
        else if (stableSourceNodeId >= 0
                 && stableLocalIndex >= 0)
        {
            automationId =
                "oe.processor."
                + String (nodeId)
                + ".lfp.display_"
                + String (oneBasedPane)
                + ".stream_hex_"
                + streamHex
                + ".channel_source_"
                + String (stableSourceNodeId)
                + "_local_"
                + String (stableLocalIndex)
                + ".waveform_visibility";
            stableKeyText =
                "source node "
                + String (stableSourceNodeId)
                + ", local channel index "
                + String (stableLocalIndex);
        }

        if (automationId.isEmpty())
            return;

        title =
            "LFP display "
            + String (oneBasedPane)
            + " stream \""
            + streamKey
            + "\" channel \""
            + persistedChannelName
            + "\" waveform visibility";
        description =
            "Waveform visibility for channel \""
            + persistedChannelName
            + "\" ("
            + stableKeyText
            + ") in stream \""
            + streamKey
            + "\" of LFP display "
            + String (oneBasedPane)
            + ".";
        help =
            "Show or hide drawing for channel \""
            + persistedChannelName
            + "\" ("
            + stableKeyText
            + ") in stream \""
            + streamKey
            + "\" of LFP display "
            + String (oneBasedPane)
            + ". Hiding stops waveform drawing only; it does not disable acquisition, buffering, hardware, or recording selection.";
        valid.store (true, std::memory_order_release);
    }

    bool isActive() const noexcept
    {
        return valid.load (std::memory_order_acquire)
            && active.load (std::memory_order_acquire);
    }

    void retire() noexcept
    {
        active.store (false, std::memory_order_release);
    }

    bool isVisible() const noexcept
    {
        return waveformVisible;
    }

    bool matchesIdentity (
        const LfpStableChannelIdentity& identity,
        int currentNodeId) const
    {
        const auto* key = identity.getStableChannelKey();
        if (! isActive()
            || key == nullptr
            || processorNodeId != currentNodeId
            || paneIndex != identity.getPaneIndex()
            || streamKey != identity.getStreamKey()
            || runtimeUuid != identity.getRuntimeUuid()
            || persistedIdentifier
                   != identity.getPersistedIdentifier()
            || persistedSourceNodeId
                   != identity.getPersistedSourceNodeId()
            || persistedLocalIndex
                   != identity.getPersistedLocalIndex()
            || persistedChannelName
                   != identity.getPersistedChannelName()
            || persistedChannelType
                   != identity.getPersistedChannelType()
            || stableKeyKind != key->getKind())
        {
            return false;
        }

        return stableKeyKind
                   == LfpStableChannelKey::Kind::
                          identifier
                   ? stableIdentifier
                         == key->getIdentifier()
                   : stableSourceNodeId
                             == key->getSourceNodeId()
                         && stableLocalIndex
                                == key->getLocalIndex();
    }

    const String& getAutomationId() const noexcept
    {
        return automationId;
    }

    const String& getTitle() const noexcept
    {
        return title;
    }

    const String& getDescription() const noexcept
    {
        return description;
    }

    const String& getHelp() const noexcept
    {
        return help;
    }

    void requestToggle()
    {
        if (! isActive())
            return;

        const auto perform =
            [weakState = weak_from_this()]
            {
                const auto state =
                    weakState.lock();
                if (state == nullptr
                    || ! state->isActive())
                {
                    return false;
                }

                auto* info =
                    state->owner.getComponent();
                return info != nullptr
                    && info->performWaveformVisibilityAccessibilityToggle (
                        state);
            };
        auto* messageManager =
            MessageManager::getInstanceWithoutCreating();
        if (messageManager == nullptr)
            return;

        if (messageManager->isThisTheMessageThread())
        {
            perform();
            return;
        }

        MessageManager::callSync (
            perform);
    }

private:
    Component::SafePointer<LfpChannelDisplayInfo> owner;
    const int processorNodeId;
    const int paneIndex;
    const String streamKey;
    const Uuid runtimeUuid;
    const String persistedIdentifier;
    const int persistedSourceNodeId;
    const int persistedLocalIndex;
    const String persistedChannelName;
    const ContinuousChannel::Type persistedChannelType;
    LfpStableChannelKey::Kind stableKeyKind =
        LfpStableChannelKey::Kind::identifier;
    String stableIdentifier;
    int stableSourceNodeId = -1;
    int stableLocalIndex = -1;
    const bool waveformVisible;
    String automationId;
    String stableKeyText;
    String title;
    String description;
    String help;
    std::atomic<bool> valid { false };
    std::atomic<bool> active { true };
};

class LfpWaveformVisibilityAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit LfpWaveformVisibilityAccessibilityValue (
        std::shared_ptr<LfpWaveformVisibilityAccessibilityState>
            stateToUse)
        : state (std::move (stateToUse))
    {
    }

    bool isReadOnly() const override
    {
        return true;
    }

    void setValueAsString (const String&) override
    {
    }

    String getCurrentValueAsString() const override
    {
        return state->isVisible()
                   ? "Visible"
                   : "Hidden";
    }

private:
    std::shared_ptr<LfpWaveformVisibilityAccessibilityState> state;
};

class LfpWaveformVisibilityAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    LfpWaveformVisibilityAccessibilityHandler (
        Component& component,
        std::shared_ptr<LfpWaveformVisibilityAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              component,
              AccessibilityRole::toggleButton,
              createActions (stateToUse),
              AccessibilityHandler::Interfaces {
                  std::make_unique<LfpWaveformVisibilityAccessibilityValue> (
                      stateToUse) }),
          state (std::move (stateToUse))
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto current =
            AccessibleState()
                .withFocusable()
                .withCheckable();
        return state->isVisible()
                   ? current.withChecked()
                   : current;
    }

    String getTitle() const override
    {
        return state->getTitle();
    }

    String getDescription() const override
    {
        return state->getDescription();
    }

    String getHelp() const override
    {
        return state->getHelp();
    }

    bool isEnabled() const override
    {
        return state->isActive();
    }

private:
    static AccessibilityActions createActions (
        const std::shared_ptr<LfpWaveformVisibilityAccessibilityState>&
            state)
    {
        return AccessibilityActions()
            .addAction (
                AccessibilityActionType::toggle,
                [state]
                {
                    state->requestToggle();
                });
    }

    std::shared_ptr<LfpWaveformVisibilityAccessibilityState> state;
};

class LfpWaveformVisibilityButton final
    : public UtilityButton
{
public:
    explicit LfpWaveformVisibilityButton (
        String label)
        : UtilityButton (std::move (label))
    {
    }

    void setWaveformVisibilityAccessibilityState (
        std::shared_ptr<LfpWaveformVisibilityAccessibilityState>
            stateToUse)
    {
        accessibilityState = std::move (stateToUse);
    }

    void setWaveformVisibilityAccessibilityOwner (
        LfpChannelDisplayInfo& ownerToUse)
    {
        owner = &ownerToUse;
    }

protected:
    std::unique_ptr<AccessibilityHandler>
    createAccessibilityHandler() override
    {
        const auto state =
            accessibilityState.lock();
        return state != nullptr
            ? std::make_unique<
                  LfpWaveformVisibilityAccessibilityHandler> (
                  *this,
                  state)
            : nullptr;
    }

    void visibilityChanged() override
    {
        UtilityButton::visibilityChanged();
        notifyOwnerOfLifecycleChange();
    }

    void enablementChanged() override
    {
        UtilityButton::enablementChanged();
        notifyOwnerOfLifecycleChange();
    }

    void parentHierarchyChanged() override
    {
        UtilityButton::parentHierarchyChanged();
        notifyOwnerOfLifecycleChange();
    }

private:
    void notifyOwnerOfLifecycleChange()
    {
        if (auto* currentOwner = owner.getComponent())
        {
            currentOwner
                ->handleWaveformVisibilityAccessibilityLifecycleChange();
        }
    }

    Component::SafePointer<LfpChannelDisplayInfo> owner;
    std::weak_ptr<LfpWaveformVisibilityAccessibilityState>
        accessibilityState;
};

class LfpChannelActionAccessibilityState final
    : public std::enable_shared_from_this<
          LfpChannelActionAccessibilityState>
{
public:
    LfpChannelActionAccessibilityState (
        LfpChannelDisplayInfo& ownerToUse,
        const LfpStableChannelIdentity& identity,
        int nodeId,
        LfpChannelAction actionToUse,
        bool selectedToUse,
        bool focusedToUse,
        bool invertedToUse,
        bool canInvertToUse)
        : owner (&ownerToUse),
          processorNodeId (nodeId),
          paneIndex (identity.getPaneIndex()),
          streamKey (identity.getStreamKey()),
          runtimeUuid (identity.getRuntimeUuid()),
          persistedIdentifier (
              identity.getPersistedIdentifier()),
          persistedSourceNodeId (
              identity.getPersistedSourceNodeId()),
          persistedLocalIndex (
              identity.getPersistedLocalIndex()),
          channelName (
              identity.getPersistedChannelName()),
          persistedChannelType (
              identity.getPersistedChannelType()),
          action (actionToUse),
          selected (selectedToUse),
          focused (focusedToUse),
          inverted (invertedToUse),
          canInvert (canInvertToUse)
    {
        const auto* key =
            identity.getStableChannelKey();
        if (key == nullptr)
            return;

        stableKeyKind = key->getKind();
        stableIdentifier = key->getIdentifier();
        stableSourceNodeId = key->getSourceNodeId();
        stableLocalIndex = key->getLocalIndex();
        const auto streamHex =
            encodeWaveformVisibilityUtf8Hex (
                streamKey);
        if (streamHex.isEmpty())
            return;

        const auto oneBasedPane =
            paneIndex + 1;
        prefix =
            "oe.processor."
            + String (nodeId)
            + ".lfp.display_"
            + String (oneBasedPane)
            + ".stream_hex_"
            + streamHex;
        if (stableKeyKind
            == LfpStableChannelKey::Kind::identifier)
        {
            const auto identifierHex =
                encodeWaveformVisibilityUtf8Hex (
                    stableIdentifier);
            if (identifierHex.isEmpty())
                return;
            prefix +=
                ".channel_identifier_hex_"
                + identifierHex;
            stableKeyText =
                "identifier \""
                + stableIdentifier
                + "\"";
        }
        else
        {
            if (stableSourceNodeId < 0
                || stableLocalIndex < 0)
            {
                return;
            }
            prefix +=
                ".channel_source_"
                + String (stableSourceNodeId)
                + "_local_"
                + String (stableLocalIndex);
            stableKeyText =
                "source node "
                + String (stableSourceNodeId)
                + ", local channel index "
                + String (stableLocalIndex);
        }

        const auto context =
            "channel \""
            + channelName
            + "\" ("
            + stableKeyText
            + ") in stream \""
            + streamKey
            + "\" of LFP display "
            + String (oneBasedPane);
        switch (action)
        {
            case LfpChannelAction::select:
                suffix = ".select";
                actionTitle = "select";
                description =
                    "Channel selection for "
                    + context + ".";
                help =
                    "Select "
                    + context
                    + " for the existing LFP channel operations. Selection also selects the pane; it does not change the focused XY readout, acquisition, buffering, hardware enablement, or recording selection.";
                break;
            case LfpChannelAction::toggleFocus:
                suffix =
                    ".single_channel_focus";
                actionTitle =
                    "single-channel focus";
                description =
                    "Single-channel focus for "
                    + context + ".";
                help =
                    "Show only "
                    + context
                    + ", or restore the pane's prior drawable channels. This coordinate-free action leaves the focused XY readout unchanged and does not rewrite stored waveform visibility or change acquisition, buffering, hardware, or recording selection.";
                break;
            case LfpChannelAction::toggleInvert:
                suffix = ".invert_signal";
                actionTitle = "invert signal";
                description =
                    "Displayed signal polarity for "
                    + context + ".";
                help =
                    "Invert or restore displayed signal polarity for "
                    + context
                    + ". If this channel cannot be inverted, the existing selection and redraw action still completes and polarity remains unchanged. This affects LFP display behavior only; it does not alter acquired samples, buffering, hardware, recording selection, or audio monitoring.";
                break;
            case LfpChannelAction::monitor:
                suffix = ".monitor";
                actionTitle = "monitor";
                description =
                    "One-shot audio monitor action for "
                    + context + ".";
                help =
                    "Send the existing one-shot audio monitor selection for "
                    + context
                    + ". Each Invoke sends exactly one AUDIO SELECT command and exposes no persistent monitor state.";
                break;
        }

        automationId = prefix + suffix;
        title =
            "LFP display "
            + String (oneBasedPane)
            + " stream \""
            + streamKey
            + "\" channel \""
            + channelName
            + "\" "
            + actionTitle;
        valid.store (
            true,
            std::memory_order_release);
    }

    bool isActive() const noexcept
    {
        return valid.load (
                   std::memory_order_acquire)
            && active.load (
                std::memory_order_acquire);
    }

    void retire() noexcept
    {
        active.store (
            false,
            std::memory_order_release);
    }

    bool matchesIdentity (
        const LfpStableChannelIdentity& identity,
        int nodeId) const
    {
        const auto* key =
            identity.getStableChannelKey();
        if (! isActive()
            || key == nullptr
            || processorNodeId != nodeId
            || paneIndex
                   != identity.getPaneIndex()
            || streamKey
                   != identity.getStreamKey()
            || runtimeUuid
                   != identity.getRuntimeUuid()
            || persistedIdentifier
                   != identity.getPersistedIdentifier()
            || persistedSourceNodeId
                   != identity.getPersistedSourceNodeId()
            || persistedLocalIndex
                   != identity.getPersistedLocalIndex()
            || channelName
                   != identity.getPersistedChannelName()
            || persistedChannelType
                   != identity.getPersistedChannelType()
            || stableKeyKind != key->getKind())
        {
            return false;
        }
        return stableKeyKind
                   == LfpStableChannelKey::Kind::identifier
                   ? stableIdentifier
                         == key->getIdentifier()
                   : stableSourceNodeId
                             == key->getSourceNodeId()
                         && stableLocalIndex
                                == key->getLocalIndex();
    }

    bool matchesObservableState (
        bool selectedToUse,
        bool focusedToUse,
        bool invertedToUse,
        bool canInvertToUse) const noexcept
    {
        return selected == selectedToUse
            && focused == focusedToUse
            && inverted == invertedToUse
            && canInvert == canInvertToUse;
    }

    void requestAction()
    {
        if (! isActive())
            return;
        const auto perform =
            [weakState = weak_from_this()]
            {
                const auto state =
                    weakState.lock();
                if (state == nullptr
                    || ! state->isActive())
                {
                    return false;
                }
                auto* info =
                    state->owner.getComponent();
                return info != nullptr
                    && info
                           ->performChannelActionAccessibility (
                               state)
                           != LfpChannelActionResult::rejected;
            };
        auto* manager =
            MessageManager::getInstanceWithoutCreating();
        if (manager == nullptr)
            return;
        if (manager->isThisTheMessageThread())
        {
            perform();
            return;
        }
        auto completion =
            std::make_shared<
                LfpChannelActionCompletion>();
        const auto posted =
            MessageManager::callAsync (
                [perform,
                 completion]
                {
                    if (! completion
                              ->tryBeginCallback())
                    {
                        return;
                    }
                    try
                    {
                        perform();
                    }
                    catch (...)
                    {
                    }
                    completion->complete();
                });
        if (! posted)
        {
            completion->cancel();
            return;
        }
        completion
            ->waitForMutationCompletion (
                channelActionDispatchTimeout);
    }

    const String& getAutomationId() const noexcept { return automationId; }
    const String& getPrefix() const noexcept { return prefix; }
    const String& getStableKeyText() const noexcept { return stableKeyText; }
    const String& getTitle() const noexcept { return title; }
    const String& getDescription() const noexcept { return description; }
    const String& getHelp() const noexcept { return help; }
    const String& getStreamKey() const noexcept { return streamKey; }
    const String& getChannelName() const noexcept { return channelName; }
    int getPaneIndex() const noexcept { return paneIndex; }
    LfpChannelAction getAction() const noexcept { return action; }
    bool isSelected() const noexcept { return selected; }
    bool isFocused() const noexcept { return focused; }
    bool isInverted() const noexcept { return inverted; }
    bool canBeInverted() const noexcept { return canInvert; }

private:
    Component::SafePointer<LfpChannelDisplayInfo> owner;
    const int processorNodeId;
    const int paneIndex;
    const String streamKey;
    const Uuid runtimeUuid;
    const String persistedIdentifier;
    const int persistedSourceNodeId;
    const int persistedLocalIndex;
    const String channelName;
    const ContinuousChannel::Type persistedChannelType;
    const LfpChannelAction action;
    const bool selected;
    const bool focused;
    const bool inverted;
    const bool canInvert;
    LfpStableChannelKey::Kind stableKeyKind =
        LfpStableChannelKey::Kind::identifier;
    String stableIdentifier;
    int stableSourceNodeId = -1;
    int stableLocalIndex = -1;
    String prefix;
    String suffix;
    String stableKeyText;
    String actionTitle;
    String automationId;
    String title;
    String description;
    String help;
    std::atomic<bool> valid { false };
    std::atomic<bool> active { true };
};

class LfpChannelActionAccessibilityValue final
    : public AccessibilityTextValueInterface
{
public:
    explicit LfpChannelActionAccessibilityValue (
        std::shared_ptr<LfpChannelActionAccessibilityState>
            stateToUse)
        : state (std::move (stateToUse))
    {
    }

    bool isReadOnly() const override { return true; }
    void setValueAsString (const String&) override {}

    String getCurrentValueAsString() const override
    {
        switch (state->getAction())
        {
            case LfpChannelAction::select:
                return state->isSelected()
                    ? "Selected"
                    : "Not selected";
            case LfpChannelAction::toggleFocus:
                return state->isFocused()
                    ? "Focused"
                    : "Not focused";
            case LfpChannelAction::toggleInvert:
            {
                const auto polarity =
                    state->isInverted()
                        ? String ("Inverted")
                        : String ("Normal");
                return state->canBeInverted()
                    ? polarity
                    : polarity
                        + "; inversion unavailable";
            }
            case LfpChannelAction::monitor:
                break;
        }
        return {};
    }

private:
    std::shared_ptr<LfpChannelActionAccessibilityState> state;
};

class LfpChannelActionAccessibilityHandler final
    : public AccessibilityHandler
{
public:
    LfpChannelActionAccessibilityHandler (
        Component& component,
        std::shared_ptr<LfpChannelActionAccessibilityState>
            stateToUse)
        : AccessibilityHandler (
              component,
              roleFor (*stateToUse),
              actionsFor (stateToUse),
              interfacesFor (stateToUse)),
          state (std::move (stateToUse))
    {
    }

    AccessibleState getCurrentState() const override
    {
        auto current =
            AccessibleState().withFocusable();
        if (state->getAction()
                == LfpChannelAction::toggleFocus
            || state->getAction()
                   == LfpChannelAction::toggleInvert)
        {
            current = current.withCheckable();
            const auto checked =
                state->getAction()
                        == LfpChannelAction::toggleFocus
                    ? state->isFocused()
                    : state->isInverted();
            if (checked)
                current = current.withChecked();
        }
        return current;
    }

    String getTitle() const override { return state->getTitle(); }
    String getDescription() const override { return state->getDescription(); }
    String getHelp() const override { return state->getHelp(); }
    bool isEnabled() const override { return state->isActive(); }

private:
    static AccessibilityRole roleFor (
        const LfpChannelActionAccessibilityState& state)
    {
        return state.getAction()
                       == LfpChannelAction::toggleFocus
                   || state.getAction()
                          == LfpChannelAction::toggleInvert
            ? AccessibilityRole::toggleButton
            : AccessibilityRole::button;
    }

    static AccessibilityActions actionsFor (
        const std::shared_ptr<LfpChannelActionAccessibilityState>& state)
    {
        return AccessibilityActions()
            .addAction (
                state->getAction()
                           == LfpChannelAction::toggleFocus
                       || state->getAction()
                              == LfpChannelAction::toggleInvert
                    ? AccessibilityActionType::toggle
                    : AccessibilityActionType::press,
                [state]
                {
                    state->requestAction();
                });
    }

    static AccessibilityHandler::Interfaces interfacesFor (
        const std::shared_ptr<LfpChannelActionAccessibilityState>& state)
    {
        AccessibilityHandler::Interfaces result;
        if (state->getAction()
            != LfpChannelAction::monitor)
        {
            result.value =
                std::make_unique<
                    LfpChannelActionAccessibilityValue> (
                    state);
        }
        return result;
    }

    std::shared_ptr<LfpChannelActionAccessibilityState> state;
};

class LfpChannelActionAccessibilityComponent final
    : public Component
{
public:
    explicit LfpChannelActionAccessibilityComponent (
        LfpChannelDisplayInfo& ownerToUse)
        : owner (&ownerToUse)
    {
        setInterceptsMouseClicks (
            false,
            false);
        setWantsKeyboardFocus (false);
    }

    void setState (
        std::shared_ptr<LfpChannelActionAccessibilityState>
            stateToUse)
    {
        state = stateToUse;
    }

protected:
    std::unique_ptr<AccessibilityHandler>
    createAccessibilityHandler() override
    {
        const auto current = state.lock();
        return current != nullptr
            && current->isActive()
            ? std::make_unique<
                  LfpChannelActionAccessibilityHandler> (
                  *this,
                  current)
            : nullptr;
    }

    void visibilityChanged() override
    {
        Component::visibilityChanged();
        notifyOwner();
    }

    void enablementChanged() override
    {
        Component::enablementChanged();
        notifyOwner();
    }

    void parentHierarchyChanged() override
    {
        Component::parentHierarchyChanged();
        notifyOwner();
    }

private:
    void notifyOwner()
    {
        if (state.expired())
            return;
        if (auto* currentOwner =
                owner.getComponent())
        {
            currentOwner
                ->handleChannelActionAccessibilityLifecycleChange();
        }
    }

    Component::SafePointer<LfpChannelDisplayInfo> owner;
    std::weak_ptr<LfpChannelActionAccessibilityState> state;
};
} // namespace LfpViewer

#pragma mark - LfpChannelDisplayInfo -
// -------------------------------

LfpChannelDisplayInfo::LfpChannelDisplayInfo (LfpDisplaySplitter* canvas_, LfpDisplay* display_, LfpDisplayOptions* options_, int ch)
    : LfpChannelDisplay (canvas_, display_, options_, ch),
      x (-1.0f),
      y (-1.0f),
      rms (0.0f),
      mean (0.0f),
      isSingleChannel (false)
{
    enableButton = std::make_unique<LfpWaveformVisibilityButton> ("");
    static_cast<LfpWaveformVisibilityButton*> (
        enableButton.get())
        ->setWaveformVisibilityAccessibilityOwner (*this);
    enableButton->setRadius (5.0f);

    enableButton->setEnabledState (true);
    enableButton->setCorners (true, true, true, true);
    enableButton->addListener (this);
    enableButton->setClickingTogglesState (true);
    enableButton->setToggleState (true, dontSendNotification);
    enableButton->setAccessible (false);

    addAndMakeVisible (enableButton.get());
    setFocusContainerType (
        Component::FocusContainerType::
            focusContainer);
    for (auto& component :
         channelActionAccessibilityComponents)
    {
        component =
            std::make_unique<
                LfpChannelActionAccessibilityComponent> (
                *this);
        component->setAccessible (false);
        addAndMakeVisible (
            component.get());
    }

    String svgString = "M302.189 329.126H196.105l55.831 135.993c3.889 9.428-.555 19.999-9.444 23.999l-49.165 21.427c-9.165 \
                       4-19.443-.571-23.332-9.714l-53.053-129.136-86.664 89.138C18.729 472.71 0 463.554 0 447.977V18.299C0 \
                       1.899 19.921-6.096 30.277 5.443l284.412 292.542c11.472 11.179 3.007 31.141-12.5 31.141z";

    pointerPath = Drawable::parseSVGPath (svgString);
}

LfpChannelDisplayInfo::~LfpChannelDisplayInfo()
{
    revokeChannelActionAccessibility();
    revokeWaveformVisibilityAccessibility();
}

std::unique_ptr<AccessibilityHandler>
LfpChannelDisplayInfo::
    createAccessibilityHandler()
{
    return std::make_unique<
        AccessibilityHandler> (
        *this,
        AccessibilityRole::group);
}

void LfpChannelDisplayInfo::visibilityChanged()
{
    LfpChannelDisplay::visibilityChanged();
    handleChannelActionAccessibilityLifecycleChange();
    handleWaveformVisibilityAccessibilityLifecycleChange();
}

void LfpChannelDisplayInfo::enablementChanged()
{
    LfpChannelDisplay::enablementChanged();
    handleChannelActionAccessibilityLifecycleChange();
    handleWaveformVisibilityAccessibilityLifecycleChange();
}

void LfpChannelDisplayInfo::parentHierarchyChanged()
{
    Component::parentHierarchyChanged();
    handleChannelActionAccessibilityLifecycleChange();
    handleWaveformVisibilityAccessibilityLifecycleChange();
}

void LfpChannelDisplayInfo::updateType (ContinuousChannel::Type type_)
{
    type = type_;
    typeStr = options->getTypeName (type);
    repaint();
}

void LfpChannelDisplayInfo::buttonClicked (Button* button)
{
    bool state = button->getToggleState();

    display->setEnabledState (state, chan, true);
}

void LfpChannelDisplayInfo::setEnabledState (bool state)
{
    LfpChannelDisplay::setEnabledState (
        state);
    enableButton->setToggleState (state, dontSendNotification);
}

void LfpChannelDisplayInfo::
    refreshWaveformVisibilityAccessibility (
        const std::shared_ptr<
            const LfpStableChannelIdentity>& identity,
        int nodeId,
        bool structurallyAvailable,
        bool waveformVisible)
{
    const auto isAvailable =
        structurallyAvailable
        && identity != nullptr
        && identity->getStableChannelKey() != nullptr
        && identity->getPaneIndex() >= 0
        && identity->getStreamKey().isNotEmpty()
        && isWaveformVisibilityAccessibilityControlAvailable();
    if (! isAvailable)
    {
        revokeWaveformVisibilityAccessibility();
        return;
    }

    const auto current =
        waveformVisibilityAccessibilityState;
    if (current != nullptr
        && current->isActive()
        && current->isVisible()
               == waveformVisible
        && current->matchesIdentity (
               *identity,
               nodeId))
    {
        return;
    }

    revokeWaveformVisibilityAccessibility();
    auto successor = std::make_shared<
        LfpWaveformVisibilityAccessibilityState> (
        *this,
        *identity,
        nodeId,
        waveformVisible);
    if (! successor->isActive())
    {
        successor->retire();
        return;
    }

    auto* button = dynamic_cast<
        LfpWaveformVisibilityButton*> (
        enableButton.get());
    jassert (button != nullptr);
    if (button == nullptr)
    {
        successor->retire();
        return;
    }

    waveformVisibilityAccessibilityState =
        std::move (successor);
    enableButton->setComponentID (
        waveformVisibilityAccessibilityState
            ->getAutomationId());
    enableButton->setTitle (
        waveformVisibilityAccessibilityState
            ->getTitle());
    enableButton->setDescription (
        waveformVisibilityAccessibilityState
            ->getDescription());
    enableButton->setHelpText (
        waveformVisibilityAccessibilityState
            ->getHelp());
    enableButton->setAccessible (true);
    button->setWaveformVisibilityAccessibilityState (
        waveformVisibilityAccessibilityState);
    enableButton->invalidateAccessibilityHandler();
    const auto buttonForNotification =
        Component::SafePointer<Component> (
            enableButton.get());
    MessageManager::callAsync (
        [buttonForNotification]
        {
            if (auto* button =
                    buttonForNotification.getComponent())
            {
                if (auto* handler =
                        button
                            ->getAccessibilityHandler())
                {
                    handler->notifyAccessibilityEvent (
                        AccessibilityEvent::valueChanged);
                }
            }
        });
}

void LfpChannelDisplayInfo::
    revokeWaveformVisibilityAccessibility()
{
    if (waveformVisibilityAccessibilityState != nullptr)
        waveformVisibilityAccessibilityState->retire();

    waveformVisibilityAccessibilityState.reset();
    if (auto* button = dynamic_cast<
            LfpWaveformVisibilityButton*> (
            enableButton.get()))
    {
        button->setWaveformVisibilityAccessibilityState (
            {});
    }
    enableButton->setAccessible (false);
    enableButton->setComponentID ({});
    enableButton->setTitle ({});
    enableButton->setDescription ({});
    enableButton->setHelpText ({});
    enableButton->invalidateAccessibilityHandler();
}

bool LfpChannelDisplayInfo::
    performWaveformVisibilityAccessibilityToggle (
        const std::shared_ptr<
            LfpWaveformVisibilityAccessibilityState>& state)
{
    jassert (
        MessageManager::existsAndIsCurrentThread());
    if (state == nullptr
        || state != waveformVisibilityAccessibilityState
        || ! state->isActive()
        || display == nullptr
        || ! display
                ->validateWaveformVisibilityAccessibility (
                    *this))
    {
        return false;
    }

    const auto requestedState =
        ! state->isVisible();
    if (display->getStoredChannelVisibility (chan)
        == requestedState)
    {
        return false;
    }

    state->retire();
    display->setEnabledState (
        requestedState,
        chan,
        true);
    if (auto* handler =
            display->getAccessibilityHandler())
    {
        handler->notifyAccessibilityEvent (
            AccessibilityEvent::structureChanged);
    }
    return true;
}

bool LfpChannelDisplayInfo::
    matchesWaveformVisibilityAccessibilityIdentity (
        const LfpStableChannelIdentity& identity,
        int nodeId) const
{
    return waveformVisibilityAccessibilityState
               != nullptr
        && waveformVisibilityAccessibilityState
               ->matchesIdentity (identity, nodeId);
}

bool LfpChannelDisplayInfo::
    isWaveformVisibilityAccessibilityControlAvailable () const
{
    return enableButton != nullptr
        && enableButton->isShowing()
        && enableButton->Component::isEnabled()
        && isShowing()
        && Component::isEnabled();
}

void LfpChannelDisplayInfo::
    handleWaveformVisibilityAccessibilityLifecycleChange()
{
    jassert (MessageManager::existsAndIsCurrentThread());
    revokeWaveformVisibilityAccessibility();
    if (display != nullptr)
    {
        display
            ->requestWaveformVisibilityAccessibilityAvailabilityRefresh();
    }
}

void LfpChannelDisplayInfo::
    refreshChannelActionAccessibility (
        const std::shared_ptr<
            const LfpStableChannelIdentity>& identity,
        int nodeId,
        bool structurallyAvailable)
{
    const auto available =
        structurallyAvailable
        && identity != nullptr
        && identity->getStableChannelKey()
               != nullptr
        && identity->getPaneIndex() >= 0
        && identity->getStreamKey()
               .isNotEmpty()
        && isShowing()
        && Component::isEnabled();
    if (! available)
    {
        revokeChannelActionAccessibility();
        return;
    }

    const auto selected =
        display != nullptr
        && chan >= 0
        && chan < display->channels.size()
        && display->channels[chan] != nullptr
        && display->channels[chan]
               ->getSelected();
    const auto focused =
        display != nullptr
        && display->getSingleChannelState()
        && display->getSingleChannelShown()
               == chan;
    const auto inverted =
        display != nullptr
        && chan >= 0
        && chan < display->channels.size()
        && display->channels[chan] != nullptr
        && display->channels[chan]
               ->getInputInverted();
    const auto invertible =
        display != nullptr
        && chan >= 0
        && chan < display->channels.size()
        && display->channels[chan] != nullptr
        && display->channels[chan]
               ->getCanBeInverted();

    bool currentMatches = true;
    for (const auto& state :
         channelActionAccessibilityStates)
    {
        currentMatches =
            currentMatches
            && state != nullptr
            && state->isActive()
            && state->matchesIdentity (
                *identity,
                nodeId)
            && state->matchesObservableState (
                selected,
                focused,
                inverted,
                invertible);
    }
    if (currentMatches)
        return;

    revokeChannelActionAccessibility();
    constexpr std::array<
        LfpChannelAction,
        4>
        actions {
            LfpChannelAction::select,
            LfpChannelAction::toggleFocus,
            LfpChannelAction::toggleInvert,
            LfpChannelAction::monitor
        };
    for (size_t index = 0;
         index < actions.size();
         ++index)
    {
        auto state = std::make_shared<
            LfpChannelActionAccessibilityState> (
            *this,
            *identity,
            nodeId,
            actions[index],
            selected,
            focused,
            inverted,
            invertible);
        if (! state->isActive())
        {
            state->retire();
            revokeChannelActionAccessibility();
            return;
        }
        channelActionAccessibilityStates[index] =
            state;
        auto* component =
            dynamic_cast<
                LfpChannelActionAccessibilityComponent*> (
                channelActionAccessibilityComponents[
                    index]
                    .get());
        if (component == nullptr)
        {
            revokeChannelActionAccessibility();
            return;
        }
        component->setState (state);
        component->setComponentID (
            state->getAutomationId());
        component->setTitle (
            state->getTitle());
        component->setDescription (
            state->getDescription());
        component->setHelpText (
            state->getHelp());
        component->setAccessible (true);
        component
            ->invalidateAccessibilityHandler();
    }

    const auto& state =
        channelActionAccessibilityStates[0];
    const auto oneBasedPane =
        state->getPaneIndex() + 1;
    setComponentID (
        state->getPrefix()
        + ".actions");
    setTitle (
        "LFP display "
        + String (oneBasedPane)
        + " stream \""
        + state->getStreamKey()
        + "\" channel \""
        + state->getChannelName()
        + "\" actions");
    setDescription (
        "Actions for channel \""
        + state->getChannelName()
        + "\" ("
        + state->getStableKeyText()
        + ") in stream \""
        + state->getStreamKey()
        + "\" of LFP display "
        + String (oneBasedPane)
        + ".");
    setHelpText (
        "Contains the stable select, single-channel focus, invert signal, and monitor controls for channel \""
        + state->getChannelName()
        + "\" ("
        + state->getStableKeyText()
        + ") in stream \""
        + state->getStreamKey()
        + "\" of LFP display "
        + String (oneBasedPane)
        + ". It also contains the existing waveform visibility control when that control is available.");
    setAccessible (true);
    invalidateAccessibilityHandler();
    layoutChannelActionAccessibilityComponents();
}

void LfpChannelDisplayInfo::
    revokeChannelActionAccessibility()
{
    for (auto& state :
         channelActionAccessibilityStates)
    {
        if (state != nullptr)
            state->retire();
        state.reset();
    }
    for (auto& component :
         channelActionAccessibilityComponents)
    {
        if (auto* actionComponent =
                dynamic_cast<
                    LfpChannelActionAccessibilityComponent*> (
                    component.get()))
        {
            actionComponent->setState ({});
        }
        if (component != nullptr)
        {
            component->setAccessible (false);
            component->setComponentID ({});
            component->setTitle ({});
            component->setDescription ({});
            component->setHelpText ({});
            component
                ->invalidateAccessibilityHandler();
        }
    }
    setAccessible (false);
    setComponentID ({});
    setTitle ({});
    setDescription ({});
    setHelpText ({});
    invalidateAccessibilityHandler();
}

LfpChannelActionResult
LfpChannelDisplayInfo::
    performChannelActionAccessibility (
        const std::shared_ptr<
            LfpChannelActionAccessibilityState>& state)
{
    jassert (
        MessageManager::existsAndIsCurrentThread());
    if (display == nullptr
        || state == nullptr)
    {
        return LfpChannelActionResult::rejected;
    }
    const auto action =
        state->getAction();
    const auto actionIndex =
        action
                == LfpChannelAction::select
            ? size_t (0)
        : action
                == LfpChannelAction::toggleFocus
            ? size_t (1)
        : action
                == LfpChannelAction::toggleInvert
            ? size_t (2)
            : size_t (3);
    if (! state->isActive()
        || channelActionAccessibilityStates[
               actionIndex]
               != state)
    {
        return LfpChannelActionResult::rejected;
    }
    return display
        ->performChannelActionAccessibility (
            *this,
            action);
}

bool LfpChannelDisplayInfo::
    matchesChannelActionAccessibilityIdentity (
        const LfpStableChannelIdentity& identity,
        int nodeId) const
{
    for (const auto& state :
         channelActionAccessibilityStates)
    {
        if (state == nullptr
            || ! state->matchesIdentity (
                identity,
                nodeId))
        {
            return false;
        }
    }
    return true;
}

void LfpChannelDisplayInfo::
    handleChannelActionAccessibilityLifecycleChange()
{
    jassert (
        MessageManager::existsAndIsCurrentThread());
    revokeChannelActionAccessibility();
    if (display != nullptr)
    {
        display
            ->requestChannelActionAccessibilityAvailabilityRefresh();
    }
}

void LfpChannelDisplayInfo::
    layoutChannelActionAccessibilityComponents()
{
    for (auto& component :
         channelActionAccessibilityComponents)
    {
        if (component != nullptr)
            component->setBounds (
                getLocalBounds());
    }
}

void LfpChannelDisplayInfo::setSingleChannelState (bool state)
{
    isSingleChannel = state;
}

int LfpChannelDisplayInfo::getChannelSampleRate()
{
    return samplerate;
}

void LfpChannelDisplayInfo::setChannelSampleRate (int samplerate_)
{
    samplerate = samplerate_;
}

void LfpChannelDisplayInfo::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isLeftButtonDown()) // double check that we initiate only for left click and hold
    {
        if (e.mods.isCommandDown() && ! display->getSingleChannelState()) // CTRL + drag -> change channel spacing
        {
            // init state in our track zooming info struct
            if (! display->trackZoomInfo.isScrollingY)
            {
                auto& zoomInfo = display->trackZoomInfo;

                zoomInfo.isScrollingY = true;
                zoomInfo.componentStartHeight = getChannelHeight();
                zoomInfo.zoomPivotRatioY = (getY() + e.getMouseDownY()) / (float) display->getHeight();
                zoomInfo.zoomPivotRatioX = (getX() + e.getMouseDownX()) / (float) display->getWidth();
                zoomInfo.zoomPivotViewportOffset = getPosition() + e.getMouseDownPosition() - canvasSplit->viewport->getViewPosition();

                zoomInfo.unpauseOnScrollEnd = ! display->isPaused();
                if (! display->isPaused())
                    display->options->togglePauseButton (true);
            }

            int h = display->trackZoomInfo.componentStartHeight;
            int hdiff = 0;
            int dragDeltaY = -0.1 * (e.getScreenPosition().getY() - e.getMouseDownScreenY()); // invert so drag up -> scale up

            if (dragDeltaY > 0)
            {
                hdiff = 2 * dragDeltaY;
            }
            else
            {
                if (h > 5)
                    hdiff = 2 * dragDeltaY;
            }

            if (abs (h) > 100) // accelerate scrolling for large ranges
                hdiff *= 3;

            int newHeight = h + hdiff;

            // constrain the spread resizing to max and min values;
            if (newHeight < display->trackZoomInfo.minZoomHeight)
            {
                newHeight = display->trackZoomInfo.minZoomHeight;
            }
            else if (newHeight > display->trackZoomInfo.maxZoomHeight)
            {
                newHeight = display->trackZoomInfo.maxZoomHeight;
            }

            // return early if there is nothing to update
            if (newHeight == getChannelHeight())
            {
                return;
            }

            // set channel heights for all channels
            for (int i = 0; i < display->getNumChannels(); ++i)
            {
                display->channels[i]->setChannelHeight (newHeight);
                display->channelInfo[i]->setChannelHeight (newHeight);
            }

            options->setSpreadSelection (newHeight, false, true); // update combobox

            canvasSplit->fullredraw = true; //issue full redraw - scrolling without modifier doesnt require a full redraw

            display->setBounds (0, 0, display->getWidth() - 0, display->getChannelHeight() * display->drawableChannels.size()); // update height so that the scrollbar is correct

            int newViewportY = display->trackZoomInfo.zoomPivotRatioY * display->getHeight() - display->trackZoomInfo.zoomPivotViewportOffset.getY();
            if (newViewportY < 0)
                newViewportY = 0; // make sure we don't adjust beyond the edge of the actual view

            canvasSplit->viewport->setViewPosition (0, newViewportY);
        }
    }
}

void LfpChannelDisplayInfo::mouseUp (const MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && display->trackZoomInfo.isScrollingY)
    {
        display->trackZoomInfo.isScrollingY = false;
        if (display->trackZoomInfo.unpauseOnScrollEnd)
        {
            display->pause (false);
        }
    }
}

void LfpChannelDisplayInfo::recordingStarted()
{
    recordingIsActive = true;
    repaint();
}

void LfpChannelDisplayInfo::recordingStopped()
{
    recordingIsActive = false;
    repaint();
}

void LfpChannelDisplayInfo::paint (Graphics& g)
{
    int center = getHeight() / 2 - (isSingleChannel ? (75) : (0));
    const bool showChannelNumbers = options->getChannelNameState();

    // Draw the channel numbers
    if (isRecorded && recordingIsActive)
        g.setColour (Colours::red);
    else
        g.setColour (Colours::grey);

    const String channelString = (isChannelNumberHidden() ? ("--") : showChannelNumbers ? String (getChannelNumber() + 1)
                                                                                        : getName());
    bool isCentered = ! getEnabledButtonVisibility();

    if (isSingleChannel)
        g.setFont (FontOptions (16.0f).withStyle ("SemiBold"));
    else
        g.setFont (FontOptions (14.0f));

    g.drawText (channelString,
                showChannelNumbers ? 6 : 3,
                center - 4,
                getWidth(),
                12,
                isCentered ? Justification::centred : Justification::centredLeft,
                false);

    g.setColour (lineColour);
    g.fillRect (0, 0, 2, getHeight());

    if (getChannelTypeStringVisibility())
    {
        constexpr int textHeight = 14;
        constexpr int textStartX = 5;
        const int textY = center + 10;

        g.setFont (FontOptions (13.0f));
        g.setColour (lineColour);
        const auto currentFont = g.getCurrentFont();

        const int typeWidth = currentFont.getStringWidth (typeStr);
        const int typeBoundsWidth = typeWidth + 2;
        g.drawText (typeStr, textStartX, textY, typeBoundsWidth, textHeight, Justification::centredLeft, false);

        const String& unitsText = getUnits();
        if (unitsText.isNotEmpty())
        {
            const int unitsX = textStartX + typeWidth + 5;
            const int unitsWidth = getWidth() - unitsX - 4;

            if (unitsWidth > 0)
            {
                g.setColour (Colours::grey.withAlpha (0.8f));
                g.setFont (FontOptions (12.0f));
                g.drawFittedText (unitsText, unitsX, textY, unitsWidth, textHeight, Justification::centredLeft, 1, 0.8f);
            }
        }
    }

    if (isSingleChannel)
    {
        g.setColour (Colours::grey);
        g.setFont (FontOptions (13.0f));

        g.drawText ("MEAN:", 5, center + 40, 50, 12, Justification::centred, false);
        g.drawText (String (mean, 2), 5, center + 60, 50, 12, Justification::centred, false);

        g.drawText (String (rms, 2), 5, center + 110, 50, 12, Justification::centred, false);
        g.drawText ("RMS:", 5, center + 90, 50, 12, Justification::centred, false);

        if (x > 0)
        {
            g.setColour (Colours::darkgrey);
            g.fillPath (pointerPath, pointerPath.getTransformToScaleToFit (23, center + 140, 13, 13, true));
            g.drawText (String (y, 2), 5, center + 160, 50, 10, Justification::centred, false);
        }
    }
}

void LfpChannelDisplayInfo::updateXY (float x_, float y_)
{
    x = x_;
    y = y_;
}

void LfpChannelDisplayInfo::updateMeanAndRMS()
{
    rms = canvasSplit->getRMS (chan);
    mean = canvasSplit->getDisplayBufferMean (chan);

    repaint();
}

void LfpChannelDisplayInfo::resized()
{
    layoutChannelActionAccessibilityComponents();
    int center = getHeight() / 2 - (isSingleChannel ? (75) : (0));
    setEnabledButtonVisibility (getHeight() >= 16);

    if (getEnabledButtonVisibility())
    {
        enableButton->setBounds (getWidth() - 13, center - 5, 10, 10);
    }

    setChannelNumberIsHidden (getHeight() < 16 && (getDrawableChannelNumber() + 1) % 10 != 0);

    setChannelTypeStringVisibility (getHeight() > 34);
}

void LfpChannelDisplayInfo::setEnabledButtonVisibility (bool shouldBeVisible)
{
    const auto changed =
        enableButton->isVisible()
        != shouldBeVisible;
    enableButton->setVisible (shouldBeVisible);
    if (changed
        && display != nullptr)
    {
        display
            ->requestWaveformVisibilityAccessibilityAvailabilityRefresh();
    }
}

bool LfpChannelDisplayInfo::getEnabledButtonVisibility()
{
    return enableButton->isVisible();
}

void LfpChannelDisplayInfo::setChannelTypeStringVisibility (bool shouldBeVisible)
{
    channelTypeStringIsVisible = shouldBeVisible;
}

bool LfpChannelDisplayInfo::getChannelTypeStringVisibility()
{
    return channelTypeStringIsVisible || isSingleChannel;
}

void LfpChannelDisplayInfo::setChannelNumberIsHidden (bool shouldBeHidden)
{
    channelNumberHidden = shouldBeHidden;
}

bool LfpChannelDisplayInfo::isChannelNumberHidden()
{
    return channelNumberHidden;
}

String LfpChannelDisplayInfo::getTooltip()
{
    const bool showChannelNumbers = options->getChannelNameState();
    const String channelString = showChannelNumbers ? String (getChannelNumber() + 1) : getName();

    return channelString;
}
