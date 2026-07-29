#include "FakeSourceNode.h"

FakeSourceNode::FakeSourceNode (const FakeSourceNodeParams& params)
    : GenericProcessor ("FakeSourceNode", true),
      params (params)
{
    setProcessorType (Plugin::Processor::Type::SOURCE);
}

FakeSourceNode::~FakeSourceNode() = default;

void FakeSourceNode::updateSettings()
{
    // Don't recreate datastreams, or the IDs / pointer locations keep changing.
    if (cachedDataStreams.size() == 0)
    {
        for (int i = 0; i < params.streams; i++)
        {
            DataStream::Settings settings {
                "FakeSourceNode" + String (i),
                "description",
                "identifier",
                params.sampleRate
            };

            cachedDataStreams.add (new DataStream (settings));
        }
    }

    continuousChannels.clear();
    dataStreams.clear();
    for (const auto& item : cachedDataStreams)
    {
        // Copy it over
        dataStreams.add (new DataStream (*item));
    }
    for (int i = 0; i < params.streams; i++)
    {
        for (int index = 0; index < params.channels; index++)
        {
            ContinuousChannel::Settings settings {
                ContinuousChannel::Type::ELECTRODE,
                "CH" + String (index),
                String (index),
                "identifier",
                params.bitVolts,
                dataStreams[i]

            };

            continuousChannels.add (new ContinuousChannel (settings));
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
    cachedDataStreams.clear();
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
        const auto streamIndex =
            cachedDataStreams.size();
        DataStream::Settings settings {
            "FakeSourceNode"
                + String (
                    streamIndex),
            "description",
            "identifier",
            params.sampleRate
        };
        cachedDataStreams.add (
            new DataStream (
                settings));
    }

    while (cachedDataStreams.size()
           > streams)
    {
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

void FakeSourceNode::process (AudioBuffer<float>& continuousBuffer) {}
