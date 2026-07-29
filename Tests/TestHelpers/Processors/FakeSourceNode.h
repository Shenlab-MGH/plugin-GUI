#ifndef FAKESOURCENODE_H
#define FAKESOURCENODE_H

#include <NonAPIHeaders.h>
#include <ProcessorHeaders.h>

#include <map>

/** Collection of optional, settable parameters for configuring the FakeSourceNode for testing. */
struct FakeSourceNodeParams
{
    int channels = 1;
    float sampleRate = 20000.0f;
    float bitVolts = 1.0f;
    int streams = 1;
    uint32_t metadataSizeBytes = 0;
};

class TESTABLE FakeSourceNode : public GenericProcessor
{
public:
    explicit FakeSourceNode (const FakeSourceNodeParams& params);
    ~FakeSourceNode() override;

    void updateSettings() override;
    void process (AudioBuffer<float>& continuousBuffer) override;
    void setParams (const FakeSourceNodeParams& params);
    void setStreamCountPreservingExisting (
        int streams,
        int channels);
    void moveStream (
        int currentIndex,
        int newIndex);
    void setStreamName (
        int streamIndex,
        const String& name);
    void setStreamSourceNodeId (
        int streamIndex,
        int sourceNodeId);

private:
    void addCachedDataStream();
    void updateCachedContinuousChannels();

    FakeSourceNodeParams params;
    OwnedArray<DataStream> cachedDataStreams;
    OwnedArray<ContinuousChannel> cachedContinuousChannels;
    std::map<uint16, int> cachedStreamIdentityIndices;
    int channelIdentityGeneration = 0;
};

#endif
