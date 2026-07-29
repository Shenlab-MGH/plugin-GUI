#include "FakeSourceNode.h"

#include <algorithm>

namespace
{
class FakeDataStream final : public DataStream
{
public:
    using DataStream::DataStream;

    void setFakeSourceNodeId (
        int sourceNodeId)
    {
        setSourceNodeId (
            sourceNodeId);
    }
};

class FakeContinuousChannel final : public ContinuousChannel
{
public:
    using ContinuousChannel::ContinuousChannel;

    void setFakeSourceNodeId (
        int sourceNodeId)
    {
        setSourceNodeId (
            sourceNodeId);
    }
};
}

FakeSourceNode::FakeSourceNode (const FakeSourceNodeParams& params)
    : GenericProcessor ("FakeSourceNode", true),
      params (params)
{
    setProcessorType (Plugin::Processor::Type::SOURCE);
}

FakeSourceNode::~FakeSourceNode() = default;

void FakeSourceNode::updateSettings()
{
    // Keep stream and channel identity stable across ordinary settings updates.
    if (cachedDataStreams.size() == 0)
    {
        for (int i = 0; i < params.streams; i++)
        {
            addCachedDataStream();
        }
    }

    updateCachedContinuousChannels();

    continuousChannels.clear();
    dataStreams.clear();
    for (const auto& item : cachedDataStreams)
    {
        // Copy it over
        auto* stream = new DataStream (*item);
        stream->clearContinuousChannels();
        dataStreams.add (stream);
    }
    for (int streamIndex = 0;
         streamIndex < cachedDataStreams.size();
         ++streamIndex)
    {
        const auto streamId =
            cachedDataStreams[
                streamIndex]
                ->getStreamId();
        for (const auto* cachedChannel :
             cachedContinuousChannels)
        {
            if (cachedChannel->getStreamId()
                == streamId)
            {
                continuousChannels.add (
                    new ContinuousChannel (
                        *cachedChannel));
                continuousChannels
                    .getLast()
                    ->setDataStream (
                        dataStreams[
                            streamIndex]);
            }
        }
    }

    EventChannel::Settings settings {
        EventChannel::Type::TTL,
        "TTL",
        "TTL",
        "identifier.ttl.events",
        dataStreams.getFirst(),
    };
    eventChannels.add (new EventChannel (settings));
    eventChannels.getFirst()->setIdentifier ("sourceevent");
    eventChannels.getFirst()->addProcessor (this);

    if (params.metadataSizeBytes > 0)
    {
        eventChannels.getLast()->addEventMetadata (new MetadataDescriptor (
            MetadataDescriptor::MetadataType::UINT8,
            params.metadataSizeBytes,
            "FakeSourceNodeMetadata",
            "FakeSourceNodeMetadata",
            "identifier"));
    }
}

void FakeSourceNode::setParams (const FakeSourceNodeParams& params)
{
    this->params = params;
    cachedContinuousChannels.clear();
    cachedDataStreams.clear();
    cachedStreamIdentityIndices.clear();
    ++channelIdentityGeneration;
}

void FakeSourceNode::
    setStreamCountPreservingExisting (
        int streams,
        int channels)
{
    jassert (streams > 0);
    jassert (
        cachedDataStreams.size()
        > 0);

    while (cachedDataStreams.size()
           < streams)
    {
        addCachedDataStream();
    }

    while (cachedDataStreams.size()
           > streams)
    {
        const auto streamId =
            cachedDataStreams
                .getLast()
                ->getStreamId();
        for (int channelIndex =
                 cachedContinuousChannels.size()
                 - 1;
             channelIndex >= 0;
             --channelIndex)
        {
            if (cachedContinuousChannels[
                    channelIndex]
                    ->getStreamId()
                == streamId)
            {
                cachedContinuousChannels
                    .remove (channelIndex);
            }
        }
        cachedStreamIdentityIndices.erase (
            streamId);
        cachedDataStreams.removeLast();
    }

    params.streams = streams;
    params.channels = channels;
}

void FakeSourceNode::moveStream (
    int currentIndex,
    int newIndex)
{
    cachedDataStreams.move (
        currentIndex,
        newIndex);
}

void FakeSourceNode::setStreamName (
    int streamIndex,
    const String& name)
{
    cachedDataStreams[streamIndex]
        ->setName (name);
}

void FakeSourceNode::setStreamSourceNodeId (
    int streamIndex,
    int sourceNodeId)
{
    auto* stream =
        dynamic_cast<
            FakeDataStream*> (
            cachedDataStreams[
                streamIndex]);
    jassert (stream != nullptr);
    if (stream != nullptr)
    {
        stream->setFakeSourceNodeId (
            sourceNodeId);

        for (auto* channel :
             cachedContinuousChannels)
        {
            if (channel->getStreamId()
                == stream->getStreamId())
            {
                auto* fakeChannel =
                    dynamic_cast<
                        FakeContinuousChannel*> (
                        channel);
                jassert (fakeChannel != nullptr);
                if (fakeChannel != nullptr)
                {
                    fakeChannel
                        ->setFakeSourceNodeId (
                            sourceNodeId);
                }
            }
        }
    }
}

void FakeSourceNode::addCachedDataStream()
{
    const auto streamIdentityIndex =
        cachedDataStreams.size();
    DataStream::Settings settings {
        "FakeSourceNode"
            + String (
                streamIdentityIndex),
        "description",
        "identifier",
        params.sampleRate
    };
    auto* stream = new FakeDataStream (settings);
    cachedStreamIdentityIndices[
        stream->getStreamId()] =
        streamIdentityIndex;
    cachedDataStreams.add (stream);
}

void FakeSourceNode::updateCachedContinuousChannels()
{
    for (auto* stream :
         cachedDataStreams)
    {
        stream->clearContinuousChannels();
    }

    for (int channelIndex =
             cachedContinuousChannels.size()
             - 1;
         channelIndex >= 0;
         --channelIndex)
    {
        const auto* channel =
            cachedContinuousChannels[
                channelIndex];
        const auto matchingStream =
            std::find_if (
                cachedDataStreams.begin(),
                cachedDataStreams.end(),
                [&] (const DataStream* stream)
                {
                    return stream->getStreamId()
                           == channel->getStreamId();
                });
        if (matchingStream
                == cachedDataStreams.end()
            || channel->getLocalIndex()
                   >= params.channels)
        {
            cachedContinuousChannels
                .remove (channelIndex);
        }
    }

    for (auto* stream :
         cachedDataStreams)
    {
        Array<ContinuousChannel*> streamChannels;
        for (auto* channel :
             cachedContinuousChannels)
        {
            if (channel->getStreamId()
                == stream->getStreamId())
            {
                streamChannels.add (channel);
            }
        }
        std::sort (
            streamChannels.begin(),
            streamChannels.end(),
            [] (const ContinuousChannel* lhs,
                const ContinuousChannel* rhs)
            {
                return lhs->getLocalIndex()
                       < rhs->getLocalIndex();
            });
        for (auto* channel :
             streamChannels)
        {
            stream->addChannel (channel);
        }

        const auto streamIdentityIndex =
            cachedStreamIdentityIndices[
                stream->getStreamId()];
        for (int channelIndex =
                 streamChannels.size();
             channelIndex < params.channels;
             ++channelIndex)
        {
            ContinuousChannel::Settings settings {
                ContinuousChannel::Type::ELECTRODE,
                "CH" + String (channelIndex),
                String (channelIndex),
                "identifier.g"
                    + String (
                        channelIdentityGeneration)
                    + ".s"
                    + String (
                        streamIdentityIndex)
                    + ".c"
                    + String (channelIndex),
                params.bitVolts,
                stream
            };
            auto* channel =
                new FakeContinuousChannel (
                    settings);
            channel->setFakeSourceNodeId (
                stream->getSourceNodeId());
            cachedContinuousChannels.add (
                channel);
        }
    }
}

void FakeSourceNode::process (AudioBuffer<float>& continuousBuffer) {}
