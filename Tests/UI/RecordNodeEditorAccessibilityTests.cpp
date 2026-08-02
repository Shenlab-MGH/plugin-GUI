#include "../../Source/Processors/RecordNode/RecordNodeEditor.h"
#include "../../Source/Processors/RecordNode/RecordNode.h"
#include "../../Source/Processors/Parameter/ParameterOwner.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/Processors/Settings/ContinuousChannel.h"
#include "../../Source/Processors/Settings/DataStream.h"
#include "../../Source/Audio/AudioComponent.h"
#include "../../Source/UI/ControlPanel.h"
#include "../../Source/UI/SemanticComponent.h"
#include "../../Source/AccessClass.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

namespace
{
class RecordNodeEditorAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        messageManagerLock =
            std::make_unique<MessageManagerLock>();
        AccessClass::clearAccessClassStateForTesting();
        processorGraph =
            std::make_unique<ProcessorGraph> (true);
    }

    void TearDown() override
    {
        processorGraph.reset();
        AccessClass::clearAccessClassStateForTesting();
        Parameter::parameterMap.clear();
        messageManagerLock.reset();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<MessageManagerLock>
        messageManagerLock;
    std::unique_ptr<ProcessorGraph> processorGraph;
};

class TestParameterOwner final : public ParameterOwner
{
public:
    TestParameterOwner() : ParameterOwner (Type::OTHER) {}

    void parameterChangeRequest (Parameter* parameter) override
    {
        parameterChangeUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        parameterChangeCount.fetch_add (1);
        parameter->updateValue();
    }

    std::atomic<bool>
        parameterChangeUsedMessageThread {
            false
        };
    std::atomic<int>
        parameterChangeCount { 0 };
};

class TestRecordNode final : public RecordNode
{
public:
    uint16 addTestStream (int channelCount)
    {
        auto* stream = dataStreams.add (
            new DataStream (
                { "Probe AP",
                  "Neuropixels AP stream",
                  "probe.ap",
                  30000.0f,
                  false }));

        for (int channel = 0;
             channel < channelCount;
             ++channel)
        {
            continuousChannels.add (
                new ContinuousChannel (
                    { ContinuousChannel::ELECTRODE,
                      "AP" + String (channel),
                      "Probe AP channel",
                      "probe.ap." + String (channel),
                      0.195f,
                      stream }));
        }

        updateChannelIndexMaps();
        return stream->getStreamId();
    }
};

class StreamMonitorClickListener final
    : public Button::Listener
{
public:
    void buttonClicked (Button*) override
    {
        clickUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        clickCount.fetch_add (1);
    }

    std::atomic<int> clickCount { 0 };
    std::atomic<bool>
        clickUsedMessageThread { false };
};

Component* findDescendantById (
    Component& parent,
    const String& componentId)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == componentId)
            return child;

        if (auto* descendant =
                findDescendantById (*child, componentId))
            return descendant;
    }

    return nullptr;
}

template <typename ComponentType>
ComponentType* findDescendantOfType (Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* match =
                dynamic_cast<ComponentType*> (child))
            return match;

        if (auto* descendant =
                findDescendantOfType<ComponentType> (*child))
            return descendant;
    }

    return nullptr;
}
} // namespace

TEST_F (RecordNodeEditorAccessibilityTests,
        ExposesEventAndSpikeRecordingToggles)
{
    TestParameterOwner owner;

    for (const auto& settings :
         std::initializer_list<std::array<const char*, 3>> {
             { "events",
               "Record Events",
               "Toggle saving events coming into this node" },
             { "spikes",
               "Record Spikes",
               "Toggle saving spikes coming into this node" } })
    {
        BooleanParameter parameter (
            &owner,
            Parameter::PROCESSOR_SCOPE,
            settings[0],
            settings[1],
            settings[2],
            true);
        parameter.setKey (
            "100|" + std::string (settings[0]) + ": Main");
        Parameter::registerParameter (&parameter);
        RecordToggleParameterEditor editor (&parameter);
        auto* toggle = editor.getEditor();
        ASSERT_NE (toggle, nullptr);

        EXPECT_EQ (
            toggle->getComponentID(),
            parameter.getAutomationId());
        EXPECT_EQ (toggle->getTitle(), settings[1]);
        EXPECT_EQ (toggle->getDescription(), settings[2]);
        auto* label =
            findDescendantOfType<Label> (editor);
        ASSERT_NE (label, nullptr);
        EXPECT_FALSE (label->isAccessible());

        auto handler = toggle->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::toggleButton);
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::toggle));
        EXPECT_FALSE (
            handler->getActions().contains (
                AccessibilityActionType::press));
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_TRUE (
            handler->getCurrentState().isChecked());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            "On");

        auto* messageManager =
            MessageManager::getInstance();
        std::atomic<bool> workerEntered {
            false
        };
        std::atomic<bool> invoked {
            false
        };
        std::thread worker (
            [&]
            {
                workerEntered.store (true);
                invoked.store (
                    handler->getActions().invoke (
                        AccessibilityActionType::
                            toggle));
            });

        while (! workerEntered.load())
            std::this_thread::yield();

        EXPECT_EQ (
            owner.parameterChangeCount.load(),
            0);
        for (int attempt = 0;
             attempt < 20
                 && ! invoked.load();
             ++attempt)
        {
            messageManager
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();

        EXPECT_TRUE (invoked.load());
        EXPECT_EQ (
            owner.parameterChangeCount.load(),
            1);
        EXPECT_TRUE (
            owner
                .parameterChangeUsedMessageThread
                .load());
        EXPECT_FALSE (parameter.getBoolValue());
        EXPECT_FALSE (
            handler->getCurrentState().isChecked());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            "Off");

        parameter.setNextValue (
            true,
            false);
        editor.updateView();
        EXPECT_TRUE (
            handler->getCurrentState().isChecked());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            "On");

        owner.parameterChangeCount.store (
            0);
        owner
            .parameterChangeUsedMessageThread
            .store (false);
        editor.setEnabled (false);
        workerEntered.store (false);
        invoked.store (false);
        std::thread disabledWorker (
            [&]
            {
                workerEntered.store (true);
                invoked.store (
                    handler->getActions().invoke (
                        AccessibilityActionType::
                            toggle));
            });
        while (! workerEntered.load())
            std::this_thread::yield();
        for (int attempt = 0;
             attempt < 20
                 && ! invoked.load();
             ++attempt)
        {
            messageManager
                ->runDispatchLoopUntil (
                    10);
        }
        disabledWorker.join();

        EXPECT_TRUE (invoked.load());
        EXPECT_EQ (
            owner.parameterChangeCount.load(),
            0);
        EXPECT_TRUE (parameter.getBoolValue());
        EXPECT_TRUE (
            handler->getCurrentState().isChecked());
    }
}

TEST_F (RecordNodeEditorAccessibilityTests,
        ExposesRecordingDirectoryValueAndDefaultAction)
{
    TestParameterOwner owner;
    const auto directory =
        File::getSpecialLocation (
            File::tempDirectory);
    PathParameter parameter (
        &owner,
        Parameter::PROCESSOR_SCOPE,
        "directory",
        "Directory",
        "Select a directory to write data to",
        directory,
        StringArray {},
        true,
        false);
    parameter.setKey ("100|directory");
    Parameter::registerParameter (&parameter);
    parameter.setNextValue (
        directory.getFullPathName(),
        false);
    RecordPathParameterEditor editor (&parameter);
    editor.updateView();

    auto* browse = editor.getEditor();
    ASSERT_NE (browse, nullptr);
    auto* browseButton =
        dynamic_cast<TextButton*> (
            browse);
    ASSERT_NE (
        browseButton,
        nullptr);
    EXPECT_EQ (
        browse->getComponentID(),
        "oe.parameter.100_directory");
    EXPECT_EQ (browse->getTitle(), "Directory");
    EXPECT_EQ (
        browse->getDescription(),
        "Select a directory to write data to");
    EXPECT_FALSE (editor.getLabel()->isAccessible());

    auto browseHandler =
        browse->createAccessibilityHandler();
    ASSERT_NE (browseHandler, nullptr);
    EXPECT_EQ (
        browseHandler->getRole(),
        AccessibilityRole::button);
    ASSERT_NE (
        browseHandler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        browseHandler->getValueInterface()
            ->isReadOnly());
    struct BrowseSnapshot
    {
        String value;
        String help;
        bool enabled = false;
    };
    const auto readBrowseSnapshot =
        [&]
        {
            BrowseSnapshot snapshot;
            std::thread worker (
                [&]
                {
                    snapshot.value =
                        browseHandler
                            ->getValueInterface()
                            ->getCurrentValueAsString();
                    snapshot.help =
                        browseHandler->getHelp();
                    snapshot.enabled =
                        browseHandler->isEnabled();
                });
            worker.join();
            return snapshot;
        };
    auto browseSnapshot =
        readBrowseSnapshot();
    EXPECT_EQ (
        browseSnapshot.value,
        directory.getFullPathName());
    EXPECT_EQ (
        browseSnapshot.help,
        "Valid recording directory: "
            + directory.getFullPathName());
    EXPECT_TRUE (browseSnapshot.enabled);
    EXPECT_EQ (
        browseButton->getTooltip(),
        browseSnapshot.help);

    const auto invalidDirectory =
        directory
            .getNonexistentChildFile (
                "open-ephys-invalid-recording-directory",
                {},
                false);
    XmlElement savedParameters (
        "PARAMETERS");
    savedParameters.setAttribute (
        "directory",
        invalidDirectory
            .getFullPathName());
    parameter.fromXml (
        &savedParameters);
    editor.updateView();
    browseSnapshot =
        readBrowseSnapshot();
    EXPECT_EQ (
        browseSnapshot.value,
        invalidDirectory
            .getFullPathName());
    EXPECT_EQ (
        browseSnapshot.help,
        "Invalid recording directory: "
            + invalidDirectory
                  .getFullPathName());
    EXPECT_EQ (
        browseButton->getTooltip(),
        browseSnapshot.help);

    const auto watchedDirectory =
        File::getSpecialLocation (
            File::tempDirectory)
            .getChildFile (
                "open-ephys-watched-recording-directory")
            .getNonexistentSibling (
                false);
    ASSERT_TRUE (
        watchedDirectory
            .createDirectory());
    savedParameters.setAttribute (
        "directory",
        watchedDirectory
            .getFullPathName());
    parameter.fromXml (
        &savedParameters);
    editor.updateView();
    browseSnapshot =
        readBrowseSnapshot();
    EXPECT_EQ (
        browseSnapshot.value,
        watchedDirectory
            .getFullPathName());
    EXPECT_EQ (
        browseSnapshot.help,
        "Valid recording directory: "
            + watchedDirectory
                  .getFullPathName());
    ASSERT_TRUE (
        watchedDirectory
            .deleteRecursively());
    editor.updateView();
    browseSnapshot =
        readBrowseSnapshot();
    EXPECT_EQ (
        browseSnapshot.value,
        watchedDirectory
            .getFullPathName());
    EXPECT_EQ (
        browseSnapshot.help,
        "Invalid recording directory: "
            + watchedDirectory
                  .getFullPathName());
    EXPECT_EQ (
        browseButton->getTooltip(),
        browseSnapshot.help);

    auto* useDefault = findDescendantById (
        editor,
        "oe.parameter.100_directory.use_default");
    ASSERT_NE (useDefault, nullptr);
    EXPECT_TRUE (useDefault->isVisible());
    auto* useDefaultButton =
        dynamic_cast<TextButton*> (
            useDefault);
    ASSERT_NE (useDefaultButton, nullptr);
    EXPECT_EQ (
        useDefaultButton->getName(),
        "Revert Dir");
    EXPECT_EQ (
        useDefaultButton->getButtonText(),
        "default");
    EXPECT_EQ (
        useDefault->getTitle(),
        "Use default recording directory");
    EXPECT_EQ (
        useDefault->getDescription(),
        "Remove this Record Node directory override.");
    auto defaultHandler =
        useDefault->createAccessibilityHandler();
    ASSERT_NE (defaultHandler, nullptr);
    ASSERT_NE (
        defaultHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        defaultHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "default");
    const auto defaultActions =
        defaultHandler->getActions();
    const auto changeCountBeforeDefault =
        owner.parameterChangeCount.load();
    owner.parameterChangeUsedMessageThread
        .store (false);
    std::atomic<bool> defaultCompleted {
        false
    };
    bool defaultInvoked = false;
    std::thread defaultWorker (
        [&]
        {
            defaultInvoked =
                defaultActions.invoke (
                    AccessibilityActionType::
                        press);
            defaultCompleted.store (true);
        });
    while (! defaultCompleted.load())
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    defaultWorker.join();
    EXPECT_TRUE (defaultInvoked);
    for (int attempt = 0;
         attempt < 20
             && owner
                    .parameterChangeCount
                    .load()
                 == changeCountBeforeDefault;
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    EXPECT_EQ (
        owner.parameterChangeCount.load(),
        changeCountBeforeDefault + 1);
    EXPECT_TRUE (
        owner
            .parameterChangeUsedMessageThread
            .load());
    EXPECT_EQ (
        parameter.getValueAsString(),
        "None");
    browseSnapshot =
        readBrowseSnapshot();
    EXPECT_EQ (
        browseSnapshot.value,
        "default");
    EXPECT_EQ (
        browseSnapshot.help,
        String (
            "Using the default recording directory. Press to choose an override."));
    EXPECT_EQ (
        browseButton->getTooltip(),
        browseSnapshot.help);
}

TEST_F (RecordNodeEditorAccessibilityTests,
        ExposesRecordingChannelAndFifoState)
{
    auto audioComponent =
        std::make_unique<AudioComponent>();
    auto controlPanel =
        std::make_unique<ControlPanel> (
            processorGraph.get(),
            audioComponent.get(),
            true);
    controlPanel->updateRecordEngineList();

    TestRecordNode recordNode;
    const auto streamId =
        recordNode.addTestStream (8);
    StreamMonitor monitor (
        &recordNode,
        streamId);

    monitor.updateChannelCount (3);
    monitor.setFillPercentage (0.25f);

    auto handler =
        static_cast<Component&> (monitor)
            .createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::button);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler->getValueInterface()
            ->isReadOnly());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "3 of 8 channels selected; FIFO 25%");

    monitor.updateChannelCount (8);
    monitor.setFillPercentage (0.875f);

    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "8 of 8 channels selected; FIFO 88%");
}

TEST_F (RecordNodeEditorAccessibilityTests,
        StreamMonitorProvidersAndActionsAreWorkerSafe)
{
    auto audioComponent =
        std::make_unique<AudioComponent>();
    auto controlPanel =
        std::make_unique<ControlPanel> (
            processorGraph.get(),
            audioComponent.get(),
            true);
    controlPanel->updateRecordEngineList();

    TestRecordNode recordNode;
    const auto streamId =
        recordNode.addTestStream (8);
    auto monitor =
        std::make_unique<StreamMonitor> (
            &recordNode,
            streamId);
    applySemanticMetadata (
        *monitor,
        "oe.processor.100.streams.stream_probe_ap.recording_channels",
        "Probe AP recording channels",
        "Choose recorded channels and inspect FIFO usage.");
    monitor->updateChannelCount (4);
    monitor->setFillPercentage (0.5f);

    StreamMonitorClickListener listener;
    monitor->addListener (&listener);
    auto handler =
        monitor->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);

    String workerTitle;
    String workerDescription;
    String workerHelp;
    String workerValue;
    bool workerEnabled = false;
    std::thread reader (
        [&]
        {
            workerTitle = handler->getTitle();
            workerDescription =
                handler->getDescription();
            workerHelp = handler->getHelp();
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
            workerEnabled = handler->isEnabled();
        });
    reader.join();

    EXPECT_EQ (
        workerTitle,
        "Probe AP recording channels");
    EXPECT_EQ (
        workerDescription,
        "Choose recorded channels and inspect FIFO usage.");
    EXPECT_EQ (workerHelp, workerDescription);
    EXPECT_EQ (
        workerValue,
        "4 of 8 channels selected; FIFO 50%");
    EXPECT_TRUE (workerEnabled);

    const auto actions = handler->getActions();
    const auto invokePress =
        [&]
        {
            std::atomic<bool> completed { false };
            bool invoked = false;
            std::thread worker (
                [&]
                {
                    invoked = actions.invoke (
                        AccessibilityActionType::press);
                    completed.store (true);
                });
            while (! completed.load())
            {
                MessageManager::getInstance()
                    ->runDispatchLoopUntil (10);
            }
            worker.join();
            return invoked;
        };

    EXPECT_TRUE (invokePress());
    for (int attempt = 0;
         attempt < 20
             && listener.clickCount.load() == 0;
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    EXPECT_EQ (listener.clickCount.load(), 1);
    EXPECT_TRUE (
        listener.clickUsedMessageThread.load());

    monitor->setEnabled (false);
    EXPECT_FALSE (handler->isEnabled());
    EXPECT_TRUE (invokePress());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.clickCount.load(), 1);

    handler.reset();
    monitor.reset();
    EXPECT_TRUE (invokePress());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.clickCount.load(), 1);
}

TEST_F (RecordNodeEditorAccessibilityTests,
        ExposesStructuredDiskMonitorState)
{
    auto audioComponent =
        std::make_unique<AudioComponent>();
    auto controlPanel =
        std::make_unique<ControlPanel> (
            processorGraph.get(),
            audioComponent.get(),
            true);
    controlPanel->updateRecordEngineList();

    TestRecordNode recordNode;
    recordNode.setNodeId (100);
    DiskMonitor monitor (
        &recordNode);
    applySemanticMetadata (
        monitor,
        "oe.processor.100.disk_usage",
        "Record node disk usage",
        "Recording-volume usage and capacity status.");
    auto handler =
        monitor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::progressBar);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler->getValueInterface()
            ->isReadOnly());

    monitor.updateDiskSpace (
        0.75f);
    EXPECT_DOUBLE_EQ (
        handler->getValueInterface()
            ->getCurrentValue(),
        0.25);

    constexpr int64 gibibyte =
        int64 (1) << 30;
    monitor.update (
        0.0f,
        12 * gibibyte,
        0.0f);
    EXPECT_EQ (
        handler->getHelp(),
        "Status: idle; Available: 12.00 GB");
    EXPECT_EQ (
        monitor.getTooltip(),
        handler->getHelp());

    constexpr float megabytesPerSecond =
        2.5f;
    const auto bytesPerMillisecond =
        megabytesPerSecond
        * float (int64 (1) << 20)
        / 1000.0f;
    monitor.update (
        bytesPerMillisecond,
        10 * gibibyte,
        10.0f * 60.0f);
    EXPECT_EQ (
        handler->getHelp(),
        "Status: recording; Available: 10.00 GB; Remaining: 10 min; Data rate: 2.50 MB/s");
    EXPECT_EQ (
        monitor.getTooltip(),
        handler->getHelp());

    monitor.directoryInvalid (
        false);
    EXPECT_DOUBLE_EQ (
        handler->getValueInterface()
            ->getCurrentValue(),
        1.0);
    EXPECT_EQ (
        handler->getHelp(),
        "Status: invalid directory");
    EXPECT_EQ (
        monitor.getTooltip(),
        handler->getHelp());

    monitor.lowDiskSpace();
    EXPECT_EQ (
        handler->getHelp(),
        "Status: recording stopped; Reason: less than 5 minutes of disk space remaining");
    EXPECT_EQ (
        monitor.getTooltip(),
        handler->getHelp());

    monitor.update (
        bytesPerMillisecond,
        9 * gibibyte,
        9.0f * 60.0f);
    EXPECT_EQ (
        handler->getHelp(),
        "Status: recording stopped; Reason: less than 5 minutes of disk space remaining");
    monitor.update (
        0.0f,
        9 * gibibyte,
        0.0f);
    EXPECT_EQ (
        handler->getHelp(),
        "Status: recording stopped; Reason: less than 5 minutes of disk space remaining");
}

TEST_F (RecordNodeEditorAccessibilityTests,
        ReadsDiskMonitorAccessibilitySnapshotFromWorkerThread)
{
    auto audioComponent =
        std::make_unique<AudioComponent>();
    auto controlPanel =
        std::make_unique<ControlPanel> (
            processorGraph.get(),
            audioComponent.get(),
            true);
    controlPanel->updateRecordEngineList();

    TestRecordNode recordNode;
    recordNode.setNodeId (100);
    DiskMonitor monitor (
        &recordNode);
    auto handler =
        monitor.createAccessibilityHandler();

    monitor.updateDiskSpace (
        0.5f);
    monitor.update (
        0.0f,
        int64 (3) << 30,
        0.0f);

    double publishedValue = 0.0;
    String publishedHelp;
    std::thread reader (
        [&]
        {
            publishedValue =
                handler->getValueInterface()
                    ->getCurrentValue();
            publishedHelp =
                handler->getHelp();
        });
    reader.join();

    EXPECT_DOUBLE_EQ (
        publishedValue,
        0.5);
    EXPECT_EQ (
        publishedHelp,
        "Status: idle; Available: 3.00 GB");
}
