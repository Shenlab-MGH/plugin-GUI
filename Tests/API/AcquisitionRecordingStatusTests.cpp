#include "../../Source/Utils/AcquisitionRecordingStatus.h"

#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"

#include "gtest/gtest.h"

#include <memory>
#include <vector>

namespace
{
using Mode = AcquisitionRecordingMode;
using Node = RecordNodeRuntimeState;
using Status = AcquisitionRecordingStatus;

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
      DerivesIdleAndAcquireOnlyWhenEveryRecordNodeIsInactive)
{
    const std::vector<Node> inactiveNodes {
        { false, false },
        { false, true },
        { false, false }
    };

    expectStatus (
        deriveAcquisitionRecordingStatus (false, inactiveNodes),
        Mode::idle,
        false,
        false,
        3,
        0,
        1,
        true);

    expectStatus (
        deriveAcquisitionRecordingStatus (true, inactiveNodes),
        Mode::acquire,
        true,
        false,
        3,
        0,
        1,
        true);
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
