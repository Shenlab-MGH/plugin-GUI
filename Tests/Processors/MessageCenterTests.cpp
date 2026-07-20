#include "gtest/gtest.h"

#include <ProcessorHeaders.h>
#include <Processors/MessageCenter/MessageCenter.h>
#include <Processors/ProcessorGraph/ProcessorGraph.h>
#include <memory>

class MessageCenterTests : public testing::Test
{
protected:
    void SetUp() override
    {
        messageCenter = std::make_unique<MessageCenter>();
    }

protected:
    std::unique_ptr<MessageCenter> messageCenter;
};

class MessageCenterBroadcastTests : public testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::deleteInstance();
        MessageManager::getInstance();
        AccessClass::clearAccessClassStateForTesting();

        processorGraph = std::make_unique<ProcessorGraph> (true);
        messageCenter = processorGraph->getMessageCenter();
    }

    void TearDown() override
    {
        messageCenter = nullptr;
        processorGraph = nullptr;

        AccessClass::clearAccessClassStateForTesting();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    MessageCenter* messageCenter = nullptr;
    std::unique_ptr<ProcessorGraph> processorGraph;
};

TEST_F(MessageCenterTests, Constructor)
{
    EXPECT_EQ(messageCenter->getName(), "Message Center");
}

TEST_F(MessageCenterTests, AddSpecialProcessorChannels)
{
    messageCenter->addSpecialProcessorChannels();
    EXPECT_EQ(messageCenter->getDataStreams().size(), 1);
    EXPECT_EQ(messageCenter->getEventChannels().size(), 1);
}

TEST_F(MessageCenterTests, GetMessageChannel)
{
    EXPECT_EQ(messageCenter->getMessageChannel(), nullptr);
}

TEST_F(MessageCenterTests, GetMessageStream)
{
    EXPECT_EQ(messageCenter->getMessageStream(), nullptr);
}

TEST_F(MessageCenterTests, AddOutgoingMessage)
{
    messageCenter->addOutgoingMessage("Test Message", 100);
    EXPECT_EQ(messageCenter->getSavedMessages().size(), 1);

    const auto& savedMsg = messageCenter->getSavedMessages()[0];
    EXPECT_EQ(savedMsg, "Test Message");
}

TEST_F(MessageCenterTests, AddSavedMessage)
{
    messageCenter->addSavedMessage("Test Message");
    EXPECT_EQ(messageCenter->getSavedMessages().size(), 1);

    const auto& savedMsg = messageCenter->getSavedMessages()[0];
    EXPECT_EQ(savedMsg, "Test Message");
}

TEST_F(MessageCenterTests, ClearSavedMessages)
{
    messageCenter->addSavedMessage("Test Message");
    EXPECT_EQ(messageCenter->getSavedMessages().size(), 1);

    messageCenter->clearSavedMessages();
    EXPECT_EQ(messageCenter->getSavedMessages().size(), 0);
}

static TextEventPtr processBroadcast (MessageCenter& messageCenter, const String& text)
{
    messageCenter.addSpecialProcessorChannels();
    messageCenter.broadcastMessage (text, 100);

    AudioBuffer<float> audioBuffer (1, 1);
    audioBuffer.clear();
    MidiBuffer eventBuffer;
    AudioProcessor& processor = messageCenter;
    processor.processBlock (audioBuffer, eventBuffer);

    EXPECT_EQ (eventBuffer.getNumEvents(), 1);
    if (eventBuffer.getNumEvents() != 1)
        return nullptr;

    const auto metadata = *eventBuffer.begin();
    return TextEvent::deserialize (metadata.data, messageCenter.getMessageChannel());
}

TEST_F (MessageCenterBroadcastTests, BroadcastAtLimitIsPreserved)
{
    const String input = String::repeatedString ("a", 512);
    const auto event = processBroadcast (*messageCenter, input);
    ASSERT_NE (event, nullptr);
    EXPECT_EQ (event->getText(), input);
}

TEST_F (MessageCenterBroadcastTests, BroadcastOverLimitIsTruncatedSafely)
{
    const String input = String::repeatedString ("b", 514);
    const auto event = processBroadcast (*messageCenter, input);
    ASSERT_NE (event, nullptr);
    EXPECT_EQ (event->getText().length(), 512);
    EXPECT_EQ (event->getText(), input.substring (0, 512));
}
