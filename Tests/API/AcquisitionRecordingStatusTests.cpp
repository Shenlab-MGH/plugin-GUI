#include "../../Source/Utils/AcquisitionRecordingStatus.h"

#include "../../Source/AccessClass.h"
#include "../../Source/Audio/AudioComponent.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/Processors/RecordNode/RecordNode.h"
#include "../../Source/UI/ControlPanel.h"

#include "gtest/gtest.h"

#include <memory>
#include <vector>

namespace
{
using Mode = AcquisitionRecordingMode;
using Node = RecordNodeRuntimeState;
using Status = AcquisitionRecordingStatus;

struct InspectableWriter
{
    bool running = false;
    int readCount = 0;

    bool isThreadRunning()
    {
        ++readCount;
        return running;
    }
};

struct InspectableRecordNode
{
    bool recording = false;
    mutable int readCount = 0;
    InspectableWriter* recordThread = nullptr;

    bool getRecordingStatus() const
    {
        ++readCount;
        return recording;
    }
};

struct InspectableGraph
{
    std::vector<InspectableRecordNode*> nodes;
    int readCount = 0;

    std::vector<InspectableRecordNode*> getRecordNodes()
    {
        ++readCount;
        return nodes;
    }
};

struct InspectableAudio
{
    bool callbacksActive = false;
    int readCount = 0;

    bool callbacksAreActive()
    {
        ++readCount;
        return callbacksActive;
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

void expectStatus (const Status& status,
                   std::optional<Mode> mode,
                   bool acquisitionActive,
                   bool recordingActive,
                   std::size_t recordNodeCount,
                   std::size_t activeRecordNodeCount,
                   std::size_t writerThreadRunningCount,
                   bool recordingConsistent)
{
    EXPECT_EQ (status.mode, mode);
    EXPECT_EQ (status.acquisitionActive, acquisitionActive);
    EXPECT_EQ (status.recordingActive, recordingActive);
    EXPECT_EQ (status.recordNodeCount, recordNodeCount);
    EXPECT_EQ (status.activeRecordNodeCount, activeRecordNodeCount);
    EXPECT_EQ (status.writerThreadRunningCount,
               writerThreadRunningCount);
    EXPECT_EQ (status.recordingConsistent, recordingConsistent);
}
} // namespace

TEST (AcquisitionRecordingStatusTests,
      DerivesIdleAndAcquireWithNoRecordNodes)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (false, {}),
        Mode::idle,
        false,
        false,
        0,
        0,
        0,
        true);

    expectStatus (
        deriveAcquisitionRecordingStatus (true, {}),
        Mode::acquire,
        true,
        false,
        0,
        0,
        0,
        true);
}

TEST (AcquisitionRecordingStatusTests,
      DerivesIdleAndAcquireWhenEveryRecordNodeAndWriterIsInactive)
{
    const std::vector<Node> inactiveNodes {
        { false, false },
        { false, false },
        { false, false }
    };

    expectStatus (
        deriveAcquisitionRecordingStatus (false, inactiveNodes),
        Mode::idle,
        false,
        false,
        3,
        0,
        0,
        true);

    expectStatus (
        deriveAcquisitionRecordingStatus (true, inactiveNodes),
        Mode::acquire,
        true,
        false,
        3,
        0,
        0,
        true);
}

TEST (AcquisitionRecordingStatusTests,
      RejectsLingeringWriterWithCallbacksActive)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (
            true,
            { { false, true } }),
        std::nullopt,
        true,
        false,
        1,
        0,
        1,
        false);
}

TEST (AcquisitionRecordingStatusTests,
      RejectsLingeringWriterWithCallbacksInactive)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (
            false,
            { { false, true } }),
        std::nullopt,
        false,
        false,
        1,
        0,
        1,
        false);
}

TEST (AcquisitionRecordingStatusTests,
      RejectsAnyLingeringWriterAcrossInactiveNodes)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (
            true,
            {
                { false, false },
                { false, true },
                { false, false }
            }),
        std::nullopt,
        true,
        false,
        3,
        0,
        1,
        false);
}

TEST (AcquisitionRecordingStatusTests,
      DerivesRecordOnlyWhenEveryNodeAndWriterAreRunning)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (
            true,
            { { true, true } }),
        Mode::record,
        true,
        true,
        1,
        1,
        1,
        true);

    expectStatus (
        deriveAcquisitionRecordingStatus (
            true,
            { { true, true }, { true, true }, { true, true } }),
        Mode::record,
        true,
        true,
        3,
        3,
        3,
        true);
}

TEST (AcquisitionRecordingStatusTests,
      RejectsPartialRecordNodeActivityAsInconsistent)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (
            true,
            { { true, true }, { false, false } }),
        std::nullopt,
        true,
        false,
        2,
        1,
        1,
        false);
}

TEST (AcquisitionRecordingStatusTests,
      RejectsActiveRecordNodesWhenCallbacksAreOff)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (
            false,
            { { true, true }, { true, true } }),
        std::nullopt,
        false,
        false,
        2,
        2,
        2,
        false);
}

TEST (AcquisitionRecordingStatusTests,
      RejectsRecordWhenAnyRequiredWriterIsStopped)
{
    expectStatus (
        deriveAcquisitionRecordingStatus (
            true,
            { { true, true }, { true, false } }),
        std::nullopt,
        true,
        false,
        2,
        2,
        1,
        false);
}

TEST (AcquisitionRecordingStatusTests,
      CopiesRuntimeFactsWithoutRetainingItsSources)
{
    Status copied;
    {
        std::vector<Node> temporarySources {
            { true, true },
            { true, true }
        };
        copied = deriveAcquisitionRecordingStatus (
            true,
            temporarySources);
    }

    expectStatus (
        copied,
        Mode::record,
        true,
        true,
        2,
        2,
        2,
        true);
}

TEST (AcquisitionRecordingStatusTests,
      CapturesTheCurrentEmptyProcessorGraphWithoutRetainingIt)
{
    auto graph = std::make_unique<ProcessorGraph> (true);

    const auto copied =
        captureAcquisitionRecordingStatus (false, *graph);

    graph.reset();

    expectStatus (
        copied,
        Mode::idle,
        false,
        false,
        0,
        0,
        0,
        true);
}

TEST (AcquisitionRecordingStatusTests,
      SharedProductionOwnerAccessMapsCallbacksAndEveryCurrentNodeFact)
{
    InspectableAudio audio { true };
    InspectableWriter firstWriter { true };
    InspectableWriter secondWriter { false };
    InspectableRecordNode firstNode {
        true,
        0,
        &firstWriter
    };
    InspectableRecordNode secondNode {
        true,
        0,
        &secondWriter
    };
    InspectableRecordNode thirdNode {
        false,
        0,
        nullptr
    };
    InspectableGraph graph {
        { &firstNode, &secondNode, &thirdNode }
    };

    const auto copied =
        AcquisitionRecordingStatusDetail::
            captureFromOwners (audio, graph);

    expectStatus (
        copied,
        std::nullopt,
        true,
        false,
        3,
        2,
        1,
        false);
    EXPECT_EQ (audio.readCount, 1);
    EXPECT_EQ (graph.readCount, 1);
    EXPECT_EQ (firstNode.readCount, 1);
    EXPECT_EQ (secondNode.readCount, 1);
    EXPECT_EQ (thirdNode.readCount, 1);
    EXPECT_EQ (firstWriter.readCount, 1);
    EXPECT_EQ (secondWriter.readCount, 1);
}

TEST (AcquisitionRecordingStatusTests,
      ConcreteOwnersCaptureInactiveRecordNodesWithoutStartingRuntimeThreads)
{
    MessageManager::getInstance();
    const ScopedGuiRuntimeCleanup cleanup;
    const MessageManagerLock messageManagerLock;
    AccessClass::clearAccessClassStateForTesting();

    auto audio =
        std::make_unique<AudioComponent>();
    auto graph =
        std::make_unique<ProcessorGraph> (true);
    auto controlPanel =
        std::make_unique<ControlPanel> (
            graph.get(),
            audio.get(),
            true);
    controlPanel->updateRecordEngineList();

    const auto addRecordNode =
        [&] (int nodeId)
        {
            auto node =
                std::make_unique<RecordNode>();
            node->setNodeId (nodeId);
            node->setProcessorType (
                Plugin::Processor::RECORD_NODE);
            node->setHeadlessMode (true);
            AudioProcessorGraph::Node* added =
                graph->addNode (
                std::unique_ptr<AudioProcessor> (
                    node.release()),
                AudioProcessorGraph::NodeID (nodeId));
            return added != nullptr
                     ? static_cast<RecordNode*> (
                           added->getProcessor())
                     : nullptr;
        };

    const auto* first = addRecordNode (201);
    const auto* second = addRecordNode (202);
    ASSERT_NE (first, nullptr);
    ASSERT_NE (second, nullptr);
    ASSERT_FALSE (audio->callbacksAreActive());
    ASSERT_FALSE (first->getRecordingStatus());
    ASSERT_FALSE (second->getRecordingStatus());
    ASSERT_FALSE (
        first->recordThread->isThreadRunning());
    ASSERT_FALSE (
        second->recordThread->isThreadRunning());

    const auto copied =
        captureAcquisitionRecordingStatus (
            *audio,
            *graph);

    expectStatus (
        copied,
        Mode::idle,
        false,
        false,
        2,
        0,
        0,
        true);
}
