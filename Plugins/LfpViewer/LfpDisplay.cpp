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

#include "LfpDisplay.h"
#include "EventDisplayInterface.h"
#include "LfpBitmapPlotter.h"
#include "LfpBitmapPlotterInfo.h"
#include "LfpChannelDisplay.h"
#include "LfpChannelDisplayInfo.h"
#include "LfpDisplayCanvas.h"
#include "LfpDisplayNode.h"
#include "LfpDisplayOptions.h"
#include "LfpTimescale.h"
#include "LfpViewport.h"
#include "PerPixelBitmapPlotter.h"
#include "ShowHideOptionsButton.h"
#include "SupersampledBitmapPlotter.h"

#include "ColourSchemes/DefaultColourScheme.h"
#include "ColourSchemes/LightBackgroundColourScheme.h"
#include "ColourSchemes/MonochromeGrayColourScheme.h"
#include "ColourSchemes/MonochromeGreenColourScheme.h"
#include "ColourSchemes/MonochromePurpleColourScheme.h"
#include "ColourSchemes/MonochromeYellowColourScheme.h"
#include "ColourSchemes/OELogoColourScheme.h"
#include "ColourSchemes/TropicalColourScheme.h"

#define MS_FROM_START Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - start) * 1000

#include <cmath>
#include <charconv>
#include <math.h>
#include <numeric>
#include <unordered_map>
#include <vector>

using namespace LfpViewer;

#pragma mark - LfpDisplay -
// ---------------------------------------------------------------

LfpDisplay::LfpDisplay (LfpDisplaySplitter* c, Viewport* v)
    : singleChan (-1),
      canvasSplit (c),
      viewport (v),
      channelsReversed (false),
      channelsOrderedByDepth (false),
      displaySkipAmt (0),
      m_SpikeRasterPlottingFlag (false),
      lastBitmapIndex (0),
      lastFillFrom (-1),
      totalPixelsFilled (0)
{
    perPixelPlotter = std::make_unique<PerPixelBitmapPlotter> (this);
    supersampledPlotter = std::make_unique<SupersampledBitmapPlotter> (this);

    colourSchemeList.add (new DefaultColourScheme());
    colourSchemeList.add (new MonochromeGrayColourScheme());
    colourSchemeList.add (new MonochromeYellowColourScheme());
    colourSchemeList.add (new MonochromePurpleColourScheme());
    colourSchemeList.add (new MonochromeGreenColourScheme());
    colourSchemeList.add (new OELogoColourScheme());
    colourSchemeList.add (new TropicalColourScheme());
    colourSchemeList.add (new LightBackgroundColourScheme());

    plotter = perPixelPlotter.get();
    m_MedianOffsetPlottingFlag = false;

    activeColourScheme = 0;
    totalHeight = 0;
    colourGrouping = "1";

    //hand-built palette (used for event channels)
    channelColours.add (Colour (224, 185, 36));
    channelColours.add (Colour (214, 210, 182));
    channelColours.add (Colour (243, 119, 33));
    channelColours.add (Colour (186, 157, 168));
    channelColours.add (Colour (237, 37, 36));
    channelColours.add (Colour (179, 122, 79));
    channelColours.add (Colour (217, 46, 171));
    channelColours.add (Colour (217, 139, 196));
    channelColours.add (Colour (101, 31, 255));
    channelColours.add (Colour (141, 111, 181));
    channelColours.add (Colour (48, 117, 255));
    channelColours.add (Colour (184, 198, 224));
    channelColours.add (Colour (116, 227, 156));
    channelColours.add (Colour (150, 158, 155));
    channelColours.add (Colour (82, 173, 0));
    channelColours.add (Colour (125, 99, 32));

    range[0] = 1000; // headstage channels
    range[1] = 500; // aux channels
    range[2] = 500000; // adc channels

    scrollX = 0;
    scrollY = 0;

    addMouseListener (this, true);

    for (int ttlLine = 0; ttlLine < 8; ttlLine++)
    {
        eventDisplayEnabled[ttlLine] = true;
    }

    numChans = 0;
}

LfpDisplay::~LfpDisplay()
{
    invalidateStableChannelIdentities();
}

int LfpDisplay::getNumChannels()
{
    return numChans;
}

const String& LfpDisplay::getColourGrouping()
{
    return colourGrouping;
}

void LfpDisplay::setColourGrouping (const String& grouping)
{
    colourGrouping = grouping;

    if (grouping.equalsIgnoreCase ("By Shank"))
    {
        // Set colour grouping to 1 to allow shank grouping
        // by using the shank index to determine the colour
        getColourSchemePtr()->setColourGrouping (1);
    }
    else
    {
        getColourSchemePtr()->setColourGrouping (grouping.getIntValue());
    }

    setColours(); // so that channel colours get re-assigned
}

ChannelColourScheme* LfpDisplay::getColourSchemePtr()
{
    return colourSchemeList[activeColourScheme];
}

void LfpDisplay::updateRange (int i)
{
    // Check if this is an AUX channel with auto-scaling enabled
    if (channels[i]->getType() == ContinuousChannel::Type::AUX && options->isAuxAutoScaleEnabled())
    {
        // Apply individual auto-scaling based on this channel's InputRange
        float rangeMin = canvasSplit->displayBuffer->channelMetadata[i].inputRangeMin;
        float rangeMax = canvasSplit->displayBuffer->channelMetadata[i].inputRangeMax;
        float autoRange = rangeMax - rangeMin;

        if (autoRange > 0.0f)
        {
            channels[i]->setRange (autoRange);
            channelInfo[i]->setRange (autoRange);
        }
        else
        {
            // Fall back to the default range for this type
            channels[i]->setRange (range[channels[i]->getType()]);
            channelInfo[i]->setRange (range[channels[i]->getType()]);
        }
    }
    else
    {
        // Use the standard range for the channel type
        channels[i]->setRange (range[channels[i]->getType()]);
        channelInfo[i]->setRange (range[channels[i]->getType()]);
    }
}

void LfpDisplay::setNumChannels (int newChannelCount)
{
    newChannelCount =
        jmax (0, newChannelCount);
    if (newChannelCount == 0)
    {
        const auto streamKey =
            canvasSplit != nullptr
                ? canvasSplit
                      ->getStreamKey()
                : String();
        if (streamKey.isNotEmpty())
        {
            clearStoredVisibilityForStream (
                streamKey);
        }
    }
    beginStableChannelIdentityBulkMutation();
    invalidateStableChannelIdentities();

    if (numChans > newChannelCount)
    {
        for (int i = newChannelCount; i < numChans; i++)
        {
            removeChildComponent (channels[i]);
            removeChildComponent (channelInfo[i]);
        }

        channels.removeLast (numChans - newChannelCount);
        channelInfo.removeLast (numChans - newChannelCount);
    }
    stableChannelIdentityBindingSlots.resize (
        static_cast<size_t> (
            newChannelCount));

    totalHeight = 0;

    cachedDisplayChannelHeight = canvasSplit->getChannelHeight();

    if (newChannelCount > 0)
    {
        for (int i = 0; i < newChannelCount; i++)
        {
            LfpChannelDisplay* lfpChan;
            LfpChannelDisplayInfo* lfpInfo;

            if (i >= numChans)
            {
                // std::cout << "ADDING NEW CHANNEL " << i << std::endl;

                lfpChan = new LfpChannelDisplay (canvasSplit, this, options, i);
                channels.add (lfpChan);

                lfpInfo = new LfpChannelDisplayInfo (canvasSplit, this, options, i);
                channelInfo.add (lfpInfo);
            }
            else
            {
                lfpChan = channels[i];
                lfpInfo = channelInfo[i];
            }

            auto& identitySlot =
                stableChannelIdentityBindingSlots[
                    static_cast<size_t> (
                        i)];
            if (identitySlot == nullptr)
            {
                identitySlot =
                    std::make_shared<
                        LfpStableChannelIdentityBindingSlot>();
            }
            lfpChan
                ->bindStableChannelIdentitySlot (
                    identitySlot);
            lfpInfo
                ->bindStableChannelIdentitySlot (
                    identitySlot);

            lfpChan->setChannelHeight (canvasSplit->getChannelHeight());
            lfpInfo->setChannelHeight (canvasSplit->getChannelHeight());

            lfpChan->setEnabledState (true);
            lfpInfo->setEnabledState (true);

            totalHeight += lfpChan->getChannelHeight();
        }
    }

    numChans = newChannelCount;
    endStableChannelIdentityBulkMutation();
}

void LfpDisplay::setColours()
{
    if (drawableChannels.size() == 0)
        return;

    if (! getSingleChannelState())
    {
        for (int i = 0; i < drawableChannels.size(); i++)
        {
            if (colourGrouping.equalsIgnoreCase ("By Shank"))
            {
                auto* channel = drawableChannels[i].channel;
                auto* info = drawableChannels[i].channelInfo;

                if (channel->hasGroupMetadata())
                {
                    const int colourIdx = (channel->getGroup() * 2) % 8;
                    channel->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
                    info->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
                }
                else
                {
                    /*
                    Legacy depth-based shank colouring ranges:
                    Shank 1: depth < 10000
                    Shank 2: 10000 ≤ depth < 20000
                    Shank 3: 20000 ≤ depth < 30000
                    Shank 4: depth ≥ 30000
                    */
                    const int depth = channel->getDepth();
                    const int colourIdx = depth < 10000 ? 0 : depth < 20000 ? 2
                                                          : depth < 30000   ? 4
                                                                            : 6;
                    channel->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
                    info->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
                }
            }
            else
            {
                drawableChannels[i].channel->setColour (getColourSchemePtr()->getColourForIndex (i));
                drawableChannels[i].channelInfo->setColour (getColourSchemePtr()->getColourForIndex (i));
            }
        }
    }
    else
    {
        if (colourGrouping.equalsIgnoreCase ("By Shank"))
        {
            auto* channel = drawableChannels[0].channel;
            auto* info = drawableChannels[0].channelInfo;

            if (channel->hasGroupMetadata())
            {
                const int colourIdx = channel->getGroup();
                channel->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
                info->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
            }
            else
            {
                const int depth = channel->getDepth();
                const int colourIdx = depth < 10000 ? 0 : depth < 20000 ? 2
                                                      : depth < 30000   ? 4
                                                                        : 6;
                channel->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
                info->setColour (getColourSchemePtr()->getColourForIndex (colourIdx));
            }
        }
        else
        {
            drawableChannels[0].channel->setColour (getColourSchemePtr()->getColourForIndex (singleChan));
            drawableChannels[0].channelInfo->setColour (getColourSchemePtr()->getColourForIndex (singleChan));
        }
    }

    if (displayIsPaused)
    {
        timeOffsetChanged = true;
        canRefresh = true;
    }
    else
    {
        canvasSplit->fullredraw = true;
        colourSchemeChanged = true;

        refresh();
    }
}

void LfpDisplay::
    invalidateStableChannelIdentities()
{
    for (const auto& slot :
         stableChannelIdentityBindingSlots)
    {
        if (slot != nullptr)
            slot->revoke();
    }
    for (const auto& blueprint :
         stableChannelIdentityBlueprints)
    {
        if (blueprint != nullptr)
        {
            blueprint
                ->revokeAgentActionability();
        }
    }
    stableChannelIdentityBlueprints
        .clear();
    stableChannelIdentityRefreshPending =
        false;
}

void LfpDisplay::
    bindStableChannelIdentities (
        const std::vector<
            std::shared_ptr<
                const LfpStableChannelIdentity>>&
            identities,
        const Array<DisplayBuffer*>&
            availableStreams)
{
    beginStableChannelIdentityBulkMutation();
    invalidateStableChannelIdentities();
    stableChannelIdentityBlueprints =
        identities;
    for (const auto& blueprint :
         stableChannelIdentityBlueprints)
    {
        if (blueprint != nullptr)
        {
            blueprint
                ->revokeAgentActionability();
        }
    }
    commitStagedWaveformVisibilityState (
        availableStreams);
    pruneStoredVisibilityAfterBind();
    reconcileFocusedChannelAfterBind();
    applyStoredChannelVisibilityAfterBind();
    const auto count =
        jmin (
            channels.size(),
            channelInfo.size(),
            static_cast<int> (
                identities.size()),
            static_cast<int> (
                stableChannelIdentityBindingSlots
                    .size()));
    const auto drawableMask =
        createCurrentDrawableChannelMask();
    const bool paneIsAvailable =
        canvasSplit != nullptr
        && canvasSplit
               ->isIdentityTargetAvailable();
    auto* actionOwner =
        canvasSplit != nullptr
            ? canvasSplit
                  ->findParentComponentOfClass<
                      LfpDisplayCanvas>()
            : nullptr;
    const auto actionOwnerState =
        actionOwner != nullptr
            ? actionOwner
                  ->getStableChannelActionOwnerState()
            : std::weak_ptr<
                  const LfpStableChannelActionOwnerState>();
    for (int index = 0;
         index < count;
         ++index)
    {
        const auto& blueprint =
            stableChannelIdentityBlueprints[
                static_cast<size_t> (
                    index)];
#if BUILD_TESTS
        notifyStableIdentityLifecycleTestHook (
            StableIdentityLifecycleTestPhase::
                beforePairPublication,
            index);
#endif
#if BUILD_TESTS
        notifyStableIdentityLifecycleTestHook (
            StableIdentityLifecycleTestPhase::
                afterChannelPublicationBeforeInfo,
            index);
#endif
        auto* channel = channels[index];
        auto* info = channelInfo[index];
        const bool pairIsAvailable =
            paneIsAvailable
            && drawableMask[
                   static_cast<size_t> (
                       index)]
            && channel->getEnabledState()
            && info->getEnabledState()
            && ! channel->getHidden()
            && ! info->getHidden()
            && channel->isVisible()
            && info->isVisible()
            && channel->Component::isEnabled()
            && info->Component::isEnabled();
        if (pairIsAvailable)
        {
            stableChannelIdentityBindingSlots[
                static_cast<size_t> (
                    index)]
                ->publishSuccessor (
                    blueprint,
                    actionOwnerState);
        }
#if BUILD_TESTS
        notifyStableIdentityLifecycleTestHook (
            StableIdentityLifecycleTestPhase::
                afterPairPublication,
            index);
#endif
    }
    endStableChannelIdentityBulkMutation();
}

LfpDisplay::StableChannelVisibilityKey::
    StableChannelVisibilityKey (
        int paneIndex_,
        String streamKey_,
        const LfpStableChannelKey&
            stableChannelKey_)
    : paneIndex (paneIndex_),
      streamKey (
          std::move (
              streamKey_)),
      stableChannelKey (
          stableChannelKey_)
{
}

bool LfpDisplay::StableChannelVisibilityKey::
    operator== (
        const StableChannelVisibilityKey&
            other) const noexcept
{
    return paneIndex
               == other.paneIndex
        && streamKey
               == other.streamKey
        && stableChannelKey
               == other.stableChannelKey;
}

size_t LfpDisplay::
    StableChannelVisibilityKeyHash::
    operator() (
        const StableChannelVisibilityKey&
            key) const noexcept
{
    size_t result =
        std::hash<int> {} (
            key.paneIndex);
    const auto combine =
        [&result] (size_t value)
        {
            result ^= value
                + static_cast<size_t> (
                    0x9e3779b9)
                + (result << 6)
                + (result >> 2);
        };
    combine (
        std::hash<std::string> {} (
            key.streamKey
                .toStdString()));
    combine (
        std::hash<int> {} (
            static_cast<int> (
                key.stableChannelKey
                    .getKind())));
    if (key.stableChannelKey
            .getKind()
        == LfpStableChannelKey::Kind::
               identifier)
    {
        combine (
            std::hash<std::string> {} (
                key.stableChannelKey
                    .getIdentifier()
                    .toStdString()));
    }
    else
    {
        combine (
            std::hash<int> {} (
                key.stableChannelKey
                    .getSourceNodeId()));
        combine (
            std::hash<int> {} (
                key.stableChannelKey
                    .getLocalIndex()));
    }
    return result;
}

std::optional<
    LfpDisplay::StableChannelVisibilityKey>
LfpDisplay::
    getStableChannelVisibilityKey (
        int channelIndex) const
{
    if (channelIndex < 0
        || channelIndex
               >= static_cast<int> (
                   stableChannelIdentityBlueprints
                       .size()))
    {
        return std::nullopt;
    }

    const auto& identity =
        stableChannelIdentityBlueprints[
            static_cast<size_t> (
                channelIndex)];
    const auto* stableChannelKey =
        identity != nullptr
            ? identity
                  ->getStableChannelKey()
            : nullptr;
    if (identity == nullptr
        || stableChannelKey == nullptr
        || identity->getPaneIndex() < 0
        || identity
               ->getStreamKey()
               .isEmpty()
        || identity
               ->getRuntimeUuid()
               == Uuid::null()
        || canvasSplit == nullptr
        || canvasSplit->splitID
               != identity
                      ->getPaneIndex()
        || canvasSplit
                   ->getStreamKey()
               != identity
                      ->getStreamKey())
    {
        return std::nullopt;
    }

    return StableChannelVisibilityKey (
        identity->getPaneIndex(),
        identity->getStreamKey(),
        *stableChannelKey);
}

bool LfpDisplay::
    getStoredChannelVisibility (
        int channelIndex) const
{
    const auto key =
        getStableChannelVisibilityKey (
            channelIndex);
    return ! key.has_value()
        || hiddenStableChannels.find (
               *key)
               == hiddenStableChannels.end();
}

void LfpDisplay::
    reconcileFocusedChannelAfterBind()
{
    if (! getSingleChannelState())
    {
        focusedStableChannel.reset();
        return;
    }

    int matchingChannel = -1;
    if (focusedStableChannel.has_value())
    {
        for (int index = 0;
             index
                 < static_cast<int> (
                     stableChannelIdentityBlueprints
                         .size());
             ++index)
        {
            const auto key =
                getStableChannelVisibilityKey (
                    index);
            if (key.has_value()
                && *key
                       == *focusedStableChannel)
            {
                matchingChannel = index;
                break;
            }
        }
    }

    if (matchingChannel >= 0)
    {
        singleChan = matchingChannel;
        return;
    }

    if (drawableChannels.size() == 1
        && drawableChannels[0].channelInfo
               != nullptr)
    {
        drawableChannels[0]
            .channelInfo
            ->setSingleChannelState (
                false);
    }
    singleChan = -1;
    focusedStableChannel.reset();
}

void LfpDisplay::
    pruneStoredVisibilityAfterBind()
{
    if (canvasSplit == nullptr)
        return;

    const auto paneIndex =
        canvasSplit->splitID;
    const auto streamKey =
        canvasSplit
            ->getStreamKey();
    if (paneIndex < 0
        || streamKey.isEmpty())
    {
        return;
    }

    std::unordered_set<
        StableChannelVisibilityKey,
        StableChannelVisibilityKeyHash>
        currentKeys;
    currentKeys.reserve (
        stableChannelIdentityBlueprints
            .size());
    for (int index = 0;
         index
             < static_cast<int> (
                 stableChannelIdentityBlueprints
                     .size());
         ++index)
    {
        const auto key =
            getStableChannelVisibilityKey (
                index);
        if (key.has_value())
        {
            currentKeys.insert (
                *key);
        }
    }

    for (auto iterator =
             hiddenStableChannels.begin();
         iterator
             != hiddenStableChannels.end();)
    {
        if (iterator->paneIndex
                    == paneIndex
            && iterator->streamKey
                   == streamKey
            && currentKeys.find (
                   *iterator)
                   == currentKeys.end())
        {
            iterator =
                hiddenStableChannels.erase (
                    iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

void LfpDisplay::
    clearStoredVisibilityForStream (
        const String& streamKey)
{
    if (streamKey.isEmpty())
        return;

    for (auto iterator =
             hiddenStableChannels.begin();
         iterator
             != hiddenStableChannels.end();)
    {
        if (iterator->streamKey
            == streamKey)
        {
            iterator =
                hiddenStableChannels.erase (
                    iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

void LfpDisplay::
    pruneStoredVisibilityForAvailableStreams (
        const Array<DisplayBuffer*>&
            availableStreams)
{
    std::unordered_map<
        std::string,
        int>
        streamKeyCounts;
    for (const auto* stream :
         availableStreams)
    {
        if (stream != nullptr)
        {
            ++streamKeyCounts[
                stream->streamKey
                    .toStdString()];
        }
    }

    for (auto iterator =
             hiddenStableChannels.begin();
         iterator
             != hiddenStableChannels.end();)
    {
        const auto found =
            streamKeyCounts.find (
                iterator->streamKey
                    .toStdString());
        if (iterator->streamKey.isEmpty()
            || found
                   == streamKeyCounts.end()
            || found->second != 1)
        {
            iterator =
                hiddenStableChannels.erase (
                    iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

void LfpDisplay::
    applyStoredChannelVisibilityAfterBind()
{
    const auto count =
        jmin (
            channels.size(),
            channelInfo.size(),
            static_cast<int> (
                stableChannelIdentityBlueprints
                    .size()));
    for (int index = 0;
         index < count;
         ++index)
    {
        const auto visible =
            getStoredChannelVisibility (
                index);
        channels[index]
            ->setEnabledState (
                visible);
        channelInfo[index]
            ->setEnabledState (
                visible);
    }
}

void LfpDisplay::
    saveWaveformVisibilityState (
        XmlElement& paneNode,
        const Array<DisplayBuffer*>&
            availableStreams) const
{
    struct HiddenRecord
    {
        String streamKey;
        LfpStableChannelKey::Kind
            stableKeyKind;
        String stableIdentifier;
        int stableSourceNodeId;
        int stableLocalIndex;
        String persistedIdentifier;
        int persistedSourceNodeId;
        int persistedLocalIndex;
        String channelName;
        ContinuousChannel::Type channelType;
    };

    std::vector<HiddenRecord>
        hiddenRecords;
    for (const auto* stream :
         availableStreams)
    {
        if (stream == nullptr
            || stream->streamKey
                   .trim()
                   .isEmpty())
            continue;

        const auto identities =
            resolveStableChannelIdentities (
                canvasSplit != nullptr
                    ? canvasSplit->splitID
                    : -1,
                *stream,
                availableStreams);
        for (const auto& identity :
             identities)
        {
            const auto* stableKey =
                identity != nullptr
                    ? identity
                          ->getStableChannelKey()
                    : nullptr;
            if (identity == nullptr
                || stableKey == nullptr)
            {
                continue;
            }

            const StableChannelVisibilityKey
                visibilityKey (
                    identity->getPaneIndex(),
                    identity->getStreamKey(),
                    *stableKey);
            if (hiddenStableChannels.find (
                    visibilityKey)
                == hiddenStableChannels.end())
            {
                continue;
            }
            if (stableKey->getKind()
                    == LfpStableChannelKey::
                           Kind::identifier
                && (identity
                            ->getPersistedSourceNodeId()
                        < -1
                    || identity
                               ->getPersistedLocalIndex()
                           < -1))
            {
                continue;
            }

            hiddenRecords.push_back (
                { identity->getStreamKey(),
                  stableKey->getKind(),
                  stableKey->getIdentifier(),
                  stableKey->getSourceNodeId(),
                  stableKey->getLocalIndex(),
                  identity
                      ->getPersistedIdentifier(),
                  identity
                      ->getPersistedSourceNodeId(),
                  identity
                      ->getPersistedLocalIndex(),
                  identity
                      ->getPersistedChannelName(),
                  identity
                      ->getPersistedChannelType() });
        }
    }

    std::sort (
        hiddenRecords.begin(),
        hiddenRecords.end(),
        [] (const HiddenRecord& left,
            const HiddenRecord& right)
        {
            const auto streamOrder =
                left.streamKey.compare (
                    right.streamKey);
            if (streamOrder != 0)
                return streamOrder < 0;
            if (left.stableKeyKind
                != right.stableKeyKind)
            {
                return static_cast<int> (
                           left.stableKeyKind)
                    < static_cast<int> (
                          right.stableKeyKind);
            }
            if (left.stableKeyKind
                == LfpStableChannelKey::Kind::
                       identifier)
            {
                return left.stableIdentifier
                           .compare (
                               right
                                   .stableIdentifier)
                    < 0;
            }
            if (left.stableSourceNodeId
                != right.stableSourceNodeId)
            {
                return left.stableSourceNodeId
                    < right.stableSourceNodeId;
            }
            return left.stableLocalIndex
                < right.stableLocalIndex;
        });

    auto* stateNode =
        paneNode.createNewChildElement (
            "WAVEFORM_VISIBILITY_STATE");
    stateNode->setAttribute (
        "version",
        2);
    for (const auto& record :
         hiddenRecords)
    {
        auto* recordNode =
            stateNode
                ->createNewChildElement (
                    "HIDDEN_CHANNEL");
        recordNode->setAttribute (
            "stream_key",
            record.streamKey);
        recordNode->setAttribute (
            "stable_key_kind",
            record.stableKeyKind
                    == LfpStableChannelKey::
                           Kind::identifier
                ? "identifier"
                : "source_local");
        if (record.persistedIdentifier
                .trim()
                .isNotEmpty())
        {
            recordNode->setAttribute (
                "identifier",
                record
                    .persistedIdentifier);
        }
        recordNode->setAttribute (
            "source_node_id",
            record.persistedSourceNodeId);
        recordNode->setAttribute (
            "local_index",
            record.persistedLocalIndex);
        recordNode->setAttribute (
            "channel_name",
            record.channelName);
        recordNode->setAttribute (
            "channel_type",
            static_cast<int> (
                record.channelType));
    }
}

void LfpDisplay::
    stageWaveformVisibilityState (
        const XmlElement& paneNode)
{
    stagedWaveformVisibilityRecords
        .clear();
    stagedLegacyChannelDisplayState
        .clear();
    stagedLegacyStreamKey.clear();
    stagedLegacyStreamKeyPresent =
        false;
    stagedWaveformVisibilityKind =
        StagedWaveformVisibilityKind::
            none;
    stagedWaveformVisibilityPending =
        true;

    const XmlElement* stateNode =
        nullptr;
    int stateNodeCount = 0;
    for (const auto* child :
         paneNode.getChildIterator())
    {
        if (child->hasTagName (
                "WAVEFORM_VISIBILITY_STATE"))
        {
            ++stateNodeCount;
            stateNode = child;
        }
    }

    if (stateNodeCount == 0)
    {
        stagedWaveformVisibilityKind =
            StagedWaveformVisibilityKind::
                legacy;
        stagedLegacyChannelDisplayState =
            paneNode
                .getStringAttribute (
                    "ChannelDisplayState");
        stagedLegacyStreamKeyPresent =
            paneNode.hasAttribute (
                "stream_key");
        stagedLegacyStreamKey =
            paneNode
                .getStringAttribute (
                    "stream_key");
        return;
    }

    stagedWaveformVisibilityKind =
        StagedWaveformVisibilityKind::
            version2;
    if (stateNodeCount != 1
        || stateNode == nullptr
        || ! stateNode->hasAttribute (
            "version")
        || stateNode
                   ->getStringAttribute (
                       "version")
               != "2")
    {
        return;
    }

    const auto parseStrictInteger =
        [] (const XmlElement& element,
            const char* attribute,
            int& result)
        {
            if (! element.hasAttribute (
                    attribute))
            {
                return false;
            }
            const auto text =
                element
                    .getStringAttribute (
                        attribute)
                    .toStdString();
            if (text.empty())
                return false;

            int parsed = 0;
            const auto conversion =
                std::from_chars (
                    text.data(),
                    text.data()
                        + text.size(),
                    parsed);
            if (conversion.ec
                    != std::errc()
                || conversion.ptr
                       != text.data()
                              + text.size())
            {
                return false;
            }
            result = parsed;
            return true;
        };

    for (const auto* child :
         stateNode->getChildIterator())
    {
        StagedWaveformVisibilityRecord
            record;
        if (! child->hasTagName (
                "HIDDEN_CHANNEL"))
        {
            stagedWaveformVisibilityRecords
                .push_back (
                    std::move (
                        record));
            continue;
        }

        record.streamKey =
            child->getStringAttribute (
                "stream_key");
        const auto stableKeyKind =
            child->getStringAttribute (
                "stable_key_kind");
        if (stableKeyKind
            == "identifier")
        {
            record.stableKeyKind =
                LfpStableChannelKey::Kind::
                    identifier;
        }
        else if (stableKeyKind
                 == "source_local")
        {
            record.stableKeyKind =
                LfpStableChannelKey::Kind::
                    sourceAndLocalIndex;
        }
        else
        {
            stagedWaveformVisibilityRecords
                .push_back (
                    std::move (
                        record));
            continue;
        }

        record.identifier =
            child->getStringAttribute (
                "identifier");
        record.channelName =
            child->getStringAttribute (
                "channel_name");
        int channelType = -1;
        const bool sourceNodeIdIsValid =
            parseStrictInteger (
                *child,
                "source_node_id",
                record.sourceNodeId);
        const bool localIndexIsValid =
            parseStrictInteger (
                *child,
                "local_index",
                record.localIndex);
        const bool channelTypeWasParsed =
            parseStrictInteger (
                *child,
                "channel_type",
                channelType);
        const bool integersAreValid =
            sourceNodeIdIsValid
            && localIndexIsValid
            && channelTypeWasParsed;
        const bool identifierIsValid =
            record.stableKeyKind
                    != LfpStableChannelKey::
                           Kind::identifier
            || (child->hasAttribute (
                    "identifier")
                && record.identifier
                       .trim()
                       .isNotEmpty());
        const bool sourceLocalIsValid =
            record.stableKeyKind
                    != LfpStableChannelKey::
                           Kind::
                               sourceAndLocalIndex
            || (sourceNodeIdIsValid
                && localIndexIsValid
                && record.sourceNodeId >= 0
                && record.localIndex >= 0);
        const bool identifierAuxiliaryValuesAreValid =
            record.stableKeyKind
                    != LfpStableChannelKey::
                           Kind::identifier
            || (sourceNodeIdIsValid
                && localIndexIsValid
                && record.sourceNodeId >= -1
                && record.localIndex >= -1);
        const bool channelTypeIsValid =
            isPersistedChannelTypeValueAccepted (
                channelType);
        bool hasOnlyCanonicalAttributes =
            true;
        for (int attribute = 0;
             attribute
             < child->getNumAttributes();
             ++attribute)
        {
            const auto attributeName =
                child->getAttributeName (
                    attribute);
            if (attributeName
                        != "stream_key"
                && attributeName
                       != "stable_key_kind"
                && attributeName
                       != "identifier"
                && attributeName
                       != "source_node_id"
                && attributeName
                       != "local_index"
                && attributeName
                       != "channel_name"
                && attributeName
                       != "channel_type")
            {
                hasOnlyCanonicalAttributes =
                    false;
                break;
            }
        }
        record.stableKeyWellFormed =
            record.streamKey
                .trim()
                .isNotEmpty()
            && identifierIsValid
            && sourceLocalIsValid;
        record.wellFormed =
            record.stableKeyWellFormed
            && child->hasAttribute (
                "channel_name")
            && integersAreValid
            && identifierAuxiliaryValuesAreValid
            && hasOnlyCanonicalAttributes
            && channelTypeIsValid;
        if (channelTypeIsValid)
        {
            record.channelType =
                static_cast<
                    ContinuousChannel::Type> (
                    channelType);
        }
        stagedWaveformVisibilityRecords
            .push_back (
                std::move (
                    record));
    }
}

void LfpDisplay::
    stageInvalidWaveformVisibilityState()
{
    stagedWaveformVisibilityRecords
        .clear();
    stagedLegacyChannelDisplayState
        .clear();
    stagedLegacyStreamKey.clear();
    stagedLegacyStreamKeyPresent =
        false;
    stagedWaveformVisibilityKind =
        StagedWaveformVisibilityKind::
            version2;
    stagedWaveformVisibilityPending =
        true;
}

bool LfpDisplay::
    isPersistedChannelTypeValueAccepted (
        int value) noexcept
{
    return value
               == static_cast<int> (
                   ContinuousChannel::Type::
                       ELECTRODE)
        || value
               == static_cast<int> (
                   ContinuousChannel::Type::
                       AUX)
        || value
               == static_cast<int> (
                   ContinuousChannel::Type::
                       ADC)
        || value
               == static_cast<int> (
                   ContinuousChannel::Type::
                       INVALID);
}

void LfpDisplay::
    commitStagedWaveformVisibilityState (
        const Array<DisplayBuffer*>&
            availableStreams)
{
    if (! stagedWaveformVisibilityPending)
        return;

    if (availableStreams.isEmpty())
        return;

    for (const auto* stream :
         availableStreams)
    {
        if (stream == nullptr
            || stream->numChannels < 0
            || stream
                       ->channelMetadata
                       .size()
                   != stream->numChannels)
        {
            return;
        }
    }

    std::unordered_set<
        StableChannelVisibilityKey,
        StableChannelVisibilityKeyHash>
        resolvedHiddenChannels;

    if (stagedWaveformVisibilityKind
        == StagedWaveformVisibilityKind::
               version2)
    {
        struct CurrentMetadataRow
        {
            std::optional<
                StableChannelVisibilityKey>
                visibilityKey;
            String streamKey;
            String identifier;
            int sourceNodeId;
            int localIndex;
            String channelName;
            ContinuousChannel::Type
                channelType;
        };
        struct IdentifierIndexKey
        {
            std::string streamKey;
            std::string identifier;

            bool operator== (
                const IdentifierIndexKey&
                    other) const noexcept
            {
                return streamKey
                           == other.streamKey
                    && identifier
                           == other.identifier;
            }
        };
        struct IdentifierIndexKeyHash
        {
            size_t operator() (
                const IdentifierIndexKey&
                    key) const noexcept
            {
                const auto first =
                    std::hash<std::string> {} (
                        key.streamKey);
                const auto second =
                    std::hash<std::string> {} (
                        key.identifier);
                return first
                    ^ (second
                       + static_cast<size_t> (
                           0x9e3779b9)
                       + (first << 6)
                       + (first >> 2));
            }
        };
        struct SourceLocalIndexKey
        {
            std::string streamKey;
            int sourceNodeId;
            int localIndex;

            bool operator== (
                const SourceLocalIndexKey&
                    other) const noexcept
            {
                return streamKey
                           == other.streamKey
                    && sourceNodeId
                           == other.sourceNodeId
                    && localIndex
                           == other.localIndex;
            }
        };
        struct SourceLocalIndexKeyHash
        {
            size_t operator() (
                const SourceLocalIndexKey&
                    key) const noexcept
            {
                auto result =
                    std::hash<std::string> {} (
                        key.streamKey);
                const auto sourceHash =
                    std::hash<int> {} (
                        key.sourceNodeId);
                const auto localHash =
                    std::hash<int> {} (
                        key.localIndex);
                result ^= sourceHash
                    + static_cast<size_t> (
                        0x9e3779b9)
                    + (result << 6)
                    + (result >> 2);
                result ^= localHash
                    + static_cast<size_t> (
                        0x9e3779b9)
                    + (result << 6)
                    + (result >> 2);
                return result;
            }
        };
        std::vector<CurrentMetadataRow>
            currentRows;
        size_t currentRowCapacity =
            0;
        for (const auto* stream :
             availableStreams)
        {
            currentRowCapacity +=
                static_cast<size_t> (
                    stream
                        ->channelMetadata
                        .size());
        }
        currentRows.reserve (
            currentRowCapacity);
        for (const auto* stream :
             availableStreams)
        {
            const auto identities =
                resolveStableChannelIdentities (
                    canvasSplit != nullptr
                        ? canvasSplit->splitID
                        : -1,
                    *stream,
                    availableStreams);
            for (int channel = 0;
                 channel
                 < stream->channelMetadata
                       .size();
                 ++channel)
            {
                const auto& metadata =
                    stream->channelMetadata[
                        channel];
                const auto& identity =
                    channel
                            < static_cast<int> (
                                  identities
                                      .size())
                        ? identities[
                              static_cast<
                                  size_t> (
                                  channel)]
                        : nullptr;
                const auto* stableKey =
                    identity != nullptr
                        ? identity
                              ->getStableChannelKey()
                        : nullptr;
                std::optional<
                    StableChannelVisibilityKey>
                    visibilityKey;
                if (identity != nullptr
                    && stableKey != nullptr)
                {
                    visibilityKey.emplace (
                        identity
                            ->getPaneIndex(),
                        identity
                            ->getStreamKey(),
                        *stableKey);
                }
                currentRows
                    .push_back (
                        { std::move (
                              visibilityKey),
                          stream->streamKey,
                          metadata.identifier,
                          metadata.sourceNodeId,
                          metadata.localIndex,
                          metadata.name,
                          metadata.type });
#if BUILD_TESTS
                ++waveformVisibilityResolutionWorkForTests;
#endif
            }
        }

        std::unordered_map<
            IdentifierIndexKey,
            std::vector<size_t>,
            IdentifierIndexKeyHash>
            identifierIndex;
        std::unordered_map<
            SourceLocalIndexKey,
            std::vector<size_t>,
            SourceLocalIndexKeyHash>
            sourceLocalIndex;
        identifierIndex.reserve (
            currentRows.size());
        sourceLocalIndex.reserve (
            currentRows.size());
        for (size_t index = 0;
             index
             < currentRows.size();
             ++index)
        {
            const auto& current =
                currentRows[index];
            if (current.identifier
                    .trim()
                    .isNotEmpty())
            {
                identifierIndex[
                    { current
                          .streamKey
                          .toStdString(),
                      current.identifier
                          .toStdString() }]
                    .push_back (
                        index);
            }
            if (current.sourceNodeId >= 0
                && current.localIndex >= 0)
            {
                sourceLocalIndex[
                    { current
                          .streamKey
                          .toStdString(),
                      current.sourceNodeId,
                      current.localIndex }]
                    .push_back (
                        index);
            }
        }

        struct ResolutionCount
        {
            int recordsForStableKey = 0;
            int matchingSignatures = 0;
        };
        std::unordered_map<
            StableChannelVisibilityKey,
            ResolutionCount,
            StableChannelVisibilityKeyHash>
            resolutionCounts;
        for (const auto& record :
             stagedWaveformVisibilityRecords)
        {
#if BUILD_TESTS
            ++waveformVisibilityResolutionWorkForTests;
#endif
            if (! record
                      .stableKeyWellFormed)
                continue;

            const std::vector<size_t>*
                matches = nullptr;
            if (record.stableKeyKind
                == LfpStableChannelKey::
                       Kind::identifier)
            {
                const auto match =
                    identifierIndex.find (
                        { record
                              .streamKey
                              .toStdString(),
                          record.identifier
                              .toStdString() });
                if (match
                    != identifierIndex.end())
                {
                    matches =
                        &match->second;
                }
            }
            else
            {
                const auto match =
                    sourceLocalIndex.find (
                        { record
                              .streamKey
                              .toStdString(),
                          record.sourceNodeId,
                          record.localIndex });
                if (match
                    != sourceLocalIndex.end())
                {
                    matches =
                        &match->second;
                }
            }

            if (matches == nullptr
                || matches->size() != 1)
            {
                continue;
            }

            const auto& uniqueMatch =
                currentRows[
                    matches->front()];
            if (! uniqueMatch
                      .visibilityKey
                      .has_value())
            {
                continue;
            }
            auto& count = resolutionCounts[
                *uniqueMatch.visibilityKey];
            ++count.recordsForStableKey;
            if (record.wellFormed
                && uniqueMatch.channelName
                       == record.channelName
                && uniqueMatch.channelType
                       == record.channelType)
            {
                ++count.matchingSignatures;
            }
        }

        for (const auto& resolution :
             resolutionCounts)
        {
            if (resolution.second
                        .recordsForStableKey
                    == 1
                && resolution.second
                           .matchingSignatures
                       == 1)
            {
                resolvedHiddenChannels
                    .insert (
                        resolution.first);
            }
        }
    }
    else if (
        stagedWaveformVisibilityKind
            == StagedWaveformVisibilityKind::
                   legacy)
    {
        const DisplayBuffer* stream =
            nullptr;
        if (stagedLegacyStreamKeyPresent)
        {
            int matchingStreams = 0;
            for (const auto* candidate :
                 availableStreams)
            {
                if (candidate->streamKey
                    == stagedLegacyStreamKey)
                {
                    ++matchingStreams;
                    stream = candidate;
                }
            }
            if (matchingStreams != 1)
                stream = nullptr;
        }
        else if (availableStreams.size()
                 == 1)
        {
            stream =
                availableStreams[0];
        }

        if (stream != nullptr)
        {
            const auto identities =
                resolveStableChannelIdentities (
                    canvasSplit != nullptr
                        ? canvasSplit->splitID
                        : -1,
                    *stream,
                    availableStreams);
            const auto migratedCount =
                jmin (
                    static_cast<int> (
                        identities.size()),
                    stagedLegacyChannelDisplayState
                        .length());
            for (int channel = 0;
                 channel < migratedCount;
                 ++channel)
            {
                if (stagedLegacyChannelDisplayState[
                        channel]
                    != '0')
                {
                    continue;
                }
                const auto& identity =
                    identities[
                        static_cast<size_t> (
                            channel)];
                const auto* stableKey =
                    identity != nullptr
                        ? identity
                              ->getStableChannelKey()
                        : nullptr;
                if (identity != nullptr
                    && stableKey != nullptr)
                {
                    resolvedHiddenChannels
                        .insert (
                            StableChannelVisibilityKey (
                                identity
                                    ->getPaneIndex(),
                                identity
                                    ->getStreamKey(),
                                *stableKey));
                }
            }
        }
    }

    hiddenStableChannels =
        std::move (
            resolvedHiddenChannels);
    stagedWaveformVisibilityRecords
        .clear();
    stagedLegacyChannelDisplayState
        .clear();
    stagedLegacyStreamKey.clear();
    stagedLegacyStreamKeyPresent =
        false;
    stagedWaveformVisibilityKind =
        StagedWaveformVisibilityKind::
            none;
    stagedWaveformVisibilityPending =
        false;
}

void LfpDisplay::
    refreshStableChannelIdentityAvailability()
{
    const auto count =
        jmin (
            channels.size(),
            channelInfo.size(),
            static_cast<int> (
                stableChannelIdentityBlueprints
                    .size()),
            static_cast<int> (
                stableChannelIdentityBindingSlots
                    .size()));
    const bool paneIsAvailable =
        canvasSplit != nullptr
        && canvasSplit
               ->isIdentityTargetAvailable();
    auto* actionOwner =
        canvasSplit != nullptr
            ? canvasSplit
                  ->findParentComponentOfClass<
                      LfpDisplayCanvas>()
            : nullptr;
    const auto actionOwnerState =
        actionOwner != nullptr
            ? actionOwner
                  ->getStableChannelActionOwnerState()
            : std::weak_ptr<
                  const LfpStableChannelActionOwnerState>();
    const auto drawableMask =
        createCurrentDrawableChannelMask();

    for (int index = 0;
         index < count;
         ++index)
    {
#if BUILD_TESTS
        ++stableIdentityAvailabilityWorkForTests;
#endif
        auto* channel = channels[index];
        auto* info = channelInfo[index];

        const bool pairIsAvailable =
            paneIsAvailable
            && drawableMask[
                   static_cast<size_t> (
                       index)]
            && channel->getEnabledState()
            && info->getEnabledState()
            && ! channel->getHidden()
            && ! info->getHidden()
            && channel->isVisible()
            && info->isVisible()
            && channel->Component::isEnabled()
            && info->Component::isEnabled();
        if (! pairIsAvailable)
        {
            stableChannelIdentityBindingSlots[
                static_cast<size_t> (
                    index)]
                ->revoke();
            continue;
        }

        const auto& slot =
            stableChannelIdentityBindingSlots[
                static_cast<size_t> (
                    index)];
        if (slot->get() != nullptr)
        {
            continue;
        }

        slot->publishSuccessor (
            stableChannelIdentityBlueprints[
                static_cast<size_t> (
                    index)],
            actionOwnerState);
    }
}

bool LfpDisplay::
    validateStableChannelAction (
        const std::shared_ptr<
            const LfpStableChannelIdentity>&
            retainedIdentity) const
{
    jassert (
        MessageManager::
            existsAndIsCurrentThread());
    if (retainedIdentity == nullptr
        || ! retainedIdentity
                ->isAgentActionable()
        || canvasSplit == nullptr
        || ! canvasSplit
                ->isIdentityTargetAvailable()
        || canvasSplit->splitID
               != retainedIdentity
                      ->getPaneIndex()
        || canvasSplit
                   ->getStreamKey()
               != retainedIdentity
                      ->getStreamKey()
        || ! isShowing()
        || ! isEnabled())
    {
        return false;
    }

    const auto count =
        jmin (
            channels.size(),
            channelInfo.size(),
            static_cast<int> (
                stableChannelIdentityBlueprints
                    .size()),
            static_cast<int> (
                stableChannelIdentityBindingSlots
                    .size()));
    for (int index = 0;
         index < count;
         ++index)
    {
        const auto& slot =
            stableChannelIdentityBindingSlots[
                static_cast<size_t> (
                    index)];
        if (slot == nullptr
            || slot->get()
                   != retainedIdentity)
        {
            continue;
        }

        auto* channel =
            channels[
                index];
        auto* info =
            channelInfo[
                index];
        if (channel == nullptr
            || info == nullptr)
        {
            return false;
        }

        const auto channelSlot =
            std::atomic_load_explicit (
                &channel
                     ->stableChannelIdentityBindingSlot,
                std::memory_order_acquire);
        const auto infoSlot =
            std::atomic_load_explicit (
                &info
                     ->stableChannelIdentityBindingSlot,
                std::memory_order_acquire);
        if (channelSlot != slot
            || infoSlot != slot
            || channel
                       ->getStableChannelIdentity()
                   != retainedIdentity
            || info
                       ->getStableChannelIdentity()
                   != retainedIdentity)
        {
            return false;
        }

        const auto& blueprint =
            stableChannelIdentityBlueprints[
                static_cast<size_t> (
                    index)];
        const auto* retainedKey =
            retainedIdentity
                ->getStableChannelKey();
        const auto* blueprintKey =
            blueprint != nullptr
                ? blueprint
                      ->getStableChannelKey()
                : nullptr;
        if (blueprint == nullptr
            || retainedKey == nullptr
            || blueprintKey == nullptr
            || ! (*retainedKey
                   == *blueprintKey)
            || blueprint
                       ->getPaneIndex()
                   != retainedIdentity
                          ->getPaneIndex()
            || blueprint
                       ->getStreamKey()
                   != retainedIdentity
                          ->getStreamKey()
            || blueprint
                       ->getRuntimeUuid()
                   != retainedIdentity
                          ->getRuntimeUuid()
            || channel->chan != index
            || info->chan != index
            || ! channel
                    ->getEnabledState()
            || ! info
                    ->getEnabledState()
            || channel
                   ->getHidden()
            || info
                   ->getHidden()
            || ! channel
                    ->isShowing()
            || ! info
                    ->isShowing()
            || ! channel
                    ->Component::
                    isEnabled()
            || ! info
                    ->Component::
                    isEnabled())
        {
            return false;
        }

        for (const auto& drawable :
             drawableChannels)
        {
            if (drawable.channel
                    == channel
                && drawable.channelInfo
                       == info)
            {
                return true;
            }
        }
        return false;
    }

    return false;
}

bool LfpDisplay::
    requestStableChannelVisibility (
        const std::shared_ptr<
            const LfpStableChannelIdentity>&
            retainedIdentity,
        LfpWaveformVisibility visibility)
{
    jassert (
        MessageManager::
            existsAndIsCurrentThread());
    if (! validateStableChannelAction (
            retainedIdentity))
    {
        return false;
    }

    const auto count =
        jmin (
            channels.size(),
            channelInfo.size(),
            static_cast<int> (
                stableChannelIdentityBindingSlots
                    .size()));
    for (int index = 0;
         index < count;
         ++index)
    {
        const auto& slot =
            stableChannelIdentityBindingSlots[
                static_cast<size_t> (
                    index)];
        if (slot != nullptr
            && slot->get()
                   == retainedIdentity)
        {
#if BUILD_TESTS
            notifyStableIdentityLifecycleTestHook (
                StableIdentityLifecycleTestPhase::
                    beforeVisibilityRequestMutation,
                index);
#endif
            setEnabledState (
                visibility
                    == LfpWaveformVisibility::
                           visible,
                index,
                true);
            return true;
        }
    }

    return false;
}

void LfpDisplay::
    prepareStableChannelIdentityTargetUnavailable()
{
    for (const auto& slot :
         stableChannelIdentityBindingSlots)
    {
        if (slot != nullptr)
            slot->revoke();
    }
#if BUILD_TESTS
    notifyStableIdentityLifecycleTestHook (
        StableIdentityLifecycleTestPhase::
            beforePaneVisibilityMutation,
        -1);
#endif
}

void LfpDisplay::
    requestStableChannelIdentityAvailabilityRefresh()
{
    if (stableChannelIdentityBulkMutationDepth
        > 0)
    {
        stableChannelIdentityRefreshPending =
            true;
        return;
    }
    refreshStableChannelIdentityAvailability();
}

void LfpDisplay::
    revokeStableChannelIdentityForChannel (
        int channelIndex)
{
    if (channelIndex < 0
        || channelIndex
               >= static_cast<int> (
                   stableChannelIdentityBindingSlots
                       .size()))
    {
        return;
    }
    const auto& slot =
        stableChannelIdentityBindingSlots[
            static_cast<size_t> (
                channelIndex)];
    if (slot != nullptr)
        slot->revoke();
#if BUILD_TESTS
    notifyStableIdentityLifecycleTestHook (
        StableIdentityLifecycleTestPhase::
            beforeChannelStateMutation,
        channelIndex);
#endif
}

void LfpDisplay::
    beginStableChannelIdentityBulkMutation()
{
    ++stableChannelIdentityBulkMutationDepth;
}

void LfpDisplay::
    endStableChannelIdentityBulkMutation()
{
    jassert (
        stableChannelIdentityBulkMutationDepth
        > 0);
    --stableChannelIdentityBulkMutationDepth;
    if (stableChannelIdentityBulkMutationDepth
            == 0
        && stableChannelIdentityRefreshPending)
    {
        stableChannelIdentityRefreshPending =
            false;
        refreshStableChannelIdentityAvailability();
    }
}

std::vector<uint8_t>
LfpDisplay::
    createCurrentDrawableChannelMask() const
{
    std::vector<uint8_t> mask (
        static_cast<size_t> (
            channels.size()),
        0);
    for (const auto& track :
         drawableChannels)
    {
#if BUILD_TESTS
        ++stableIdentityAvailabilityWorkForTests;
#endif
        if (track.channel == nullptr
            || track.channelInfo == nullptr)
        {
            continue;
        }
        const auto index =
            track.channel
                ->getChannelNumber();
        if (index >= 0
            && index < channels.size()
            && channels[index]
                   == track.channel
            && channelInfo[index]
                   == track.channelInfo)
        {
            mask[
                static_cast<size_t> (
                    index)] =
                1;
        }
    }
    return mask;
}

std::vector<uint8_t>
LfpDisplay::
    createDesiredDrawableChannelMask() const
{
    std::vector<uint8_t> mask (
        static_cast<size_t> (
            channels.size()),
        0);
    if (singleChan >= 0)
    {
        if (singleChan >= 0
            && singleChan < channels.size())
        {
            mask[
                static_cast<size_t> (
                    singleChan)] =
                1;
        }
        return mask;
    }

    Array<int> filteredChannels;
    if (canvasSplit->displayBuffer)
    {
        filteredChannels =
            canvasSplit
                ->getFilteredChannels();
    }
    int filterChannelIndex = 0;
    for (int index = 0;
         index < channels.size();
         ++index)
    {
#if BUILD_TESTS
        ++stableIdentityAvailabilityWorkForTests;
#endif
        const int channelNumber =
            filteredChannels.size()
            ? canvasSplit
                  ->displayBuffer
                  ->channelMetadata[
                      index]
                  .description
                  .getIntValue()
            : -1;
        while (
            filterChannelIndex
                < filteredChannels.size()
            && channelNumber
                   > filteredChannels[
                       filterChannelIndex])
        {
            ++filterChannelIndex;
        }
        const bool passesFilter =
            filteredChannels.isEmpty()
            || (filterChannelIndex
                    < filteredChannels.size()
                && channelNumber
                       == filteredChannels[
                           filterChannelIndex]);
        if (passesFilter)
        {
            if (displaySkipAmt == 0
                || ((filteredChannels.size()
                         ? filterChannelIndex
                         : index)
                    % displaySkipAmt
                    == 0))
            {
                mask[
                    static_cast<size_t> (
                        index)] =
                    1;
            }
            ++filterChannelIndex;
        }
    }
    return mask;
}

void LfpDisplay::
    revokeOutgoingStableChannelIdentities (
        const std::vector<uint8_t>&
            desiredDrawableMask)
{
    const auto currentMask =
        createCurrentDrawableChannelMask();
    const auto count =
        jmin (
            static_cast<int> (
                currentMask.size()),
            static_cast<int> (
                desiredDrawableMask
                    .size()),
            static_cast<int> (
                stableChannelIdentityBindingSlots
                    .size()));
    for (int index = 0;
         index < count;
         ++index)
    {
        if (currentMask[
                static_cast<size_t> (
                    index)]
            && ! desiredDrawableMask[
                static_cast<size_t> (
                    index)])
        {
            stableChannelIdentityBindingSlots[
                static_cast<size_t> (
                    index)]
                ->revoke();
        }
    }
}

#if BUILD_TESTS
void LfpDisplay::
    setStableIdentityLifecycleTestHook (
        std::function<void (
            StableIdentityLifecycleTestPhase,
            int)> hook)
{
    stableIdentityLifecycleTestHook =
        std::move (
            hook);
}

void LfpDisplay::
    resetStableIdentityAvailabilityWorkForTests()
{
    stableIdentityAvailabilityWorkForTests =
        0;
}

uint64 LfpDisplay::
    getStableIdentityAvailabilityWorkForTests() const noexcept
{
    return stableIdentityAvailabilityWorkForTests;
}

bool LfpDisplay::
    isPersistedChannelTypeValueAcceptedForTests (
        int value) noexcept
{
    return isPersistedChannelTypeValueAccepted (
        value);
}

void LfpDisplay::
    resetWaveformVisibilityResolutionWorkForTests()
{
    waveformVisibilityResolutionWorkForTests =
        0;
}

uint64 LfpDisplay::
    getWaveformVisibilityResolutionWorkForTests() const noexcept
{
    return waveformVisibilityResolutionWorkForTests;
}

void LfpDisplay::
    notifyStableIdentityLifecycleTestHook (
        StableIdentityLifecycleTestPhase phase,
        int channelIndex)
{
    if (stableIdentityLifecycleTestHook)
    {
        stableIdentityLifecycleTestHook (
            phase,
            channelIndex);
    }
}

void LfpDisplay::
    notifyStableIdentityComponentLifecycleTestHook (
        const LfpChannelDisplay&
            component,
        bool visibilityChanged)
{
    const auto channelIndex =
        component.chan;
    const bool isInfo =
        channelIndex >= 0
        && channelIndex
               < channelInfo.size()
        && static_cast<
               const LfpChannelDisplay*> (
               channelInfo[
                   channelIndex])
               == &component;
    notifyStableIdentityLifecycleTestHook (
        visibilityChanged
            ? (isInfo
                   ? StableIdentityLifecycleTestPhase::
                         infoVisibilityChangedEntry
                   : StableIdentityLifecycleTestPhase::
                         channelVisibilityChangedEntry)
            : (isInfo
                   ? StableIdentityLifecycleTestPhase::
                         infoEnablementChangedEntry
                   : StableIdentityLifecycleTestPhase::
                         channelEnablementChangedEntry),
        channelIndex);
}
#endif

void LfpDisplay::setActiveColourSchemeIdx (int index)
{
    activeColourScheme = index;
}

int LfpDisplay::getActiveColourSchemeIdx()
{
    return activeColourScheme;
}

int LfpDisplay::getNumColourSchemes()
{
    return colourSchemeList.size();
}

Array<String> LfpDisplay::getColourSchemeNameArray()
{
    Array<String> nameList;
    for (auto scheme : colourSchemeList)
        nameList.add (scheme->getName());

    return nameList;
}

int LfpDisplay::getTotalHeight()
{
    return totalHeight;
}

void LfpDisplay::restoreViewPosition()
{
    if (! getSingleChannelState())
        viewport->setViewPosition (scrollX, scrollY);
}

void LfpDisplay::resized()
{
    int totalHeight = 0;

    //LOGD(" !! LFP DISPLAY RESIZED TO: ", getWidth(), " pixels.");

    if (getWidth() > 0 && getHeight() > 0)
        lfpChannelBitmap = Image (Image::ARGB, getWidth() - canvasSplit->leftmargin, getHeight(), true, SoftwareImageType());
    else
        lfpChannelBitmap = Image (Image::ARGB, 10, 10, true, SoftwareImageType());

    if (getWidth() == 0)
    {
        //LOGD("   ::: Not visible, returning.");
        return;
    }

    int64 start = Time::getHighResolutionTicks();

    for (int i = 0; i < drawableChannels.size(); i++)
    {
        LfpChannelDisplay* disp = drawableChannels[i].channel;

        if (disp->getHidden())
            continue;

        disp->setBounds (canvasSplit->leftmargin,
                         totalHeight - (disp->getChannelOverlap() * canvasSplit->channelOverlapFactor) / 2,
                         getWidth() - canvasSplit->leftmargin,
                         disp->getChannelHeight() + (disp->getChannelOverlap() * canvasSplit->channelOverlapFactor));

        LfpChannelDisplayInfo* info = drawableChannels[i].channelInfo;

        info->setBounds (2,
                         totalHeight - disp->getChannelHeight() + (disp->getChannelOverlap() * canvasSplit->channelOverlapFactor) / 4.0,
                         canvasSplit->leftmargin - 2,
                         disp->getChannelHeight());

        totalHeight += disp->getChannelHeight();
    }

    if (! getSingleChannelState())
    {
        viewport->setViewPosition (scrollX, scrollY);
        //std::cout << "Setting view position to " << scrollY << std::endl;
    }
    else
    {
        //std::cout << "Setting view position for single channel " << std::endl;
        viewport->setViewPosition (0, 0);
    }

    canvasSplit->fullredraw = true;

    //LOGD("    RESIZED IN: ", MS_FROM_START, " milliseconds");
    start = Time::getHighResolutionTicks();

    if (displayIsPaused)
    {
        timeOffsetChanged = true;
        canRefresh = true;
    }

    refresh();

    //LOGD("    REFRESHED IN: ", MS_FROM_START, " milliseconds");
}

void LfpDisplay::paint (Graphics& g)
{
    // g.drawImageAt(lfpChannelBitmap, canvasSplit->leftmargin, 0);
    auto viewArea = viewport->getViewArea();
    g.drawImage (lfpChannelBitmap, canvasSplit->leftmargin, viewArea.getY(), getWidth() - canvasSplit->leftmargin, viewArea.getHeight(), 0, viewArea.getY(), lfpChannelBitmap.getWidth(), viewArea.getHeight());
}

void LfpDisplay::sync()
{
    if (! displayIsPaused)
    {
        lastBitmapIndex = 0;
        totalPixelsFilled = 0;
    }
}

void LfpDisplay::refresh()
{
    if (numChans == 0)
        return;

    // Ensure the lfpChannelBitmap has been initialized
    if (lfpChannelBitmap.isNull() || lfpChannelBitmap.getWidth() < getWidth() - canvasSplit->leftmargin)
    {
        resized();
    }

    int totalXPixels = lfpChannelBitmap.getWidth();
    int totalYPixels = lfpChannelBitmap.getHeight();

    int topBorder = viewport->getViewPositionY();
    int bottomBorder = viewport->getViewHeight() + topBorder;

    //std::cout << "refresh display " << std::endl;

    // X-bounds of this update
    int fillfrom = canvasSplit->lastScreenBufferIndex[0];
    int fillto = canvasSplit->screenBufferIndex[0];

    // If the display is paused, check and draw for backwards scrolling
    if (displayIsPaused)
    {
        // Check if the time offset has changed or if a full redraw is needed
        if ((timeOffsetChanged && canRefresh)
            || canvasSplit->fullredraw)
        {
            //std::cout << "Time offset: " << timeOffset << std::endl;

            int playhead = pausePoint + int (timeOffset);
            int rightEdge = totalXPixels;
            int maxScreenBufferIndex = canvasSplit->screenBufferIndex[0];

            timeOffsetChanged = false;
            canRefresh = false;

            //std::cout << "playhead: " << playhead << ", right edge: " << rightEdge << ", maxScreenBufferIndex: " << maxScreenBufferIndex << std::endl;

            lfpChannelBitmap.clear (Rectangle<int> (0, 0, totalXPixels, totalYPixels));

            for (int i = 0; i < drawableChannels.size(); i++)
            {
                int componentTop = drawableChannels[i].channel->getY();
                int componentBottom = drawableChannels[i].channel->getBottom();

                if ((topBorder <= componentBottom && bottomBorder >= componentTop)) // only draw things that are visible
                {
                    drawableChannels[i].channel->pxPaintHistory (playhead, rightEdge, maxScreenBufferIndex);
                    drawableChannels[i].channelInfo->repaint();
                }
            }

            repaint();

            canvasSplit->fullredraw = false;

            return;
        }
        else
        {
            return;
        }
    }

    //if (lastFillFrom == fillfrom && !canvasSplit->fullredraw)
    //     return;

    int totalPixelsToFill = 0;

    if (fillto > fillfrom)
    {
        totalPixelsToFill = fillto - fillfrom;
    }
    else if (fillto < fillfrom)
    {
        totalPixelsToFill = canvasSplit->screenBufferWidth - fillfrom + fillto;
    }

    //if (totalPixelsToFill > 0)
    //    std::cout << fillfrom << " : " << fillto << " ::: " << "totalPixelsToFill: " << totalPixelsToFill << std::endl;

    int fillfrom_local, fillto_local;

    if (canvasSplit->fullredraw)
    {
        int playhead = lastBitmapIndex;
        int rightEdge = totalXPixels;
        int maxScreenBufferIndex = canvasSplit->screenBufferIndex[0];

        //std::cout << "playhead: " << playhead << ", right edge: " << rightEdge << ", maxScreenBufferIndex: " << maxScreenBufferIndex << std::endl;

        lfpChannelBitmap.clear (Rectangle<int> (0, 0, totalXPixels, totalYPixels));

        for (int i = 0; i < drawableChannels.size(); i++)
        {
            int componentTop = drawableChannels[i].channel->getY();
            int componentBottom = drawableChannels[i].channel->getBottom();

            if ((topBorder <= componentBottom && bottomBorder >= componentTop)) // only draw things that are visible
            {
                drawableChannels[i].channel->pxPaintHistory (playhead, rightEdge, maxScreenBufferIndex);
                drawableChannels[i].channelInfo->repaint();
            }
        }

        canvasSplit->fullredraw = false;

        repaint();

        /* if (colourSchemeChanged)
        {
            colourSchemeChanged = false;
            lastBitmapIndex += totalPixelsToFill;
            lastBitmapIndex %= totalXPixels;
        }*/

        return;
    }
    else
    {
        fillfrom_local = lastBitmapIndex;
        fillto_local = (lastBitmapIndex + totalPixelsToFill) % totalXPixels;

        //if (fillto != 0)
        //{
        //    std::cout << fillfrom << " : " << fillto << " ::: " <<
        //        fillfrom_local << " : " << fillto_local << " :: " << totalPixelsToFill << " ::: " << totalXPixels << std::endl;
        // }

        for (int i = 0; i < numChans; i++)
        {
            channels[i]->ifrom = fillfrom; // canvasSplit->lastScreenBufferIndex[0];
            channels[i]->ito = fillto; // canvasSplit->screenBufferIndex[0];
            channels[i]->ifrom_local = fillfrom_local;
            channels[i]->ito_local = fillto_local;
        }

        if (fillfrom_local < fillto_local)
        {
            int x1 = fillfrom_local;
            int x2 = (fillto_local - fillfrom_local) + 2;
            lfpChannelBitmap.clear (Rectangle<int> (x1, 0, x2, totalYPixels));
            //std::cout << "Clearing from " << x1 << " to " << x1 + x2 << " (" << totalYPixels << "ypix)" << std::endl;
        }
        else if (fillfrom_local > fillto_local)
        {
            int x1 = fillfrom_local;
            int x2 = totalXPixels - fillfrom_local;
            int x3 = 0;
            int x4 = fillto_local + 2;
            //std::cout << "Clearing from " << x1 << " to " << x1 + x2 << " (" << totalYPixels << "ypix)" << std::endl;
            lfpChannelBitmap.clear (Rectangle<int> (x1, 0, x2, totalYPixels));
            //std::cout << "Clearing from " << x3 << " to " << x3 + x4 << " (" << totalYPixels << "ypix)" << std::endl;
            lfpChannelBitmap.clear (Rectangle<int> (x3, 0, x4, totalYPixels));
        }
        else
        {
            return; // no change, do nothing
        }
    }

    for (int i = 0; i < drawableChannels.size(); i++)
    {
        int componentTop = drawableChannels[i].channel->getY();
        int componentBottom = drawableChannels[i].channel->getBottom();

        if ((topBorder <= componentBottom && bottomBorder >= componentTop)) // only draw things that are visible
        {
            drawableChannels[i].channel->pxPaint(); // draws to lfpChannelBitmap
        }
    }

    repaint();

    totalPixelsFilled += totalPixelsToFill;

    if (totalPixelsFilled > (totalXPixels / (2 * canvasSplit->timebase)) && singleChan != -1)
    {
        channelInfo[singleChan]->updateMeanAndRMS();
        totalPixelsFilled = 0;
    }

    lastBitmapIndex += totalPixelsToFill;
    lastBitmapIndex %= lfpChannelBitmap.getWidth();

    lastFillFrom = fillfrom;
}

void LfpDisplay::setRange (float r, ContinuousChannel::Type type)
{
    range[type] = r;

    if (channels.size() > 0)
    {
        for (int i = 0; i < numChans; i++)
        {
            if (channels[i]->getType() == type)
            {
                channels[i]->setRange (range[type]);
                channelInfo[i]->setRange (range[type]);
            }
        }

        if (displayIsPaused)
        {
            timeOffsetChanged = true;
            canRefresh = true;

            refresh();
        }
    }
}

void LfpDisplay::setAutoRangeForAuxChannels()
{
    if (canvasSplit->displayBuffer == nullptr || channels.size() == 0)
        return;

    // Apply individual ranges to each AUX channel based on their InputRange
    for (int i = 0; i < numChans; i++)
    {
        if (channels[i]->getType() == ContinuousChannel::Type::AUX)
        {
            // Get the InputRange from the channel metadata
            if (i < canvasSplit->displayBuffer->channelMetadata.size())
            {
                float rangeMin = canvasSplit->displayBuffer->channelMetadata[i].inputRangeMin;
                float rangeMax = canvasSplit->displayBuffer->channelMetadata[i].inputRangeMax;
                float autoRange = rangeMax - rangeMin;

                if (autoRange > 0.0f)
                {
                    channels[i]->setRange (autoRange);
                    channelInfo[i]->setRange (autoRange);
                }
                else
                {
                    // Fall back to the default range for this type
                    channels[i]->setRange (range[ContinuousChannel::Type::AUX]);
                    channelInfo[i]->setRange (range[ContinuousChannel::Type::AUX]);
                }
            }
        }
    }

    if (displayIsPaused)
    {
        timeOffsetChanged = true;
        canRefresh = true;
        refresh();
    }
}

int LfpDisplay::getRange()
{
    return getRange (options->getSelectedType());
}

int LfpDisplay::getRange (ContinuousChannel::Type type)
{
    for (int i = 0; i < numChans; i++)
    {
        if (channels[i]->getType() == type)
            return channels[i]->getRange();
    }
    return 0;
}

void LfpDisplay::setChannelHeight (int r, bool resetSingle)
{
    if (! getSingleChannelState())
        cachedDisplayChannelHeight = r;

    for (int i = 0; i < numChans; i++)
    {
        channels[i]->setChannelHeight (r);
        channelInfo[i]->setChannelHeight (r);
    }

    if (singleChan == -1)
    {
        int overallHeight = drawableChannels.size() * getChannelHeight();

        setSize (getWidth(), overallHeight);
    }
}

void LfpDisplay::setInputInverted (bool isInverted)
{
    for (int i = 0; i < numChans; i++)
    {
        channels[i]->setInputInverted (isInverted);
    }
}

Array<bool> LfpDisplay::getInputInverted()
{
    Array<bool> invertedState;

    for (int i = 0; i < numChans; i++)
    {
        invertedState.add (channels[i]->getInputInverted());
    }

    return invertedState;
}

void LfpDisplay::setDrawMethod (bool isDrawMethod)
{
    for (int i = 0; i < numChans; i++)
    {
        channels[i]->setDrawMethod (isDrawMethod);
    }

    if (isDrawMethod)
    {
        plotter = supersampledPlotter.get();
    }
    else
    {
        plotter = perPixelPlotter.get();
    }

    resized();
}

int LfpDisplay::getChannelHeight()
{
    if (drawableChannels.size() > 0)
        return drawableChannels[0].channel->getChannelHeight();
    else
        return 0;
}

void LfpDisplay::cacheNewChannelHeight (int r)
{
    cachedDisplayChannelHeight = r;
}

bool LfpDisplay::getChannelsReversed()
{
    return channelsReversed;
}

void LfpDisplay::setChannelsReversed (bool state)
{
    channelsReversed = state;

    rebuildDrawableChannelsList();
}

void LfpDisplay::orderChannelsByDepth (bool state)
{
    channelsOrderedByDepth = state;

    rebuildDrawableChannelsList();
}

bool LfpDisplay::shouldOrderChannelsByDepth()
{
    return channelsOrderedByDepth;
}

int LfpDisplay::getChannelDisplaySkipAmount()
{
    return displaySkipAmt;
}

void LfpDisplay::setChannelDisplaySkipAmount (int skipAmt)
{
    displaySkipAmt = skipAmt;

    if (! getSingleChannelState())
        rebuildDrawableChannelsList();

    canvasSplit->redraw();
}

void LfpDisplay::setScrollPosition (int x, int y)
{
    scrollX = x;
    scrollY = y;
}

bool LfpDisplay::getMedianOffsetPlotting()
{
    return m_MedianOffsetPlottingFlag;
}

void LfpDisplay::setMedianOffsetPlotting (bool isEnabled)
{
    m_MedianOffsetPlottingFlag = isEnabled;
}

bool LfpDisplay::getSpikeRasterPlotting()
{
    return m_SpikeRasterPlottingFlag;
}

void LfpDisplay::setSpikeRasterPlotting (bool isEnabled)
{
    m_SpikeRasterPlottingFlag = isEnabled;
}

float LfpDisplay::getSpikeRasterThreshold()
{
    return m_SpikeRasterThreshold;
}

void LfpDisplay::setSpikeRasterThreshold (float thresh)
{
    m_SpikeRasterThreshold = thresh;
}

void LfpDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    //std::cout << "Mouse wheel " <<  e.mods.isCommandDown() << "  " << wheel.deltaY << std::endl;
    //TODO Changing ranges with the wheel is currently broken. With multiple ranges, most
    //of the wheel range code needs updating

    //std::cout << "Y: " << scrollY << std::endl;

    if (e.mods.isCommandDown() && singleChan == -1) // CTRL + scroll wheel -> change channel spacing
    {
        int h = getChannelHeight();
        int hdiff = 0;

        // std::cout << wheel.deltaY << std::endl;

        if (wheel.deltaY > 0)
        {
            hdiff = 2;
        }
        else
        {
            if (h > 5)
                hdiff = -2;
        }

        if (abs (h) > 100) // accelerate scrolling for large ranges
            hdiff *= 3;

        int newHeight = h + hdiff;

        // constrain the spread resizing to max and min values;
        if (newHeight < trackZoomInfo.minZoomHeight)
        {
            newHeight = trackZoomInfo.minZoomHeight;
            hdiff = 0;
        }
        else if (newHeight > trackZoomInfo.maxZoomHeight)
        {
            newHeight = trackZoomInfo.maxZoomHeight;
            hdiff = 0;
        }

        setChannelHeight (newHeight);
        int oldX = viewport->getViewPositionX();
        int oldY = viewport->getViewPositionY();

        setBounds (0, 0, getWidth() - 0, getChannelHeight() * drawableChannels.size()); // update height so that the scrollbar is correct

        int mouseY = e.getMouseDownY(); // should be y pos relative to inner viewport (0,0)
        int scrollBy = (mouseY / h) * hdiff * 2; // compensate for motion of point under current mouse position
        viewport->setViewPosition (oldX, oldY + scrollBy); // set back to previous position plus offset

        options->setSpreadSelection (newHeight); // update combobox
    }
    else
    {
        if (e.mods.isAltDown()) // ALT + scroll wheel -> change channel range (was SHIFT but that clamps wheel.deltaY to 0 on OSX for some reason..)
        {
            auto chanType = options->getSelectedType();

            if (chanType == ContinuousChannel::AUX && options->isAuxAutoScaleEnabled())
                return;

            int h = getRange();

            int step = options->getRangeStep (chanType);

            // std::cout << wheel.deltaY << std::endl;

            if (wheel.deltaY > 0)
            {
                setRange (h + step, chanType);
            }
            else
            {
                if (h > step + 1)
                    setRange (h - step, chanType);
            }

            options->setRangeSelection (h); // update combobox
        }
        else // just scroll
        {
            //  passes the event up to the viewport so the screen scrolls
            if (viewport != nullptr && e.eventComponent == this) // passes only if it's not a listening event
            {
                viewport->mouseWheelMove (e.getEventRelativeTo (viewport), wheel);
            }
        }
    }

    if (! getSingleChannelState())
    {
        scrollX = viewport->getViewPositionX();
        scrollY = viewport->getViewPositionY();
    }
}

void LfpDisplay::toggleSingleChannel (LfpChannelTrack drawableChannel)
{
    beginStableChannelIdentityBulkMutation();
    if (! getSingleChannelState())
    {
        singleChan = drawableChannel.channel->getChannelNumber();
        focusedStableChannel =
            getStableChannelVisibilityKey (
                singleChan);

        rebuildDrawableChannelsList();
    }
    else
    {
        drawableChannels[0].channelInfo->setSingleChannelState (false);

        singleChan = -1;
        focusedStableChannel.reset();

        setChannelHeight (cachedDisplayChannelHeight);

        reactivateChannels();
        rebuildDrawableChannelsList();
    }
    endStableChannelIdentityBulkMutation();
}

void LfpDisplay::reactivateChannels()
{
    for (int n = 0; n < channels.size(); n++)
        setEnabledState (
            getStoredChannelVisibility (n),
            n,
            false);
}

void LfpDisplay::rebuildDrawableChannelsList()
{
    beginStableChannelIdentityBulkMutation();
    const auto desiredDrawableMask =
        createDesiredDrawableChannelMask();
    revokeOutgoingStableChannelIdentities (
        desiredDrawableMask);

    if (getSingleChannelState())
    {
        int newHeight = viewport->getHeight();

        int channelIndex = -1;

        for (int i = 0; i < channels.size(); i++)
        {
            if (channels[i]->getChannelNumber() == singleChan)
            {
                channelIndex = i;
                break;
            }
        }

        if (channelIndex > -1)
        {
            if (drawableChannels.size() != 1 || numChans == 1) // if we haven't already gone through this ordeal
            {
                LfpChannelTrack lfpChannelTrack { channels[channelIndex], channelInfo[channelIndex] };

#if BUILD_TESTS
                notifyStableIdentityLifecycleTestHook (
                    StableIdentityLifecycleTestPhase::
                        beforeDrawableHierarchyMutation,
                    channelIndex);
#endif
                removeAllChildren();

                // disable unused channels
                for (int i = 0; i < drawableChannels.size(); i++)
                {
                    if (drawableChannels[i].channel != lfpChannelTrack.channel)
                        drawableChannels[i].channel->setEnabledState (false);
                }

                // update drawableChannels, give only the single channel to focus on
                drawableChannels = Array<LfpDisplay::LfpChannelTrack>();
                drawableChannels.add (lfpChannelTrack);

                lfpChannelTrack.channel->setEnabledState (true);
                lfpChannelTrack.channelInfo->setEnabledState (true);
                lfpChannelTrack.channelInfo->setSingleChannelState (true);

                addAndMakeVisible (lfpChannelTrack.channel);
                addAndMakeVisible (lfpChannelTrack.channelInfo);

                // set channel height and position (so that we allocate the smallest
                // necessary image size for drawing)
                setChannelHeight (newHeight, false);

                lfpChannelTrack.channel->setTopLeftPosition (canvasSplit->leftmargin, 0);
                lfpChannelTrack.channelInfo->setTopLeftPosition (0, 0);
                setSize (getWidth(), getChannelHeight());

                viewport->setViewPosition (0, 0);

                setColours();

                // this guards against an exception where the editor sets the drawable samplerate
                // before the lfpDisplay is fully initialized
                if (getHeight() > 0 && getWidth() > 0)
                {
                    canvasSplit->resizeToChannels();
                }
            }

            stableChannelIdentityRefreshPending =
                true;
            endStableChannelIdentityBulkMutation();
            return;
        }
        else
        {
            reactivateChannels();

            singleChan = -1; // our channel no longer exists
        }
    }

#if BUILD_TESTS
    notifyStableIdentityLifecycleTestHook (
        StableIdentityLifecycleTestPhase::
            beforeDrawableHierarchyMutation,
        -1);
#endif
    removeAllChildren(); // start with clean slate

    Array<LfpChannelTrack> channelsToDraw; // all visible channels will be added to this array
    Array<int> filteredChannels;
    if (canvasSplit->displayBuffer)
    {
        filteredChannels = canvasSplit->getFilteredChannels();
    }
    // iterate over all channels and select drawable ones
    for (int i = 0, drawableChannelNum = 0, filterChannelIndex = 0; i < channels.size(); i++)
    {
        //std::cout << "Checking for hidden channels" << std::endl;
        int channelNumber = filteredChannels.size() ? canvasSplit->displayBuffer->channelMetadata[i].description.getIntValue() : -1;
        //the filter list can have channels that aren't selected for acqusition; this skips those filtered channels
        while (filterChannelIndex < filteredChannels.size() && channelNumber > filteredChannels[filterChannelIndex])
        {
            filterChannelIndex++;
        }
        if (filteredChannels.size() == 0 || (filterChannelIndex < filteredChannels.size() && channelNumber == filteredChannels[filterChannelIndex]))
        {
            if (displaySkipAmt == 0 || ((filteredChannels.size() ? filterChannelIndex : i) % displaySkipAmt == 0)) // no skips, add all channels
            {
                channels[i]->setHidden (false);
                channelInfo[i]->setHidden (false);

                channelInfo[i]->setDrawableChannelNumber (drawableChannelNum++);
                channelInfo[i]->resized(); // to update the conditional drawing of enableButton and channel num

                channelsToDraw.add (LfpDisplay::LfpChannelTrack {
                    channels[i],
                    channelInfo[i] });

                addAndMakeVisible (channels[i]);
                addAndMakeVisible (channelInfo[i]);
            }
            filterChannelIndex++;
        }
        else // skip some channels
        {
            channels[i]->setHidden (true);
            channelInfo[i]->setHidden (true);
        }
    }

    if (channelsOrderedByDepth && channelsToDraw.size() > 0)
    {
        LOGD ("Sorting channels by depth.");

        const int numChannels = channelsToDraw.size();

        std::vector<float> depths (numChannels);
        std::vector<float> xposValues (numChannels);
        std::vector<bool> hasYposMetadata (numChannels);
        std::vector<bool> hasXposMetadata (numChannels);
        std::vector<int> groups (numChannels);
        std::vector<bool> hasGroupMetadata (numChannels);

        bool allSame = true;
        bool anyYposMetadata = false;
        bool anyXposMetadata = false;
        bool anyGroupMetadata = false;
        float last = channelsToDraw[0].channelInfo->getDepth();

        for (int i = 0; i < channelsToDraw.size(); i++)
        {
            auto* info = channelsToDraw[i].channelInfo;
            const float depth = info->getDepth();

            if (depth != last)
                allSame = false;

            depths[i] = depth;
            xposValues[i] = info->getXpos();
            hasYposMetadata[i] = info->hasYposMetadata();
            hasXposMetadata[i] = info->hasXposMetadata();
            groups[i] = info->getGroup();
            hasGroupMetadata[i] = info->hasGroupMetadata();

            anyYposMetadata = anyYposMetadata || hasYposMetadata[i];
            anyXposMetadata = anyXposMetadata || hasXposMetadata[i];
            anyGroupMetadata = anyGroupMetadata || hasGroupMetadata[i];
            last = depth;
        }

        const bool positionMetadataAvailable = anyYposMetadata && anyXposMetadata;
        const bool groupMetadataAvailable = anyGroupMetadata;

        if (! groupMetadataAvailable && allSame && ! anyYposMetadata)
        {
            LOGD ("No depth info found.");
        }
        else
        {
            std::vector<int> V (numChannels);

            std::iota (V.begin(), V.end(), 0); //Initializing
            sort (V.begin(), V.end(), [&] (int i, int j)
                  {
                      const float depthDiff = depths[i] - depths[j];
                      const float depthEpsilon = 1.0e-3f;

                      if (groupMetadataAvailable && groups[i] != groups[j])
                          return groups[i] < groups[j];

                      if (std::abs (depthDiff) >= depthEpsilon)
                          return depths[i] < depths[j];

                      if (positionMetadataAvailable)
                          return xposValues[i] < xposValues[j];

                      return i < j; // deterministic fallback
                  });

            Array<LfpChannelTrack> orderedDrawableChannels;

            for (int i = 0; i < channelsToDraw.size(); i++)
            {
                // re-order by depth
                orderedDrawableChannels.add (channelsToDraw[V[i]]);
            }

            channelsToDraw = orderedDrawableChannels;
        }
    }

    drawableChannels = Array<LfpDisplay::LfpChannelTrack>(); // this is what will determine the actual channel order

    if (getChannelsReversed())
    {
        LOGD ("Reversing channel order.");
        for (int i = channelsToDraw.size() - 1; i >= 0; --i)
        {
            drawableChannels.add (channelsToDraw[i]);
        }
    }
    else
    {
        for (int i = 0; i < channelsToDraw.size(); ++i)
        {
            drawableChannels.add (channelsToDraw[i]);
        }
    }

    // this guards against an exception where the editor sets the drawable samplerate
    // before the lfpDisplay is fully initialized
    if (getHeight() > 0 && getWidth() > 0)
    {
        canvasSplit->resizeToChannels();
    }

    setColours();

    resized();
    stableChannelIdentityRefreshPending =
        true;
    endStableChannelIdentityBulkMutation();

    //LOGD("Finished standard channel rebuild.");
}

LfpBitmapPlotter* const LfpDisplay::getPlotterPtr() const
{
    return plotter;
}

bool LfpDisplay::getSingleChannelState()
{
    return singleChan >= 0;
}

int LfpDisplay::getSingleChannelShown()
{
    return singleChan;
}

void LfpDisplay::setSingleChannelView (int chan)
{
    singleChan = chan;
    focusedStableChannel =
        getStableChannelVisibilityKey (
            chan);
}

void LfpDisplay::pause (bool shouldPause)
{
    displayIsPaused = shouldPause;

    options->setPausedState (shouldPause);

    canvasSplit->pause (shouldPause);

    if (! shouldPause)
    {
        timeOffset = 0.0f;
        sync();
        canvasSplit->fullredraw = true;
        //refresh();
    }
    else
    {
        pausePoint = lastBitmapIndex;
    }
}

void LfpDisplay::timerCallback()
{
    canRefresh = true;
}

void LfpDisplay::setTimeOffset (float offset)
{
    timeOffset = offset;
    timeOffsetChanged = true;
    canRefresh = true;

    refresh();

    //if (offset != timeOffset)
    //{

    // }

    //LOGD("Time offset: ", offset);
}

bool LfpDisplay::isPaused()
{
    return displayIsPaused;
}

void LfpDisplay::mouseDown (const MouseEvent& event)
{
    if (drawableChannels.isEmpty())
    {
        return;
    }

    //int y = event.getMouseDownY(); //relative to each channel pos
    MouseEvent canvasevent = event.getEventRelativeTo (viewport);
    int y = canvasevent.getMouseDownY() + viewport->getViewPositionY(); // need to account for scrolling
    int x = canvasevent.getMouseDownX();

    int dist = 0;
    int mindist = 10000;
    int closest = 5;
    float chanRange = getRange();

    for (int n = 0; n < drawableChannels.size(); n++) // select closest instead of relying on eventComponent
    {
        drawableChannels[n].channel->deselect();

        int cpos = (drawableChannels[n].channel->getY() + (drawableChannels[n].channel->getHeight() / 2));
        dist = int (abs (y - cpos));

        //std::cout << "Mouse down at " << y << " pos is "<< cpos << " n: " << n << "  dist " << dist << std::endl;

        if (dist < mindist)
        {
            mindist = dist - 1;
            closest = n;
            chanRange = drawableChannels[n].channel->getRange();
        }
    }

    //std::cout << "Closest channel" << closest << std::endl;

    drawableChannels[closest].channel->select();
    options->setSelectedType (drawableChannels[closest].channel->getType());

    if (event.mods.isRightButtonDown())
    { // if right click
        PopupMenu channelMenu = drawableChannels[closest].channel->getOptions();
        const int result = channelMenu.show();
        drawableChannels[closest].channel->changeParameter (result);
    }
    else // if left click
    {
        if (event.getNumberOfClicks() == 2)
        {
            toggleSingleChannel (drawableChannels[closest]);
        }

        if (getSingleChannelState()) // show info for point that was selected
        {
            drawableChannels[0].channelInfo->updateXY (
                float (x) / getWidth() * canvasSplit->timebase,
                (-(float (y) - viewport->getViewPositionY()) / viewport->getViewHeight() * float (chanRange)) + float (chanRange / 2));
        }
    }

    canvasSplit->select();

    canvasSplit->fullredraw = true; //issue full redraw

    refresh();
}

bool LfpDisplay::setEventDisplayState (int ttlLine, bool state)
{
    eventDisplayEnabled[ttlLine] = state;
    return eventDisplayEnabled[ttlLine];
}

bool LfpDisplay::getEventDisplayState (int ttlLine)
{
    return eventDisplayEnabled[ttlLine];
}

void LfpDisplay::setEnabledState (bool state, int chan, bool updateSaved)
{
    if (chan >= 0
        && chan < numChans)
    {
        if (updateSaved)
        {
            const auto key =
                getStableChannelVisibilityKey (
                    chan);
            if (! key.has_value())
            {
                state = true;
            }
            else if (state)
            {
                hiddenStableChannels.erase (
                    *key);
            }
            else
            {
                hiddenStableChannels.insert (
                    *key);
            }
        }

        channels[chan]->setEnabledState (state);
        channelInfo[chan]->setEnabledState (state);
    }
}

bool LfpDisplay::getEnabledState (int chan)
{
    if (chan >= 0
        && chan < numChans)
    {
        return channels[chan]->getEnabledState();
    }

    return false;
}
