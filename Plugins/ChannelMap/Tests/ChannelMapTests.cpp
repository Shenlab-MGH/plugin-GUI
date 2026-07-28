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

#include <stdio.h>

#include "gtest/gtest.h"

#include "../ChannelMap.h"
#include "../ChannelMapEditor.h"
#include "../../../Source/UI/SemanticComponent.h"
#include <ModelApplication.h>
#include <ModelProcessors.h>
#include <ProcessorHeaders.h>
#include <TestFixtures.h>
#include <atomic>
#include <filesystem>
#include <thread>

namespace
{
Component* findDescendantBySemanticId (
    Component& parent,
    StringRef id)
{
    if (parent.getComponentID()
        == id)
        return &parent;

    for (auto* child :
         parent.getChildren())
    {
        if (auto* result =
                findDescendantBySemanticId (
                    *child,
                    id))
            return result;
    }

    return nullptr;
}

class ThreadTrackingButtonListener final
    : public Button::Listener
{
public:
    void buttonClicked (Button*) override
    {
        callbackCount.fetch_add (1);
        callbackUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
    }

    std::atomic<int> callbackCount { 0 };
    std::atomic<bool>
        callbackUsedMessageThread { false };
};
} // namespace

class ChannelMapTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        numChannels = 8;
        tester = std::make_unique<ProcessorTester> (TestSourceNodeBuilder (FakeSourceNodeParams {
            numChannels,
            sampleRate,
            1.0,
        }));
        processor = tester->createProcessor<ChannelMap> (Plugin::Processor::FILTER);
        ASSERT_EQ (processor->getNumDataStreams(), 1);
        streamId = processor->getDataStreams()[0]->getStreamId();

        prbFilePath = std::filesystem::temp_directory_path() / "prb_file.json";
    }

    void TearDown() override
    {
        if (std::filesystem::exists (prbFilePath))
        {
            std::filesystem::remove (prbFilePath);
        }
    }

    ChannelMap* processor;
    int numChannels;
    uint16 streamId;
    std::unique_ptr<ProcessorTester> tester;
    float sampleRate = 30000.0;
    std::filesystem::path prbFilePath;
};

TEST_F (ChannelMapTests,
        ExposesChannelMapFileControls)
{
    processor->setHeadlessMode (false);
    processor->setProcessorType (
        Plugin::Processor::SPLITTER);
    auto* editor =
        static_cast<
            GenericProcessor*> (
                processor)
            ->createEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".channel_map";
    auto* load =
        findDescendantBySemanticId (
            *editor,
            prefix + ".load_prb");
    auto* save =
        findDescendantBySemanticId (
            *editor,
            prefix + ".save_prb");
    ASSERT_NE (load, nullptr);
    EXPECT_EQ (
        load->getTitle(),
        "Load channel map...");
    EXPECT_EQ (
        load->getDescription(),
        "Load channel map settings for the selected data stream from a .prb file.");
    ASSERT_NE (save, nullptr);
    EXPECT_EQ (
        save->getTitle(),
        "Save channel map...");
    EXPECT_EQ (
        save->getDescription(),
        "Save channel map settings for the selected data stream to a .prb file.");
}

TEST_F (ChannelMapTests,
        ChannelMapFileButtonActionsAreWorkerSafe)
{
    auto verifyButton =
        [] (auto button,
            StringRef semanticId,
            StringRef title)
    {
        applySemanticMetadata (
            *button,
            semanticId,
            title,
            "Choose a channel map file.",
            "Opens the system file chooser.");
        button
            ->refreshAccessibilityState();
        ThreadTrackingButtonListener
            listener;
        button->addListener (&listener);
        auto handler =
            button
                ->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::button);
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::press));
        EXPECT_FALSE (
            handler->getActions().contains (
                AccessibilityActionType::toggle));
        EXPECT_EQ (
            handler->getValueInterface(),
            nullptr);

        String workerTitle;
        String workerDescription;
        String workerHelp;
        bool workerEnabled = false;
        std::atomic<bool>
            actionReturned { false };
        std::thread worker (
            [&]
            {
                workerTitle =
                    handler->getTitle();
                workerDescription =
                    handler
                        ->getDescription();
                workerHelp =
                    handler->getHelp();
                workerEnabled =
                    handler->isEnabled();
                actionReturned.store (
                    handler
                        ->getActions()
                        .invoke (
                            AccessibilityActionType::
                                press));
            });

        auto* messageManager =
            MessageManager::getInstance();
        for (int attempt = 0;
             attempt < 20
                 && (! actionReturned.load()
                     || listener
                                .callbackCount
                                .load()
                            == 0);
             ++attempt)
        {
            messageManager
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();

        EXPECT_EQ (
            workerTitle,
            title);
        EXPECT_EQ (
            workerDescription,
            "Choose a channel map file.");
        EXPECT_EQ (
            workerHelp,
            "Opens the system file chooser.");
        EXPECT_TRUE (workerEnabled);
        EXPECT_TRUE (
            actionReturned.load());
        EXPECT_EQ (
            listener.callbackCount.load(),
            1);
        EXPECT_TRUE (
            listener
                .callbackUsedMessageThread
                .load());

        button->setEnabled (false);
        const auto actions =
            handler->getActions();
        EXPECT_TRUE (
            actions.invoke (
                AccessibilityActionType::
                    press));
        messageManager
            ->runDispatchLoopUntil (10);
        EXPECT_EQ (
            listener.callbackCount.load(),
            1);
        EXPECT_FALSE (
            handler->isEnabled());

        handler.reset();
        button.reset();
        EXPECT_TRUE (
            actions.invoke (
                AccessibilityActionType::
                    press));
        messageManager
            ->runDispatchLoopUntil (10);
        EXPECT_EQ (
            listener.callbackCount.load(),
            1);
    };

    verifyButton (
        std::make_unique<
            ChannelMapLoadButton> (
            "Load channel map"),
        "oe.test.channel_map.load_prb",
        "Load channel map...");
    verifyButton (
        std::make_unique<
            ChannelMapSaveButton> (
            "Save channel map"),
        "oe.test.channel_map.save_prb",
        "Save channel map...");
}

TEST_F (ChannelMapTests, TestRemapsChannels)
{
    // Make it backwards:
    juce::Array<int> newChanOrder;
    std::unordered_map<int, int> oldToNewChanMapping;
    for (int i = 0; i < numChannels; i++)
    {
        newChanOrder.add (numChannels - 1 - i);
        oldToNewChanMapping[i] = numChannels - 1 - i;
    }

    processor->setChannelOrder (streamId, newChanOrder);
    processor->updateSettings();

    auto originalDataStream = tester->getSourceNodeDataStream (streamId);
    auto mappedDataStream = tester->getProcessorDataStream (processor->getNodeId(), streamId);

    ASSERT_EQ (mappedDataStream->getContinuousChannels().size(),
               originalDataStream->getContinuousChannels().size());
    for (std::pair<int, int> oldToNew : oldToNewChanMapping)
    {
        ASSERT_EQ (
            mappedDataStream->getContinuousChannels()[oldToNew.second]->getGlobalIndex(),
            originalDataStream->getContinuousChannels()[oldToNew.first]->getGlobalIndex());
    }
}

TEST_F (ChannelMapTests, TestRespectsEnabledChannels)
{
    int expectedMappedNumChans = 4;
    // Make it backwards:
    juce::Array<int> newChanOrder;
    std::unordered_map<int, int> oldToNewChanMapping;
    for (int i = 0; i < numChannels; i++)
    {
        if (i < expectedMappedNumChans)
        {
            // Enable channels explicitly
            processor->setChannelEnabled (streamId, i, 1);
            oldToNewChanMapping[i] = i;
        }
        else
        {
            // Disable some channels
            processor->setChannelEnabled (streamId, i, 0);
        }

        // Always map all channels, or else it'll be ignored
        newChanOrder.add (i);
    }

    processor->setChannelOrder (streamId, newChanOrder);
    processor->updateSettings();

    auto originalDataStream = tester->getSourceNodeDataStream (streamId);
    auto mappedDataStream = tester->getProcessorDataStream (processor->getNodeId(), streamId);

    ASSERT_EQ (originalDataStream->getContinuousChannels().size(), numChannels);
    ASSERT_EQ (mappedDataStream->getContinuousChannels().size(), expectedMappedNumChans);
    for (std::pair<int, int> oldToNew : oldToNewChanMapping)
    {
        ASSERT_EQ (
            mappedDataStream->getContinuousChannels()[oldToNew.second]->getGlobalIndex(),
            originalDataStream->getContinuousChannels()[oldToNew.first]->getGlobalIndex());
    }
}

TEST_F (ChannelMapTests, ConfigFileTest)
{
    std::vector<std::string> prbFileContentsList = {
        "{",
        "  \"0\": {",
        "    \"mapping\": [",
        "      7,",
        "      6,",
        "      5,",
        "      4,",
        "      3,",
        "      2,",
        "      1,",
        "      0",
        "    ],",
        "    \"enabled\": [",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true",
        "    ]",
        "  }",
        "}"
    };

    std::stringstream prbFileSs;
    for (const auto& line : prbFileContentsList)
    {
        prbFileSs << line << std::endl;
    }

    std::string prbFileContents = prbFileSs.str();
    auto f = juce::File (prbFilePath.string());
    {
        // Write and close it via braces
        juce::FileOutputStream outStream (f);
        outStream.writeString (prbFileContents);
    }

    processor->loadStreamSettings (streamId, f);
    processor->updateSettings();

    auto originalDataStream = tester->getSourceNodeDataStream (streamId);
    auto mappedDataStream = tester->getProcessorDataStream (processor->getNodeId(), streamId);

    ASSERT_EQ (originalDataStream->getContinuousChannels().size(), numChannels);
    ASSERT_EQ (
        mappedDataStream->getContinuousChannels().size(),
        originalDataStream->getContinuousChannels().size());
    for (std::pair<int, int> oldToNew :
         std::unordered_map<int, int> ({
             { 0, 7 },
             { 1, 6 },
             { 2, 5 },
             { 3, 4 },
             { 4, 3 },
             { 5, 2 },
             { 6, 1 },
             { 7, 0 },
         }))
    {
        ASSERT_EQ (
            mappedDataStream->getContinuousChannels()[oldToNew.second]->getGlobalIndex(),
            originalDataStream->getContinuousChannels()[oldToNew.first]->getGlobalIndex());
    }
}
