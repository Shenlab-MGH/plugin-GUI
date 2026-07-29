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
#include <memory>

using namespace LfpViewer;

namespace LfpViewer
{
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

    String svgString = "M302.189 329.126H196.105l55.831 135.993c3.889 9.428-.555 19.999-9.444 23.999l-49.165 21.427c-9.165 \
                       4-19.443-.571-23.332-9.714l-53.053-129.136-86.664 89.138C18.729 472.71 0 463.554 0 447.977V18.299C0 \
                       1.899 19.921-6.096 30.277 5.443l284.412 292.542c11.472 11.179 3.007 31.141-12.5 31.141z";

    pointerPath = Drawable::parseSVGPath (svgString);
}

LfpChannelDisplayInfo::~LfpChannelDisplayInfo()
{
    revokeWaveformVisibilityAccessibility();
}

void LfpChannelDisplayInfo::visibilityChanged()
{
    LfpChannelDisplay::visibilityChanged();
    handleWaveformVisibilityAccessibilityLifecycleChange();
}

void LfpChannelDisplayInfo::enablementChanged()
{
    LfpChannelDisplay::enablementChanged();
    handleWaveformVisibilityAccessibilityLifecycleChange();
}

void LfpChannelDisplayInfo::parentHierarchyChanged()
{
    Component::parentHierarchyChanged();
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
    if (display != nullptr)
    {
        display
            ->refreshWaveformVisibilityAccessibilityAvailability();
    }
    else
    {
        revokeWaveformVisibilityAccessibility();
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
            ->refreshWaveformVisibilityAccessibilityAvailability();
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
