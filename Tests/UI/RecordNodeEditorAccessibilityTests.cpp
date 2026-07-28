#include "../../Source/Processors/RecordNode/RecordNodeEditor.h"
#include "../../Source/Processors/RecordNode/RecordNode.h"
#include "../../Source/Processors/Parameter/ParameterOwner.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/Processors/Settings/ContinuousChannel.h"
#include "../../Source/Processors/Settings/DataStream.h"
#include "../../Source/Audio/AudioComponent.h"
#include "../../Source/UI/ControlPanel.h"
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
            "100|" + std::string (settings[0]));
        Parameter::registerParameter (&parameter);
        RecordToggleParameterEditor editor (&parameter);
        auto* toggle = editor.getEditor();
        ASSERT_NE (toggle, nullptr);

        EXPECT_EQ (
            toggle->getComponentID(),
            "oe.parameter.100_" + String (settings[0]));
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
    EXPECT_EQ (
        browseHandler->getValueInterface()
            ->getCurrentValueAsString(),
        directory.getFullPathName());

    auto* useDefault = findDescendantById (
        editor,
        "oe.parameter.100_directory.use_default");
    ASSERT_NE (useDefault, nullptr);
    EXPECT_TRUE (useDefault->isVisible());
    EXPECT_EQ (
        useDefault->getTitle(),
        "Use default recording directory");
    EXPECT_EQ (
        useDefault->getDescription(),
        "Remove this Record Node directory override.");
    auto defaultHandler =
        useDefault->createAccessibilityHandler();
    ASSERT_NE (defaultHandler, nullptr);
    EXPECT_TRUE (
        defaultHandler->getActions().invoke (
            AccessibilityActionType::press));
    MessageManager::getInstance()
        ->runDispatchLoopUntil (50);
    EXPECT_EQ (
        parameter.getValueAsString(),
        "None");
    EXPECT_EQ (
        browseHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "default");
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
