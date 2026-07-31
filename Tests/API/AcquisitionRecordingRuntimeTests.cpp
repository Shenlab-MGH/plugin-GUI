#include "../../Source/Utils/AcquisitionRecordingRuntime.h"

#include "../../Source/AccessClass.h"
#include "../../Source/Audio/AudioComponent.h"
#include "../../Source/CoreServices.h"
#include "../../Source/Processors/Parameter/Parameter.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/Processors/RecordNode/RecordNode.h"
#include "../../Source/UI/ControlPanel.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
using Mode = AcquisitionRecordingMode;
using Snapshot = AcquisitionRecordingControlSnapshot;

struct InspectableAudio
{
    bool callbacks = false;
    bool deviceAvailable = false;
    int sampleRate = 0;
    int callbackReads = 0;
    int deviceReads = 0;
    int sampleRateReads = 0;

    bool callbacksAreActive()
    {
        ++callbackReads;
        return callbacks;
    }

    bool checkForDevice()
    {
        ++deviceReads;
        return deviceAvailable;
    }

    int getSampleRate()
    {
        ++sampleRateReads;
        return sampleRate;
    }
};

struct InspectableReadiness
{
    bool ready = false;
};

struct InspectableRecordNode
{
    std::uint64_t generation = 0;
    bool recording = false;
    bool writerRunning = false;
    bool pathValid = false;
    bool synchronized = false;
    int generationReads = 0;
    int recordingReads = 0;
    int writerReads = 0;
    int pathReads = 0;
    int synchronizedReads = 0;

    std::uint64_t getRuntimeGeneration()
    {
        ++generationReads;
        return generation;
    }

    bool getRecordingStatus()
    {
        ++recordingReads;
        return recording;
    }

    bool isWriterThreadRunning()
    {
        ++writerReads;
        return writerRunning;
    }

    bool isRecordingPathValid()
    {
        ++pathReads;
        return pathValid;
    }

    bool isSynchronized()
    {
        ++synchronizedReads;
        return synchronized;
    }
};

struct InspectableGraph
{
    std::vector<InspectableRecordNode*> nodes;
    bool ready = false;
    int nodeEnumerationCount = 0;
    int readinessReads = 0;

    std::vector<InspectableRecordNode*> getRecordNodes()
    {
        ++nodeEnumerationCount;
        return nodes;
    }

    InspectableReadiness inspectAcquisitionReadiness()
    {
        ++readinessReads;
        return { ready };
    }
};

struct ScopedGuiRuntimeCleanup
{
    ~ScopedGuiRuntimeCleanup()
    {
        AccessClass::clearAccessClassStateForTesting();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }
};

struct ScopedOwnedTestDirectory
{
    ScopedOwnedTestDirectory()
        : tempParent (
              File::getSpecialLocation (
                  File::tempDirectory)),
          root (
              tempParent.getNonexistentChildFile (
                  "open-ephys-p04d1-runtime-tests",
                  String(),
                  true))
    {
        EXPECT_TRUE (root.isAChildOf (tempParent));
        EXPECT_FALSE (root.exists());
        EXPECT_TRUE (root.createDirectory().wasOk());
    }

    ~ScopedOwnedTestDirectory()
    {
        if (root.isAChildOf (tempParent)
            && root.exists())
            root.deleteRecursively();
    }

    const File tempParent;
    const File root;
};

Snapshot captureFake (bool callbacks,
                      std::vector<InspectableRecordNode*> nodes)
{
    InspectableAudio audio {
        callbacks, true, 30000
    };
    InspectableGraph graph {
        std::move (nodes), true
    };
    return AcquisitionRecordingRuntimeDetail::
        captureFromOwners (audio, graph);
}

void expectAggregate (const Snapshot& snapshot,
                      std::optional<Mode> mode,
                      bool acquisitionActive,
                      bool recordingActive,
                      bool consistent)
{
    EXPECT_TRUE (
        isValidAcquisitionRecordingControlSnapshot (
            snapshot));
    EXPECT_EQ (snapshot.status.mode, mode);
    EXPECT_EQ (snapshot.status.acquisitionActive,
               acquisitionActive);
    EXPECT_EQ (snapshot.status.recordingActive,
               recordingActive);
    EXPECT_EQ (snapshot.status.recordingConsistent,
               consistent);
}
} // namespace

TEST (AcquisitionRecordingRuntimeTests,
      DerivesIdleAcquireRecordAndInconsistentFromCopiedNodeFacts)
{
    InspectableRecordNode inactive {
        1, false, false, true, true
    };
    InspectableRecordNode active {
        2, true, true, true, true
    };

    expectAggregate (
        captureFake (false, { &inactive }),
        Mode::idle, false, false, true);
    expectAggregate (
        captureFake (true, { &inactive }),
        Mode::acquire, true, false, true);
    expectAggregate (
        captureFake (true, { &active }),
        Mode::record, true, true, true);
    expectAggregate (
        captureFake (true, { &active, &inactive }),
        std::nullopt, true, false, false);
}

TEST (AcquisitionRecordingRuntimeTests,
      CopiesEveryOwnerFactExactlyOnceWithoutRetainingOwners)
{
    InspectableAudio audio {
        true, true, 25000
    };
    InspectableRecordNode first {
        17, true, true, true, false
    };
    InspectableRecordNode second {
        29, false, false, false, true
    };
    InspectableGraph graph {
        { &first, &second }, false
    };

    const auto copied =
        AcquisitionRecordingRuntimeDetail::
            captureFromOwners (audio, graph);

    ASSERT_EQ (copied.recordNodes.size(), 2);
    EXPECT_EQ (copied.recordNodes[0].generation, 17);
    EXPECT_TRUE (copied.recordNodes[0].recordingActive);
    EXPECT_TRUE (copied.recordNodes[0].writerThreadRunning);
    EXPECT_TRUE (copied.recordNodes[0].recordingPathValid);
    EXPECT_FALSE (copied.recordNodes[0].synchronized);
    EXPECT_EQ (copied.recordNodes[1].generation, 29);
    EXPECT_FALSE (copied.recordNodes[1].recordingActive);
    EXPECT_FALSE (copied.recordNodes[1].writerThreadRunning);
    EXPECT_FALSE (copied.recordNodes[1].recordingPathValid);
    EXPECT_TRUE (copied.recordNodes[1].synchronized);
    EXPECT_TRUE (copied.audioDeviceAvailable);
    EXPECT_DOUBLE_EQ (copied.audioSampleRate, 25000.0);
    EXPECT_FALSE (copied.processorGraphReady);
    EXPECT_EQ (audio.callbackReads, 1);
    EXPECT_EQ (audio.deviceReads, 1);
    EXPECT_EQ (audio.sampleRateReads, 1);
    EXPECT_EQ (graph.nodeEnumerationCount, 1);
    EXPECT_EQ (graph.readinessReads, 1);
    for (const auto* node : { &first, &second })
    {
        EXPECT_EQ (node->generationReads, 1);
        EXPECT_EQ (node->recordingReads, 1);
        EXPECT_EQ (node->writerReads, 1);
        EXPECT_EQ (node->pathReads, 1);
        EXPECT_EQ (node->synchronizedReads, 1);
    }
}

TEST (AcquisitionRecordingRuntimeTests,
      EmptyGraphStillCopiesAudioAndReadinessFacts)
{
    InspectableAudio audio {
        false, false, 44100
    };
    InspectableGraph graph {
        {}, false
    };

    const auto copied =
        AcquisitionRecordingRuntimeDetail::
            captureFromOwners (audio, graph);

    expectAggregate (
        copied, Mode::idle, false, false, true);
    EXPECT_TRUE (copied.recordNodes.empty());
    EXPECT_FALSE (copied.audioDeviceAvailable);
    EXPECT_DOUBLE_EQ (copied.audioSampleRate, 44100.0);
    EXPECT_FALSE (copied.processorGraphReady);
    EXPECT_EQ (audio.callbackReads, 1);
    EXPECT_EQ (audio.deviceReads, 1);
    EXPECT_EQ (audio.sampleRateReads, 1);
    EXPECT_EQ (graph.nodeEnumerationCount, 1);
    EXPECT_EQ (graph.readinessReads, 1);
}

TEST (AcquisitionRecordingRuntimeTests,
      ConcreteOwnersCaptureAnEmptyGraphOnTheMessageThread)
{
    MessageManager::getInstance();
    const ScopedGuiRuntimeCleanup cleanup;
    ASSERT_TRUE (
        MessageManager::getInstance()
            ->isThisTheMessageThread());
    AccessClass::clearAccessClassStateForTesting();

    auto audio = std::make_unique<AudioComponent>();
    auto graph =
        std::make_unique<ProcessorGraph> (true);

    const auto copied =
        captureAcquisitionRecordingControlSnapshot (
            *audio, *graph);

    EXPECT_TRUE (copied.recordNodes.empty());
    EXPECT_EQ (copied.status.recordNodeCount, 0);
    EXPECT_EQ (copied.status.acquisitionActive,
               audio->callbacksAreActive());
    EXPECT_EQ (copied.audioDeviceAvailable,
               audio->checkForDevice());
    EXPECT_DOUBLE_EQ (copied.audioSampleRate,
                      audio->getSampleRate());
    EXPECT_EQ (copied.processorGraphReady,
               graph->inspectAcquisitionReadiness().ready);
}

TEST (AcquisitionRecordingRuntimeTests,
      RecordNodeGenerationAndPathSeamsAreStableAndReadOnly)
{
    MessageManager::getInstance();
    const ScopedGuiRuntimeCleanup cleanup;
    ASSERT_TRUE (
        MessageManager::getInstance()
            ->isThisTheMessageThread());
    AccessClass::clearAccessClassStateForTesting();

    auto audio = std::make_unique<AudioComponent>();
    auto graph =
        std::make_unique<ProcessorGraph> (true);
    auto controlPanel =
        std::make_unique<ControlPanel> (
            graph.get(), audio.get(), true);
    controlPanel->updateRecordEngineList();

    const ScopedOwnedTestDirectory testDirectory;
    const auto& testRoot = testDirectory.root;
    const auto defaultDirectory =
        testRoot.getChildFile ("default");
    const auto explicitDirectory =
        testRoot.getChildFile ("explicit");
    ASSERT_TRUE (
        defaultDirectory.createDirectory().wasOk());
    ASSERT_TRUE (
        explicitDirectory.createDirectory().wasOk());
    controlPanel->setRecordingParentDirectory (
        defaultDirectory.getFullPathName());

    auto first = std::make_unique<RecordNode>();
    first->registerParameters();
    auto second = std::make_unique<RecordNode>();
    second->registerParameters();
    const auto firstGeneration =
        first->getRuntimeGeneration();
    const auto secondGeneration =
        second->getRuntimeGeneration();
    EXPECT_NE (firstGeneration, 0);
    EXPECT_NE (secondGeneration, 0);
    EXPECT_NE (firstGeneration, secondGeneration);
    EXPECT_EQ (first->getRuntimeGeneration(),
               firstGeneration);

    first.reset();
    auto replacement =
        std::make_unique<RecordNode>();
    replacement->registerParameters();
    const auto replacementGeneration =
        replacement->getRuntimeGeneration();
    EXPECT_NE (replacementGeneration, 0);
    EXPECT_NE (replacementGeneration,
               firstGeneration);
    EXPECT_NE (replacementGeneration,
               secondGeneration);
    EXPECT_FALSE (
        replacement->isWriterThreadRunning());

    replacement->setNodeId (201);
    replacement->setProcessorType (
        Plugin::Processor::RECORD_NODE);
    replacement->setHeadlessMode (true);
    AudioProcessorGraph::Node::Ptr addedNode =
        graph->addNode (
        std::unique_ptr<AudioProcessor> (
            replacement.release()),
        AudioProcessorGraph::NodeID (201));
    ASSERT_NE (addedNode.get(), nullptr);
    auto* runtimeNode =
        static_cast<RecordNode*> (
            addedNode->getProcessor());

    ASSERT_TRUE (
        defaultDirectory.isAChildOf (testRoot));
    ASSERT_TRUE (
        defaultDirectory.deleteRecursively());
    EXPECT_FALSE (
        runtimeNode->isRecordingPathValid());
    EXPECT_FALSE (
        graph->allRecordNodeDirectoriesAreValid());

    auto* path = runtimeNode->getParameter (
        "directory");
    ASSERT_NE (path, nullptr);
    path->setNextValue (
        explicitDirectory.getFullPathName(),
        false);
    EXPECT_TRUE (
        runtimeNode->isRecordingPathValid());
    EXPECT_TRUE (
        graph->allRecordNodeDirectoriesAreValid());

    XmlElement invalidPath ("PARAMETERS");
    invalidPath.setAttribute (
        "directory",
        testRoot.getChildFile ("missing")
            .getFullPathName());
    path->fromXml (&invalidPath);
    EXPECT_FALSE (
        runtimeNode->isRecordingPathValid());
    EXPECT_FALSE (
        graph->allRecordNodeDirectoriesAreValid());

    second.reset();
    controlPanel.reset();
    graph.reset();
    addedNode = nullptr;
    audio.reset();
}

TEST (AcquisitionRecordingRuntimeTests,
      RuntimeGenerationAllocatorPermanentlyExhaustsUnderConcurrency)
{
    constexpr std::uint64_t claimCount = 64;
    constexpr auto maximumGeneration =
        std::numeric_limits<std::uint64_t>::max();
    constexpr auto firstGeneration =
        maximumGeneration - claimCount + 1;
    std::atomic<std::uint64_t> nextGeneration {
        firstGeneration
    };
    std::mutex claimedMutex;
    std::vector<std::uint64_t> claimed;

    const auto claimUntilExhausted = [&]
    {
        for (;;)
        {
            std::uint64_t generation = 0;
            if (! RecordNodeRuntimeGenerationDetail::
                      tryClaim (
                          nextGeneration,
                          generation))
                return;

            const std::lock_guard<std::mutex>
                lock (claimedMutex);
            claimed.push_back (generation);
        }
    };

    std::vector<std::thread> claimers;
    for (int index = 0; index < 8; ++index)
        claimers.emplace_back (
            claimUntilExhausted);
    for (auto& claimer : claimers)
        claimer.join();

    std::sort (claimed.begin(), claimed.end());
    ASSERT_EQ (claimed.size(), claimCount);
    for (std::uint64_t index = 0;
         index < claimCount;
         ++index)
        EXPECT_EQ (claimed[index],
                   firstGeneration + index);
    EXPECT_EQ (nextGeneration.load(), 0);

    std::uint64_t unchanged = 17;
    EXPECT_FALSE (
        RecordNodeRuntimeGenerationDetail::
            tryClaim (
                nextGeneration,
                unchanged));
    EXPECT_EQ (unchanged, 17);
    EXPECT_EQ (nextGeneration.load(), 0);
}
