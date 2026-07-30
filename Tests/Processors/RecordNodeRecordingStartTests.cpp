#include "../../Source/Processors/RecordNode/RecordNodeRecordingStart.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace
{
using DirectoryState =
    RecordNodeRecordingDirectoryState;
using Error =
    RecordNodeRecordingStartError;
using Request =
    RecordNodeRecordingStartRequest;
using Result =
    RecordNodeRecordingStartResult;

Request request()
{
    return {
        102,
        R"(D:\recordings\session\Record Node 102)",
        3,
        7
    };
}

struct FakeOwner
{
    int ownedNodeId = 731;
    String ownedRecordingDirectory =
        R"(E:\owner\exact\Record Node 731)";
    int ownedExperimentNumber = 19;
    int ownedRecordingIndex = 23;
    bool writerIsRunning = false;
    bool pipelineIsAvailable = true;
    bool settingsPipelineIsAvailable = true;
    DirectoryState directoryState =
        DirectoryState::directory;
    RecordNodeRecordingDirectoryCreateResult
        directoryCreateResult { true, {} };
    bool writerStartSucceeds = true;
    bool settingsPersistenceNeeded = false;

    int nodeIdCount = 0;
    int recordingDirectoryCount = 0;
    int experimentNumberCount = 0;
    int recordingIndexCount = 0;
    int writerRunningCount = 0;
    int pipelineAvailableCount = 0;
    int settingsPipelineAvailableCount = 0;
    int directoryStateCount = 0;
    int createDirectoryCount = 0;
    int prepareRecordingCount = 0;
    int setFileComponentsCount = 0;
    int startWriterCount = 0;
    int markRecordingActiveCount = 0;
    int settingsNeededCount = 0;
    int persistSettingsCount = 0;

    String inspectedDirectory;
    String createdDirectory;
    String fileComponentsDirectory;
    int fileComponentsExperimentNumber = -1;
    int fileComponentsRecordingIndex = -1;
    String persistedSettingsDirectory;
    int persistedSettingsExperimentNumber = -1;
    std::vector<std::string> trace;

    int nodeId()
    {
        ++nodeIdCount;
        trace.push_back ("node_id");
        return ownedNodeId;
    }

    String recordingDirectory()
    {
        ++recordingDirectoryCount;
        trace.push_back ("recording_directory");
        return ownedRecordingDirectory;
    }

    int experimentNumber()
    {
        ++experimentNumberCount;
        trace.push_back ("experiment_number");
        return ownedExperimentNumber;
    }

    int recordingIndex()
    {
        ++recordingIndexCount;
        trace.push_back ("recording_index");
        return ownedRecordingIndex;
    }

    bool writerRunning()
    {
        ++writerRunningCount;
        trace.push_back ("writer_running");
        return writerIsRunning;
    }

    bool pipelineAvailable()
    {
        ++pipelineAvailableCount;
        trace.push_back ("pipeline_available");
        return pipelineIsAvailable;
    }

    bool settingsPersistenceAvailable()
    {
        ++settingsPipelineAvailableCount;
        trace.push_back (
            "settings_persistence_available");
        return settingsPipelineIsAvailable;
    }

    DirectoryState inspectRecordingDirectory (
        const String& directory)
    {
        ++directoryStateCount;
        inspectedDirectory = directory;
        trace.push_back ("directory_state");
        return directoryState;
    }

    RecordNodeRecordingDirectoryCreateResult
    createRecordingDirectory (
        const String& directory)
    {
        ++createDirectoryCount;
        createdDirectory = directory;
        trace.push_back ("create_directory");
        return directoryCreateResult;
    }

    void prepareRecording()
    {
        ++prepareRecordingCount;
        trace.push_back ("prepare_recording");
    }

    void setFileComponents (
        const String& directory,
        int experimentNumber,
        int recordingIndex)
    {
        ++setFileComponentsCount;
        fileComponentsDirectory = directory;
        fileComponentsExperimentNumber =
            experimentNumber;
        fileComponentsRecordingIndex =
            recordingIndex;
        trace.push_back ("set_file_components");
    }

    bool startWriter()
    {
        ++startWriterCount;
        trace.push_back ("start_writer");
        return writerStartSucceeds;
    }

    void markRecordingActive()
    {
        ++markRecordingActiveCount;
        trace.push_back ("mark_recording_active");
    }

    bool shouldPersistSettings()
    {
        ++settingsNeededCount;
        trace.push_back ("settings_needed");
        return settingsPersistenceNeeded;
    }

    void persistSettings (
        const String& directory,
        int experimentNumber)
    {
        ++persistSettingsCount;
        persistedSettingsDirectory =
            directory;
        persistedSettingsExperimentNumber =
            experimentNumber;
        trace.push_back ("persist_settings");
    }
};

void expectIdentity (
    const Result& result,
    const Request& expected)
{
    EXPECT_EQ (result.nodeId, expected.nodeId);
    EXPECT_EQ (
        result.recordingDirectory,
        expected.recordingDirectory);
    EXPECT_EQ (
        result.experimentNumber,
        expected.experimentNumber);
    EXPECT_EQ (
        result.recordingIndex,
        expected.recordingIndex);
}

} // namespace

TEST (RecordNodeRecordingStartTests,
      FromOwnerReadsIdentityOnceAndDelegatesSuccess)
{
    auto owner = FakeOwner {};
    owner.settingsPersistenceNeeded = true;

    const auto result =
        startRecordNodeRecordingFromOwner (owner);

    EXPECT_EQ (result.nodeId, owner.ownedNodeId);
    EXPECT_EQ (
        result.recordingDirectory,
        owner.ownedRecordingDirectory);
    EXPECT_EQ (
        result.experimentNumber,
        owner.ownedExperimentNumber);
    EXPECT_EQ (
        result.recordingIndex,
        owner.ownedRecordingIndex);
    EXPECT_TRUE (result.writerStarted);
    EXPECT_FALSE (result.error.has_value());
    EXPECT_EQ (owner.nodeIdCount, 1);
    EXPECT_EQ (owner.recordingDirectoryCount, 1);
    EXPECT_EQ (owner.experimentNumberCount, 1);
    EXPECT_EQ (owner.recordingIndexCount, 1);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "node_id",
            "recording_directory",
            "experiment_number",
            "recording_index",
            "writer_running",
            "pipeline_available",
            "settings_persistence_available",
            "directory_state",
            "prepare_recording",
            "set_file_components",
            "start_writer",
            "mark_recording_active",
            "settings_needed",
            "persist_settings"
        }));
    EXPECT_EQ (
        owner.inspectedDirectory,
        owner.ownedRecordingDirectory);
    EXPECT_EQ (
        owner.fileComponentsDirectory,
        owner.ownedRecordingDirectory);
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        owner.ownedExperimentNumber);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        owner.ownedRecordingIndex);
    EXPECT_EQ (
        owner.persistedSettingsDirectory,
        owner.ownedRecordingDirectory);
    EXPECT_EQ (
        owner.persistedSettingsExperimentNumber,
        owner.ownedExperimentNumber);
}

TEST (RecordNodeRecordingStartTests,
      FromOwnerReadsIdentityOnceAndDelegatesFailure)
{
    auto owner = FakeOwner {};
    owner.writerIsRunning = true;

    const auto result =
        startRecordNodeRecordingFromOwner (owner);

    EXPECT_EQ (result.nodeId, owner.ownedNodeId);
    EXPECT_EQ (
        result.recordingDirectory,
        owner.ownedRecordingDirectory);
    EXPECT_EQ (
        result.experimentNumber,
        owner.ownedExperimentNumber);
    EXPECT_EQ (
        result.recordingIndex,
        owner.ownedRecordingIndex);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::writerAlreadyRunning);
    EXPECT_EQ (owner.nodeIdCount, 1);
    EXPECT_EQ (owner.recordingDirectoryCount, 1);
    EXPECT_EQ (owner.experimentNumberCount, 1);
    EXPECT_EQ (owner.recordingIndexCount, 1);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "node_id",
            "recording_directory",
            "experiment_number",
            "recording_index",
            "writer_running"
        }));
    EXPECT_EQ (owner.directoryStateCount, 0);
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
}

TEST (RecordNodeRecordingStartTests,
      WriterAlreadyRunningStopsBeforeFilesystemOrSetup)
{
    auto owner = FakeOwner {};
    owner.writerIsRunning = true;
    owner.settingsPersistenceNeeded = true;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::writerAlreadyRunning);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running"
        }));
    EXPECT_EQ (owner.directoryStateCount, 0);
    EXPECT_EQ (owner.createDirectoryCount, 0);
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.setFileComponentsCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
    EXPECT_EQ (owner.markRecordingActiveCount, 0);
    EXPECT_EQ (owner.persistSettingsCount, 0);
    EXPECT_TRUE (owner.inspectedDirectory.isEmpty());
    EXPECT_TRUE (owner.createdDirectory.isEmpty());
    EXPECT_TRUE (
        owner.fileComponentsDirectory.isEmpty());
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        -1);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        -1);
}

TEST (RecordNodeRecordingStartTests,
      UnavailablePipelineStopsBeforeFilesystemOrSetup)
{
    auto owner = FakeOwner {};
    owner.pipelineIsAvailable = false;
    owner.settingsPersistenceNeeded = true;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::writerThreadStartFailed);
    EXPECT_EQ (
        result.detail,
        "Record writer pipeline is unavailable.");
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running",
            "pipeline_available"
        }));
    EXPECT_EQ (owner.directoryStateCount, 0);
    EXPECT_EQ (owner.createDirectoryCount, 0);
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.setFileComponentsCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
    EXPECT_EQ (owner.markRecordingActiveCount, 0);
    EXPECT_EQ (owner.settingsNeededCount, 0);
    EXPECT_EQ (owner.persistSettingsCount, 0);
}

TEST (RecordNodeRecordingStartTests,
      EmptyRecordingDirectoryFailsBeforeOwnerPreflight)
{
    auto owner = FakeOwner {};
    auto input = request();
    input.recordingDirectory = {};

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::recordingDirectoryCreateFailed);
    EXPECT_EQ (
        result.detail,
        "Recording directory is empty.");
    EXPECT_TRUE (owner.trace.empty());
    EXPECT_EQ (owner.writerRunningCount, 0);
    EXPECT_EQ (owner.pipelineAvailableCount, 0);
    EXPECT_EQ (owner.directoryStateCount, 0);
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
}

TEST (RecordNodeRecordingStartTests,
      UnavailableSettingsPersistenceStopsBeforeFilesystemOrSetup)
{
    auto owner = FakeOwner {};
    owner.settingsPersistenceNeeded = true;
    owner.settingsPipelineIsAvailable = false;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::writerThreadStartFailed);
    EXPECT_EQ (
        result.detail,
        "Recording settings persistence"
        " is unavailable.");
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running",
            "pipeline_available",
            "settings_persistence_available"
        }));
    EXPECT_EQ (owner.directoryStateCount, 0);
    EXPECT_EQ (owner.createDirectoryCount, 0);
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.setFileComponentsCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
    EXPECT_EQ (owner.markRecordingActiveCount, 0);
    EXPECT_EQ (owner.persistSettingsCount, 0);
}

TEST (RecordNodeRecordingStartTests,
      ExistingDirectorySkipsCreateAndStartsWriter)
{
    auto owner = FakeOwner {};
    owner.directoryState =
        DirectoryState::directory;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_TRUE (result.writerStarted);
    EXPECT_FALSE (result.error.has_value());
    EXPECT_EQ (owner.createDirectoryCount, 0);
    EXPECT_EQ (owner.startWriterCount, 1);
    EXPECT_EQ (owner.markRecordingActiveCount, 1);
    EXPECT_EQ (owner.persistSettingsCount, 0);
    EXPECT_EQ (
        owner.inspectedDirectory,
        input.recordingDirectory);
    EXPECT_TRUE (owner.createdDirectory.isEmpty());
    EXPECT_EQ (
        owner.fileComponentsDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        input.experimentNumber);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        input.recordingIndex);
}

TEST (RecordNodeRecordingStartTests,
      MissingDirectoryCreatesBeforeStartingWriter)
{
    auto owner = FakeOwner {};
    owner.directoryState =
        DirectoryState::missing;
    owner.directoryCreateResult = {
        true,
        {}
    };
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_TRUE (result.writerStarted);
    EXPECT_FALSE (result.error.has_value());
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running",
            "pipeline_available",
            "settings_persistence_available",
            "directory_state",
            "create_directory",
            "prepare_recording",
            "set_file_components",
            "start_writer",
            "mark_recording_active",
            "settings_needed"
        }));
    EXPECT_EQ (
        owner.inspectedDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.createdDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.fileComponentsDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        input.experimentNumber);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        input.recordingIndex);
}

TEST (RecordNodeRecordingStartTests,
      DirectoryCreateFailureReturnsExactDetailAndSkipsSetup)
{
    auto owner = FakeOwner {};
    owner.directoryState =
        DirectoryState::missing;
    owner.directoryCreateResult = {
        false,
        "Access is denied."
    };
    owner.settingsPersistenceNeeded = true;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::recordingDirectoryCreateFailed);
    EXPECT_EQ (
        result.detail,
        "Access is denied.");
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running",
            "pipeline_available",
            "settings_persistence_available",
            "directory_state",
            "create_directory"
        }));
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.setFileComponentsCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
    EXPECT_EQ (owner.markRecordingActiveCount, 0);
    EXPECT_EQ (owner.persistSettingsCount, 0);
    EXPECT_EQ (
        owner.inspectedDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.createdDirectory,
        input.recordingDirectory);
    EXPECT_TRUE (
        owner.fileComponentsDirectory.isEmpty());
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        -1);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        -1);
}

TEST (RecordNodeRecordingStartTests,
      EmptyDirectoryCreateDetailUsesStableFallback)
{
    auto owner = FakeOwner {};
    owner.directoryState =
        DirectoryState::missing;
    owner.directoryCreateResult = {
        false,
        {}
    };
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::recordingDirectoryCreateFailed);
    EXPECT_EQ (
        result.detail,
        "Could not create recording directory.");
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running",
            "pipeline_available",
            "settings_persistence_available",
            "directory_state",
            "create_directory"
        }));
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.setFileComponentsCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
}

TEST (RecordNodeRecordingStartTests,
      ExistingNonDirectoryFailsWithoutCreateOrSetup)
{
    auto owner = FakeOwner {};
    owner.directoryState =
        DirectoryState::nonDirectory;
    owner.settingsPersistenceNeeded = true;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::recordingDirectoryCreateFailed);
    EXPECT_FALSE (result.detail.isEmpty());
    EXPECT_EQ (owner.createDirectoryCount, 0);
    EXPECT_EQ (owner.prepareRecordingCount, 0);
    EXPECT_EQ (owner.setFileComponentsCount, 0);
    EXPECT_EQ (owner.startWriterCount, 0);
    EXPECT_EQ (owner.markRecordingActiveCount, 0);
    EXPECT_EQ (owner.persistSettingsCount, 0);
    EXPECT_EQ (
        owner.inspectedDirectory,
        input.recordingDirectory);
    EXPECT_TRUE (owner.createdDirectory.isEmpty());
    EXPECT_TRUE (
        owner.fileComponentsDirectory.isEmpty());
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        -1);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        -1);
}

TEST (RecordNodeRecordingStartTests,
      WriterStartFailureLeavesRecordingInactive)
{
    auto owner = FakeOwner {};
    owner.writerStartSucceeds = false;
    owner.settingsPersistenceNeeded = true;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_FALSE (result.writerStarted);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (
        *result.error,
        Error::writerThreadStartFailed);
    EXPECT_EQ (owner.prepareRecordingCount, 1);
    EXPECT_EQ (owner.setFileComponentsCount, 1);
    EXPECT_EQ (owner.startWriterCount, 1);
    EXPECT_EQ (owner.markRecordingActiveCount, 0);
    EXPECT_EQ (owner.settingsNeededCount, 0);
    EXPECT_EQ (owner.persistSettingsCount, 0);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running",
            "pipeline_available",
            "settings_persistence_available",
            "directory_state",
            "prepare_recording",
            "set_file_components",
            "start_writer"
        }));
    EXPECT_EQ (
        owner.inspectedDirectory,
        input.recordingDirectory);
    EXPECT_TRUE (owner.createdDirectory.isEmpty());
    EXPECT_EQ (
        owner.fileComponentsDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        input.experimentNumber);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        input.recordingIndex);
}

TEST (RecordNodeRecordingStartTests,
      SuccessfulStartPreservesIdentityAndPersistsSettingsAfterActive)
{
    auto owner = FakeOwner {};
    owner.settingsPersistenceNeeded = true;
    const auto input = request();

    const auto result =
        startRecordNodeRecording (
            input,
            owner);

    expectIdentity (result, input);
    EXPECT_TRUE (result.writerStarted);
    EXPECT_FALSE (result.error.has_value());
    EXPECT_TRUE (result.detail.isEmpty());
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "writer_running",
            "pipeline_available",
            "settings_persistence_available",
            "directory_state",
            "prepare_recording",
            "set_file_components",
            "start_writer",
            "mark_recording_active",
            "settings_needed",
            "persist_settings"
        }));
    EXPECT_EQ (owner.markRecordingActiveCount, 1);
    EXPECT_EQ (owner.persistSettingsCount, 1);
    EXPECT_EQ (
        owner.persistedSettingsDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.persistedSettingsExperimentNumber,
        input.experimentNumber);
    EXPECT_EQ (
        owner.inspectedDirectory,
        input.recordingDirectory);
    EXPECT_TRUE (owner.createdDirectory.isEmpty());
    EXPECT_EQ (
        owner.fileComponentsDirectory,
        input.recordingDirectory);
    EXPECT_EQ (
        owner.fileComponentsExperimentNumber,
        input.experimentNumber);
    EXPECT_EQ (
        owner.fileComponentsRecordingIndex,
        input.recordingIndex);
}
